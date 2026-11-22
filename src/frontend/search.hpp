#pragma once

#include <functional>

#include "frontend/measure.hpp"

namespace qoiw::frontend {

struct SearchOptions {
    int refine_rounds = 4;
};

struct BoundSelection {
    double bound;
    std::vector<CandidateStats> trace;  // Unique trials in measurement order.
};

using MeasureBound = std::function<CandidateStats(double)>;

// F5 for a point-wise target and a continuous absolute-error knob.
BoundSelection search_pointwise_bounds(std::vector<double> candidates, const MeasureBound& measure,
                                      const SearchOptions& options = {});

}  // namespace qoiw::frontend
