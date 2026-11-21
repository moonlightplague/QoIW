#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "frontend/prepare.hpp"

namespace {
using namespace qoiw::frontend;
using qoiw::CoreTraits;
using qoiw::DType;
using qoiw::Expr;
using Mode = Tolerance::Mode;
constexpr CoreTraits strict{qoiw::BoundKind::StrictAbs};
constexpr CoreTraits nominal{qoiw::BoundKind::NominalAbs};

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

PointPreparation prepare(const std::vector<double>& field, const PointRequest& request, CoreTraits core,
                         std::optional<double> fixed = {}, const PriorOptions& options = {}) {
    return prepare_pointwise(DType::Float64, field.data(), field.size() * sizeof(double), request, core, 2,
                             fixed, options);
}

void expect_bound(const PointPreparation& prep, double bound, ShortcutReason reason) {
    const auto* shortcut = std::get_if<PointShortcut>(&prep.decision);
    check(shortcut && shortcut->bound == bound && shortcut->reason == reason, "unexpected bound or shortcut");
}

void fixed_bound() {
    const PointRequest request{{{Expr("x^2"), {Mode::Relative, .25}}}, Tolerance{Mode::Relative, .25}, {}};
    for (double bound : {0., .5, 2.}) {
        // The invalid quantile would throw if F1 ran. F0 must still resolve
        // the original request for verification, even above its data bound.
        const auto prep = prepare({2, 6}, request, strict, bound, {1, {2}});
        expect_bound(prep, bound, ShortcutReason::FixedBound);
        check(prep.resolved.request.data_bound_abs == 1 && prep.resolved.request.targets[0].tolerance_abs == 8,
              "fixed bound changed resolved tolerances");
    }
    for (double invalid : {-1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        try {
            prepare({2, 6}, request, nominal, invalid);
        } catch (const std::invalid_argument&) {
            continue;
        }
        throw std::runtime_error("invalid fixed bound accepted");
    }
}

void zero_radii() {
    PointRequest exact_values{{{Expr("x"), {Mode::Absolute, 0}}}, {}, {}};
    for (const auto core : {strict, nominal}) {
        expect_bound(prepare({-2, 2}, exact_values, core), 4e-9, ShortcutReason::ZeroRadii);
        expect_bound(prepare({5, 5}, {}, core), 1., ShortcutReason::ZeroRadii);
        expect_bound(prepare({NAN, INFINITY}, {}, core), 1., ShortcutReason::ZeroRadii);
    }
    const double max = std::numeric_limits<double>::max();
    const double tiny = std::numeric_limits<double>::denorm_min();
    expect_bound(prepare({-max, max}, exact_values, strict), 2 * (max * 1e-9), ShortcutReason::ZeroRadii);
    expect_bound(prepare({0, tiny}, exact_values, strict), tiny, ShortcutReason::ZeroRadii);
    for (double cap : {0., 1e-10}) {
        exact_values.data_bound = Tolerance{Mode::Absolute, cap};
        expect_bound(prepare({-2, 2}, exact_values, strict), cap, ShortcutReason::ZeroRadii);
        expect_bound(prepare({3, 3}, exact_values, strict), cap, ShortcutReason::ZeroRadii);
    }
}

void single_candidate() {
    const std::vector<double> field{-2, -1, 1, 2};
    PointRequest request{{{Expr("3*x+1"), {Mode::Absolute, 1.5}}, {Expr("-2*x"), {Mode::Absolute, .25}}}, {}, {}};
    expect_bound(prepare(field, request, strict), .125, ShortcutReason::SingleCandidate);
    const auto nominal_prep = prepare(field, request, nominal);
    check(std::get<PointPrior>(nominal_prep.decision).candidates == std::vector<double>{.125},
          "nominal core missed measured tuning");

    request.targets.clear();
    request.data_bound = Tolerance{Mode::Absolute, .125};
    expect_bound(prepare(field, request, strict), .125, ShortcutReason::SingleCandidate);
    request.exact = Expr("x < 0");
    check(std::holds_alternative<PointPrior>(prepare(field, request, strict).decision),
          "exact predicate treated as an affine target");
    check(std::holds_alternative<PointPrior>(prepare(field, {}, strict).decision),
          "empty request treated as an affine target");
}

void nonlinear_candidates() {
    const std::vector<double> field{-2, -1, 1, 2};
    PointRequest request{{{Expr("x"), {Mode::Absolute, 1}}, {Expr("sqrt(x)"), {Mode::Absolute, .25}}}, {}, {}};
    const auto full = prepare(field, request, strict);
    const auto& prior = std::get<PointPrior>(full.decision);
    check(prior.candidates.size() == 2 && prior.sorted_radii[1] == 0, "nonlinear candidate distribution");
    for (double q : {0., 1.}) {
        const auto prep = prepare(field, request, strict, {}, {0, {q}});
        const auto& search = std::get<PointPrior>(prep.decision);
        const double expected = q == 0 ? prior.candidates.front() : prior.candidates.back();
        check(search.sorted_radii == prior.sorted_radii && search.candidates == std::vector<double>{expected},
              "nonlinear singleton or zero-only quantiles skipped measured tuning");
    }
}
}  // namespace

int main() {
    try {
        fixed_bound();
        zero_radii();
        single_candidate();
        nonlinear_candidates();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
