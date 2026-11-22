#pragma once

#include <variant>

#include "frontend/prior.hpp"
#include "frontend/sample.hpp"
#include "qoiw/core.hpp"
#include "qoiw/report.hpp"

namespace qoiw::frontend {

using qoiw::ShortcutReason;

struct PointShortcut {
    double bound;
    ShortcutReason reason;
};

struct PointSearch {
    PointPrior prior;
    BlockSample sample;
};

struct PointPreparation {
    PointResolution resolved;
    // A chosen bound is ready for full-field compression and verification.
    // Otherwise F4 gets the full-field prior and the original sample blocks.
    std::variant<PointShortcut, PointSearch> decision;
};

PointPreparation prepare_pointwise(DType dtype, const void* original, std::size_t array_bytes,
                                   const std::vector<std::uint64_t>& dims, const PointRequest& request,
                                   CoreTraits core, int threads, std::optional<double> fixed_bound = {},
                                   const PriorOptions& prior_options = {}, const BlockSamplingOptions& sampling = {});

}  // namespace qoiw::frontend
