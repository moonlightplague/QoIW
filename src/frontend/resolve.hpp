#pragma once

#include "qoi/pointwise.hpp"
#include "qoiw/types.hpp"

namespace qoiw::frontend {

struct Tolerance {
    enum class Mode { Absolute, Relative };
    Mode mode;
    double value;
};

struct RequestedPointTarget {
    Expr expression;
    Tolerance tolerance;
};

struct PointRequest {
    std::vector<RequestedPointTarget> targets;
    std::optional<Tolerance> data_bound;
    std::optional<Expr> exact;
};

struct Range {
    double min, max;
    // Scale the width even when subtracting opposite finite extremes overflows.
    double scaled_width(double scale) const;
};

struct PointResolution {
    ResolvedPointRequest request;
    std::optional<Range> data_range;
    std::vector<std::optional<Range>> target_ranges;
    std::size_t nonfinite_inputs = 0;
    // Bit i % 64 of word i / 64 marks an original that must be preserved
    // exactly: non-finite input, true exact condition, or any non-finite QoI.
    std::vector<std::uint64_t> exact_mask;
};

// F0 for a single field. An absent data bound disables that constraint.
// Empty or constant finite ranges use the specified relative value as absolute.
PointResolution resolve_pointwise(DType dtype, const void* original, std::size_t array_bytes,
                                  const PointRequest& request, int threads);

}  // namespace qoiw::frontend
