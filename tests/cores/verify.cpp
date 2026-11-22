#include "qoiw/qoiw.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
using namespace qoiw;

void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

template <class Work>
void rejects(Work work) {
    try {
        work();
    } catch (const Error& error) {
        if (error.code() == ErrorCode::InvalidArgument) return;
        throw;
    }
    throw std::runtime_error("invalid verification call succeeded");
}

void overlapping_constraints() {
    const float original[]{1, 2, 4, 8, 8};
    const float decoded[]{1.25, 2.5, 2, 8.5, 8};
    const Request request{{Target::pointwise("x^2").rel(.125), Target::pointwise("x < 4").abs(0)}, Tol::rel(1. / 14), {}};
    const ArrayView input(original, {1, 5});
    const auto result = verify(input, ArrayView(decoded, {1, 5}), request);
    check(!result.ok() && result.values == 5 && result.violating_values == 2 && result.exact_violations == 0,
          "a value that fails multiple checks should count once");
    check(result.data_bound_abs == .5 && result.data.violations == 1 && result.data.max_abs_error == 2,
          "inclusive relative data bound");
    const auto& square = result.targets.at(0);
    const auto& threshold = result.targets.at(1);
    check(square.target.expression == "x^2" && square.target.tolerance_abs == 7.875 && square.errors.violations == 2 &&
          square.errors.max_abs_error == 12 && square.errors.nonfinite_violations == 0, "squared field statistics");
    check(threshold.target.expression == "x < 4" && threshold.target.tolerance_abs == 0 &&
          threshold.errors.violations == 1 && threshold.errors.max_abs_error == 1,
          "data violation must not suppress a QoI violation");
    const auto identical = verify(input, input, request);
    check(identical.ok() && identical.targets.at(0).errors.max_abs_error == 0 &&
          identical.data_bound_abs == result.data_bound_abs, "identical fields");
}

void special_values() {
    const std::uint64_t bits[]{0x7ff8000000000001, 0x7ff8000000000002};
    double nan[2];
    std::memcpy(nan, bits, sizeof(nan));
    const auto inf = std::numeric_limits<double>::infinity();
    const double original[]{+0., -0., -1, 2, 2, 9, 3, nan[0], nan[0], inf, -inf, 3, -1};
    double decoded[]{+0., +0., -2, -1, 9, 3, 3, nan[1], nan[0], inf, inf, nan[1], -1};
    double copy[13];
    std::memcpy(copy, decoded, sizeof(decoded));
    const Request request{{Target::pointwise("ln(x)").abs(10)}, Tol::none(), "x > 8"};
    const auto result = verify(ArrayView(original, {13}), ArrayView(decoded, {13}), request);
    check(!result.ok() && result.violating_values == 8 && result.exact_violations == 2 &&
          !result.data_bound_abs && result.data.violations == 3 && result.data.nonfinite_violations == 3 &&
          result.data.max_abs_error == 7, "exact transitions or nonfinite storage");
    const auto& log = result.targets.at(0).errors;
    check(log.violations == 3 && log.nonfinite_violations == 3 &&
          std::abs(log.max_abs_error - (std::log(9.) - std::log(2.))) < 1e-14,
          "nonfinite QoI or finite maximum");
    check(!std::memcmp(copy, decoded, sizeof(decoded)), "verification changed reconstruction");
}

void bounds_and_invalid_inputs() {
    const double max = std::numeric_limits<double>::max();
    const double a[]{max}, b[]{-max};
    const Request request{{Target::pointwise("x").abs(max)}, Tol::abs(max), {}};
    const ArrayView input(a, {1});
    const auto overflow = verify(input, ArrayView(b, {1}), request);
    check(!overflow.ok() && std::isinf(overflow.data.max_abs_error) && overflow.data.nonfinite_violations == 0 &&
          std::isinf(overflow.targets.at(0).errors.max_abs_error), "overflow of finite operands");
    const float original[]{1}, decoded[]{static_cast<float>(4. / 3)};
    const Request tight{{Target::pointwise("x").abs(1. / 3)}, Tol::none(), {}};
    check(!verify(ArrayView(original, {1}), ArrayView(decoded, {1}), tight).ok(), "check stored float32 values");
    rejects([&] { verify(input, ArrayView(b, {1, 1})); });
    rejects([&] { verify(input, ArrayView(original, {1})); });
    rejects([&] { verify(input, ArrayView(static_cast<const double*>(nullptr), {1})); });
    rejects([&] { verify(input, input, {}, -1); });
    rejects([&] { verify(input, input, {{Target::pointwise("x+").abs(1)}, {}, {}}); });
    rejects([&] { verify(input, input, {{Target::pointwise("x").rel(-1)}, {}, {}}); });
}

void parallel_groups() {
    constexpr std::size_t n = 196609;
    const std::vector<float> original(n, 1);
    auto decoded = original;
    for (const auto i : {0, 2047, 2048, 65535, 65536, 131072, 196608}) decoded.at(i) = -1;
    decoded.back() = 4;
    const Request request{{Target::pointwise("x").abs(1)}, Tol::abs(1), {}};
    for (const auto threads : {1, 2, 7, 16, 0}) {
        const auto result = verify(ArrayView(original.data(), {n}), ArrayView(decoded.data(), {n}), request, threads);
        check(result.values == n && result.violating_values == 7 && result.data.violations == 7 &&
              result.data.max_abs_error == 3 && result.targets.at(0).errors.violations == 7 &&
              result.targets[0].errors.max_abs_error == 3, "wrong verification counts at group boundaries");
    }
}
}  // namespace

int main() {
    try {
        overlapping_constraints();
        special_values();
        bounds_and_invalid_inputs();
        parallel_groups();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
