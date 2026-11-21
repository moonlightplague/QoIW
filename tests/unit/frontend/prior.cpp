#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "frontend/prior.hpp"

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
}  // namespace

int main() {
    try {
        local_estimates();
        breakpoints_and_extremes();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
