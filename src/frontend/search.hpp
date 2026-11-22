#pragma once

#include <functional>
#include <optional>

#include "frontend/measure.hpp"

namespace qoiw::frontend {

using qoiw::SearchOptions;

struct BoundSelection {
    double bound;
    std::vector<CandidateStats> trace;  // Unique trials in measurement order.
};

using MeasureBound = std::function<CandidateStats(double)>;

// F5 for a point-wise target and a continuous absolute-error knob.
BoundSelection search_pointwise_bounds(std::vector<double> candidates, const MeasureBound& measure,
                                      const SearchOptions& options = {}, std::optional<double> data_bound = {},
                                      BoundKind kind = BoundKind::StrictAbs);

// Finish F5 for a preparation made with this core's traits. Shortcuts have
// an empty trace. Measured searches retain scalar statistics for F6.
BoundSelection select_pointwise_bound(const Core& core, const PointPreparation& prepared, CoreParams params = {},
                                      const SearchOptions& options = {}, double rare_outlier_share = .1);

}  // namespace qoiw::frontend
