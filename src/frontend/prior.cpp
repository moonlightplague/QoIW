// Adapted from QPET. Copyright (c) 2026, Jinyang Liu. See LICENSES/QPET.txt.
#include "frontend/prior.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace qoiw::frontend {
namespace {
constexpr double infinity = std::numeric_limits<double>::infinity();

// Positive root of slope*e + curvature*e^2/2 = tolerance.
double taylor_radius(double slope, double curvature, double tolerance) {
    if (curvature == 0) return slope == 0 ? infinity : tolerance / slope;
    if (tolerance == 0) return 0;
    const double discriminant = slope * slope + 2 * curvature * tolerance;
    const double denominator = slope + std::sqrt(discriminant);
    const double numerator = 2 * tolerance;
    if (std::isnormal(discriminant) && std::isfinite(denominator) && std::isfinite(numerator))
        return numerator / denominator;

    // Scale the denominator by a power of two if either square or product is
    // outside the normal range. Apply the final exponent only after division.
    int slope_exp, curvature_exp, tol_exp;
    std::frexp(slope, &slope_exp);
    const double c = std::frexp(curvature, &curvature_exp);
    const double t = std::frexp(tolerance, &tol_exp);
    const int root_exp = (curvature_exp + tol_exp) / 2 + 1;
    const int scale = slope == 0 ? root_exp : std::max(slope_exp, root_exp);
    const double a = std::ldexp(slope, -scale);
    const double quadratic = std::ldexp(c * t, 1 + curvature_exp + tol_exp - 2 * scale);
    return std::ldexp(2 * t / (a + std::sqrt(a * a + quadratic)), tol_exp - scale);
}
}  // namespace

double pointwise_radius(const PointTarget& target, double x, double cap) {
    const double tau = target.tolerance_abs;
    if (!std::isfinite(cap) || cap < 0 || !std::isfinite(tau) || tau < 0)
        throw std::invalid_argument("invalid point-wise prior cap or tolerance");
    if (cap == 0 || !std::isfinite(x)) return 0;
    const auto& expr = target.expression;
    const auto jet = expr.jet(x);
    if (!std::isfinite(jet.v) || !std::isfinite(jet.d1) || !std::isfinite(jet.d2)) return 0;
    double radius = std::min({cap, jet.dist, taylor_radius(std::abs(jet.d1), std::abs(jet.d2), tau)});

    auto endpoint_error = [&](double step) {
        const double lo = x - step, hi = x + step;
        if (!std::isfinite(lo) || !std::isfinite(hi)) return infinity;
        const double left = std::abs(expr.eval(lo) - jet.v), right = std::abs(expr.eval(hi) - jet.v);
        if (!std::isfinite(left) || !std::isfinite(right)) return infinity;
        return std::max(left, right);
    };
    if (jet.brk < radius) {
        // Probe just across the breakpoint, allowing for rounding in x +/- e.
        const double margin = std::max(jet.brk, std::abs(x)) * 1e-9 + std::numeric_limits<double>::min();
        const double across = jet.brk + margin;
        if (endpoint_error(across) > tau) {
            radius = std::max(0., jet.brk - margin);
        } else {
            for (double probe = 2 * across; probe < radius; probe *= 2) {
                if (endpoint_error(probe) > tau) {
                    radius = probe / 2;
                    break;
                }
            }
        }
    }

    for (int tries = 0; radius > 0; ++tries) {
        const double error = endpoint_error(radius);
        if (error <= tau) return radius;
        // QPET shrinks proportionally for small overshoots. If its eight
        // refinements are exhausted, keep halving until both endpoints pass.
        const double factor = tries < 8 && std::isfinite(error) ? std::max(.5, .999 * tau / error) : .5;
        const double next = radius * factor;
        radius = next < radius ? next : radius * .5;
    }
    return 0;
}

}  // namespace qoiw::frontend
