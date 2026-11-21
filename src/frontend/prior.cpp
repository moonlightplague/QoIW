// Adapted from QPET. Copyright (c) 2026, Jinyang Liu. See LICENSES/QPET.txt.
#include "frontend/prior.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "common/parallel_for.hpp"
#include "common/shape.hpp"

namespace qoiw::frontend {
namespace {
constexpr double infinity = std::numeric_limits<double>::infinity();
constexpr std::size_t default_samples = 1u << 20;

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

PointPrior estimate_point_prior(DType dtype, const void* original, std::size_t array_bytes,
                                const PointResolution& resolved, int threads, const PriorOptions& options) {
    const auto width = common::element_bytes(dtype);
    if (!width || !original || !array_bytes || array_bytes % width || threads < 0)
        throw std::invalid_argument("invalid field or thread count for point-wise prior");
    const auto count = array_bytes / width;
    if (resolved.exact_mask.size() != 1 + (count - 1) / 64)
        throw std::invalid_argument("exact mask does not cover the field");
    static const std::vector<double> default_quantiles{0, .0025, .005, .01, .02, .05, .1, .2, .5, 1};
    const auto& quantiles = options.quantiles.empty() ? default_quantiles : options.quantiles;
    for (double q : quantiles) {
        if (!(q >= 0 && q <= 1)) throw std::invalid_argument("prior quantile outside [0,1]");
    }

    // Only a finite bound can be passed to a core, even if subtracting the
    // extreme finite values of a field overflows.
    const auto& range = resolved.data_range;
    const double span = range ? range->max - range->min : 0.;
    const double cap = std::min(span, resolved.request.data_bound_abs.value_or(std::numeric_limits<double>::max()));
    const auto samples = options.point_samples ? options.point_samples : default_samples;
    PointPrior result;
    result.stride = std::max<std::size_t>(1, count / samples);
    result.sorted_radii.resize(1 + (count - 1) / result.stride);
    if (cap > 0) {
        common::parallel_for(result.sorted_radii.size(), threads, [&](std::size_t i) {
            const auto index = i * result.stride;
            if ((resolved.exact_mask[index / 64] >> (index % 64)) & 1u) return;
            const double x = dtype == DType::Float32 ? static_cast<const float*>(original)[index]
                                                     : static_cast<const double*>(original)[index];
            double radius = cap;
            for (const auto& target : resolved.request.targets) {
                radius = std::min(radius, pointwise_radius(target, x, cap));
                if (radius == 0) break;
            }
            result.sorted_radii[i] = radius;
        });
        std::sort(result.sorted_radii.begin(), result.sorted_radii.end());
    }

    const auto n = result.sorted_radii.size();
    for (double q : quantiles) {
        const auto index = std::min(n - 1, static_cast<std::size_t>(q * n));
        const double radius = result.sorted_radii[index];
        if (radius > 0) result.candidates.push_back(radius);
    }
    auto& candidates = result.candidates;
    // Custom quantiles can all land on exact samples. Keep a positive seed
    // for measurement if any exists in the sampled distribution.
    if (candidates.empty() && result.sorted_radii.back() > 0)
        candidates.push_back(*std::upper_bound(result.sorted_radii.begin(), result.sorted_radii.end(), 0.));
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return result;
}

}  // namespace qoiw::frontend
