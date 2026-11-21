#pragma once

#include <variant>

#include "frontend/prior.hpp"
#include "qoiw/core.hpp"

namespace qoiw::frontend {

enum class ShortcutReason { FixedBound, ZeroRadii, SingleCandidate };

struct PointShortcut {
    double bound;
    ShortcutReason reason;
};

struct PointPreparation {
    PointResolution resolved;
    // A chosen bound is ready for full-field compression and verification.
    // Otherwise the sampled prior supplies F3-F5 with candidates and weights.
    std::variant<PointShortcut, PointPrior> decision;
};

PointPreparation prepare_pointwise(DType dtype, const void* original, std::size_t array_bytes,
                                   const PointRequest& request, CoreTraits core, int threads,
                                   std::optional<double> fixed_bound = {}, const PriorOptions& options = {});

}  // namespace qoiw::frontend
