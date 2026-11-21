#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "frontend/prior.hpp"
#include "frontend/sample.hpp"

namespace {
using qoiw::Expr;
using qoiw::PointTarget;
using qoiw::frontend::pointwise_radius;

void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

void local_estimates() {
    check(pointwise_radius({Expr("3*x"), 1.5}, 2, 4) == .5, "linear slope");
    check(pointwise_radius({Expr("3*x"), 1.5}, 2, .25) == .25, "cap");
    check(pointwise_radius({Expr("42"), 0}, 2, 4) == 4, "constant at zero tolerance");
    const PointTarget squared{Expr("x^2"), 7};
    for (double x : {-3., 3.}) check(pointwise_radius(squared, x, 10) == 1, "quadratic root");
    check(pointwise_radius({Expr("x^2"), 0}, 0, 10) == 0, "zero tolerance at a stationary point");
    const auto small = pointwise_radius({Expr("x^4"), 1e-16}, 0, 1);
    check(small > 0 && std::pow(small, 4) <= 1e-16, "unchecked higher-order remainder");

    const PointTarget log{Expr("ln(x)"), 10};
    const auto radius = pointwise_radius(log, 1, 100);
    check(radius > 0 && radius < 1 && std::abs(std::log(1 - radius)) <= 10, "logarithm domain");
    check(pointwise_radius(log, -1, 100) == 0, "non-finite value");
    check(pointwise_radius({Expr("sqrt(x)"), 1}, 0, 1) == 0, "non-finite derivatives");
    check(pointwise_radius({Expr("2"), 1}, std::numeric_limits<double>::infinity(), 1) == 0, "non-finite input");
}

void breakpoints_and_extremes() {
    PointTarget step{Expr("x >= 2 ? 1 : 0"), .5};
    const double before = pointwise_radius(step, 1.9, 10);
    check(before > .09 && 1.9 + before < 2, "jump beyond tolerance");
    check(pointwise_radius(step, 2, 10) == 0, "sample on a jump");
    step.tolerance_abs = 2;
    check(pointwise_radius(step, 1.9, 10) == 10, "jump within tolerance");
    const auto right_nan = pointwise_radius({Expr("x > 0 ? nan : 0"), 1}, -.5, 8);
    check(right_nan > .49 && right_nan <= .5, "non-finite right endpoint");

    const auto large = pointwise_radius({Expr("1e300*x^2"), 1e100}, 0, 1);
    check(large >= .99e-100 && large <= 1.001e-100, "overflow in Taylor discriminant");
    const auto tiny = pointwise_radius({Expr("1e-200*x^2"), 1e-200}, 0, 9);
    check(tiny >= .99 && tiny <= 1.001, "underflow in Taylor discriminant");
}

template <class T>
void sampled_targets() {
    using namespace qoiw::frontend;
    using Mode = Tolerance::Mode;
    const auto dtype = std::is_same_v<T, float> ? qoiw::DType::Float32 : qoiw::DType::Float64;
    const std::vector<T> field{-4, 2, 2, 12, 2, 2, 4, 2, 2, 99, 2};
    const auto bytes = field.size() * sizeof(T);
    const PointRequest spec{{{Expr("x < 0 ? 2*x : x"), {Mode::Absolute, .5}},
                             {Expr("x > 8 ? 4*x : x"), {Mode::Absolute, .5}}},
                            Tolerance{Mode::Absolute, .375}, Expr("x > 90")};
    const auto resolved = resolve_pointwise(dtype, field.data(), bytes, spec, 1);
    // Stride 3 samples indices 0, 3, 6 and 9. Different targets limit the
    // first two, the data bound limits the third, and the fourth is exact.
    const std::vector<double> radii{0, .125, .25, .375};
    for (int threads : {1, 2, 7, 16}) {
        const auto prior = estimate_point_prior(dtype, field.data(), bytes, resolved, threads, {3, {1, .5, 0, .5}});
        check(prior.stride == 3 && prior.sorted_radii == radii, "sample layout or target intersection");
        check(prior.candidates == std::vector<double>{.25, .375}, "quantile ranks or duplicates");
    }
    const auto full = estimate_point_prior(dtype, field.data(), bytes, resolved, 1);
    check(full.stride == 1 && full.sorted_radii.size() == field.size(), "default sampling on a small field");
}

void degenerate_fields() {
    using namespace qoiw::frontend;
    const auto dtype = qoiw::DType::Float64;
    const double max = std::numeric_limits<double>::max();
    for (const std::vector<double>& field : {std::vector<double>{42, 42}, {INFINITY, NAN}, {-max, max}}) {
        const auto bytes = field.size() * sizeof(double);
        const auto resolved = resolve_pointwise(dtype, field.data(), bytes, {}, 1);
        const auto prior = estimate_point_prior(dtype, field.data(), bytes, resolved, 1);
        if (field[0] == -max) {
            check(prior.candidates == std::vector<double>{max}, "unbounded field span");
        } else {
            check(prior.candidates.empty() && prior.sorted_radii == std::vector<double>{0, 0}, "zero prior");
        }
    }
    const double negative[]{-2, -1};
    const PointRequest singular{{{Expr("ln(x)"), {Tolerance::Mode::Absolute, 1}}}, {}, {}};
    const auto resolved = resolve_pointwise(dtype, negative, sizeof(negative), singular, 1);
    const auto masked = estimate_point_prior(dtype, negative, sizeof(negative), resolved, 1);
    check(masked.candidates.empty() && masked.sorted_radii == std::vector<double>{0, 0}, "non-finite target");
}

void default_quantile_ranks() {
    using namespace qoiw::frontend;
    const auto dtype = qoiw::DType::Float64;
    std::vector<double> field(800);
    for (std::size_t i = 0; i < field.size(); ++i) field[i] = i + 1;
    // 129 is an exact sample past the first mask word. 130 is exact but off-stride.
    const PointRequest spec{{{Expr("x^2"), {Tolerance::Mode::Absolute, 1}}}, {}, Expr("x == 129 or x == 130")};
    const auto bytes = field.size() * sizeof(double);
    const auto resolved = resolve_pointwise(dtype, field.data(), bytes, spec, 1);
    const auto prior = estimate_point_prior(dtype, field.data(), bytes, resolved, 7, {400, {}});
    const auto& radii = prior.sorted_radii;
    check(radii.size() == 400 && std::is_sorted(radii.begin(), radii.end()), "ordered default distribution");
    check(radii.front() == 0 && radii[1] > 0, "strided exact mask");
    std::vector<double> expected;
    for (auto rank : {1, 2, 4, 8, 20, 40, 80, 200, 399}) expected.push_back(radii[rank]);
    check(prior.candidates == expected, "default candidate quantiles");
}

template <class T>
void block_distribution() {
    using namespace qoiw::frontend;
    using Mode = Tolerance::Mode;
    const T field[]{-1, 2, 2, 2, 4, 99, std::numeric_limits<T>::infinity(), 9};
    const auto dtype = sizeof(T) == 4 ? qoiw::DType::Float32 : qoiw::DType::Float64;
    const PointRequest spec{{{Expr("sqrt(x)"), {Mode::Absolute, 4}}, {Expr("2*x"), {Mode::Relative, .0125}}},
                            {}, Expr("x == 99")};
    const auto resolved = resolve_pointwise(dtype, field, sizeof(field), spec, 1);
    // The constant first block uses the full input's resolved tolerance (0.25 for 2*x).
    const BlockSample sample{{{3}, {1, 4}, false}, std::vector<std::vector<T>>{{2, 2, 2}, {4, 99, field[6]}}};
    const std::vector<double> expected{0, 0, .125, .125, .125, .125};
    for (int threads : {1, 4}) check(estimate_block_radii(sample, resolved, threads) == expected, "block prior");
    const auto whole = sample_blocks(dtype, field, sizeof(field), {8}, 1);
    check(estimate_block_radii(whole, resolved, 2) ==
              estimate_point_prior(dtype, field, sizeof(field), resolved, 1).sorted_radii, "F1 and block radii differ");
}

void invalid_inputs() {
    using namespace qoiw::frontend;
    const auto dtype = qoiw::DType::Float64;
    const double field[]{1, 2};
    auto resolved = resolve_pointwise(dtype, field, sizeof(field), {}, 1);
    auto rejects = [&](const void* ptr, std::size_t bytes, int threads, const PriorOptions& options) {
        try {
            estimate_point_prior(dtype, ptr, bytes, resolved, threads, options);
        } catch (const std::invalid_argument&) {
            return;
        }
        throw std::runtime_error("invalid prior arguments accepted");
    };
    rejects(nullptr, sizeof(field), 1, {});
    rejects(field, 0, 1, {});
    rejects(field, 3, 1, {});
    rejects(field, sizeof(field), -1, {});
    for (double q : {-1., 1.01, std::numeric_limits<double>::quiet_NaN()})
        rejects(field, sizeof(field), 1, {1, {q}});
    resolved.exact_mask.clear();
    rejects(field, sizeof(field), 1, {});
}
}  // namespace

int main() {
    try {
        local_estimates();
        breakpoints_and_extremes();
        sampled_targets<float>();
        sampled_targets<double>();
        degenerate_fields();
        default_quantile_ranks();
        block_distribution<float>();
        block_distribution<double>();
        invalid_inputs();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
