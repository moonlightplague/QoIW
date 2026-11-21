#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "frontend/resolve.hpp"

namespace {
using qoiw::DType;
using qoiw::Expr;
using namespace qoiw::frontend;
constexpr auto abs_mode = Tolerance::Mode::Absolute;
constexpr auto rel_mode = Tolerance::Mode::Relative;

void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

template <class T>
PointResolution resolve(const std::vector<T>& input, const PointRequest& request, int threads = 1) {
    const auto dtype = std::is_same_v<T, float> ? DType::Float32 : DType::Float64;
    return resolve_pointwise(dtype, input.data(), input.size() * sizeof(T), request, threads);
}

void check_range(const std::optional<Range>& range, double low, double high) {
    check(range && range->min == low && range->max == high, "wrong finite range");
}

template <class T>
void ranges_and_mask() {
    const auto nan = std::numeric_limits<T>::quiet_NaN(), inf = std::numeric_limits<T>::infinity();
    const std::vector<T> input{-4, 0, 1, 4, 9, 42, nan, inf, -inf};
    const PointRequest req{{{Expr("sqrt(x)"), {rel_mode, .25}}, {Expr("x^2"), {rel_mode, .5}},
                            {Expr("2"), {abs_mode, 0}}}, Tolerance{rel_mode, .125}, Expr("x > 40")};
    const auto out = resolve(input, req);
    check_range(out.data_range, -4, 9);
    check_range(out.target_ranges.at(0), 0, 3);
    check_range(out.target_ranges.at(1), 0, 81);
    check_range(out.target_ranges.at(2), 2, 2);
    check(out.nonfinite_inputs == 3 && out.exact_mask == std::vector<std::uint64_t>{0x1e1}, "wrong exact mask");
    check(out.request.data_bound_abs == 13. / 8 && out.request.exact->text() == "x > 40", "resolved data request");
    check(out.request.targets.size() == 3 && out.request.targets[0].tolerance_abs == .75 &&
          out.request.targets[1].tolerance_abs == 40.5 && out.request.targets[2].tolerance_abs == 0, "target bounds");

    const auto masked = resolve(input, {{{Expr("1"), {rel_mode, .25}}}, Tolerance{rel_mode, .5}, Expr("1")});
    check(!masked.data_range && !masked.target_ranges[0] && masked.exact_mask[0] == 0x1ff, "all inputs masked");
    check(masked.request.targets[0].tolerance_abs == .25 && masked.request.data_bound_abs == .5, "empty range bounds");
    const auto singular = resolve<T>({-2, -1}, {{{Expr("ln(x)"), {rel_mode, .25}}}, {}, {}});
    check(!singular.target_ranges[0] && singular.request.targets[0].tolerance_abs == .25 &&
          !singular.request.data_bound_abs && singular.exact_mask[0] == 3, "empty QoI range");
    const auto constant = resolve<T>({3, 3}, {{{Expr("x"), {rel_mode, .25}}}, Tolerance{rel_mode, 0}, {}});
    check_range(constant.data_range, 3, 3);
    check(constant.request.targets[0].tolerance_abs == .25 && constant.request.data_bound_abs == 0 &&
          constant.exact_mask[0] == 0, "constant range");
}

void threaded() {
    std::vector<double> data(65537, 2);
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const std::array<std::size_t, 7> indices{0, 63, 64, 4095, 4096, 32768, data.size() - 1};
    std::vector<std::uint64_t> expected(1025);
    for (auto i : indices) {
        data[i] = i % 2 ? -1 : nan;
        expected[i / 64] |= std::uint64_t{1} << (i % 64);
    }
    const PointRequest request{{{Expr("sqrt(x)"), {abs_mode, 1}}}, Tolerance{rel_mode, .25}, {}};
    for (int threads : {1, 2, 7, 16, 0}) {
        const auto out = resolve(data, request, threads);
        check(out.exact_mask == expected && out.nonfinite_inputs == 5, "mask across chunk boundaries");
        check_range(out.data_range, -1, 2);
        check_range(out.target_ranges[0], std::sqrt(2.), std::sqrt(2.));
        check(out.request.data_bound_abs == .75, "parallel data bound");
    }
}

template <class Error, class F>
void rejects(F action) {
    try {
        action();
    } catch (const Error&) {
        return;
    }
    throw std::runtime_error("invalid range or tolerance accepted");
}

void extremes_and_validation() {
    const auto max = std::numeric_limits<double>::max();
    const std::vector<double> extremes{-max, max};
    PointRequest req{{{Expr("x"), {rel_mode, .5}}}, Tolerance{rel_mode, 0}, {}};
    const auto out = resolve(extremes, req);
    check(out.request.targets[0].tolerance_abs == max && out.request.data_bound_abs == 0, "overflow in range width");
    req.targets[0].tolerance.value = 1.;
    rejects<std::overflow_error>([&] { resolve(extremes, req); });
    for (double value : {-1., std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        rejects<std::invalid_argument>([&] { resolve(extremes, {{}, Tolerance{abs_mode, value}, {}}); });
        rejects<std::invalid_argument>([&] { resolve(extremes, {{{Expr("x"), {rel_mode, value}}}, {}, {}}); });
    }
    rejects<std::invalid_argument>([&] { resolve(extremes, {{}, Tolerance{static_cast<Tolerance::Mode>(42), 1}, {}}); });
    rejects<std::invalid_argument>([&] { resolve(extremes, {}, -1); });
    rejects<std::invalid_argument>([&] { resolve_pointwise(DType::Float64, nullptr, 8, {}, 1); });
    rejects<std::invalid_argument>([&] { resolve_pointwise(DType::Float64, extremes.data(), 7, {}, 1); });
    rejects<std::invalid_argument>([&] { resolve_pointwise(DType::Float64, extremes.data(), 0, {}, 1); });
    rejects<std::invalid_argument>([&] { resolve_pointwise(static_cast<DType>(0), extremes.data(), 8, {}, 1); });
}
}  // namespace

int main() {
    try {
        ranges_and_mask<float>();
        ranges_and_mask<double>();
        threaded();
        extremes_and_validation();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
