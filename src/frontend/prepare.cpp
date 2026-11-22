// Zero-radius fallback adapted from QPET. Copyright (c) 2026, Jinyang Liu. See LICENSES/QPET.txt.
#include "frontend/prepare.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "common/shape.hpp"

namespace qoiw::frontend {
namespace {
double zero_radius_bound(const PointResolution& resolved) {
    const auto& range = resolved.data_range;
    double bound = 1.;
    if (range && range->max > range->min)
        bound = std::max(std::numeric_limits<double>::denorm_min(), range->scaled_width(1e-9));
    return std::min(bound, resolved.request.data_bound_abs.value_or(bound));
}

bool affine_request(const ResolvedPointRequest& request) {
    if (request.exact || (request.targets.empty() && !request.data_bound_abs)) return false;
    return std::all_of(request.targets.begin(), request.targets.end(),
                       [](const PointTarget& target) { return target.expression.is_linear(); });
}
}  // namespace

PointPreparation prepare_pointwise(DType dtype, const void* original, std::size_t array_bytes,
                                   const std::vector<std::uint64_t>& dims, const PointRequest& request,
                                   CoreTraits core, int threads, std::optional<double> fixed_bound,
                                   const PriorOptions& prior_options, const BlockSamplingOptions& sampling) {
    if (!array_bytes || array_bytes != common::array_bytes(dtype, dims))
        throw std::invalid_argument("point-wise field size disagrees with shape");
    if (fixed_bound && (!std::isfinite(*fixed_bound) || *fixed_bound < 0))
        throw std::invalid_argument("fixed compressor bound must be finite and nonnegative");
    auto resolved = resolve_pointwise(dtype, original, array_bytes, request, threads);
    if (fixed_bound) return {std::move(resolved), PointShortcut{*fixed_bound, ShortcutReason::FixedBound}};

    auto prior = estimate_point_prior(dtype, original, array_bytes, resolved, threads, prior_options);
    if (prior.sorted_radii.back() == 0) {
        const auto bound = zero_radius_bound(resolved);
        return {std::move(resolved), PointShortcut{bound, ShortcutReason::ZeroRadii}};
    }
    if (prior.candidates.size() == 1 && core.bound_kind == BoundKind::StrictAbs && affine_request(resolved.request))
        return {std::move(resolved), PointShortcut{prior.candidates.front(), ShortcutReason::SingleCandidate}};

    auto sample = sample_blocks(dtype, original, array_bytes, dims, threads, sampling);
    return {std::move(resolved), PointSearch{std::move(prior), std::move(sample)}};
}

}  // namespace qoiw::frontend
