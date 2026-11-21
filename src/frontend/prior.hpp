#pragma once

#include "frontend/resolve.hpp"

namespace qoiw::frontend {

// F1 estimate at one storage value. The cap and resolved tolerance must be
// finite and nonnegative. The correction pass checks actual reconstructions.
double pointwise_radius(const PointTarget& target, double x, double cap);

struct PriorOptions {
    // Zero or an empty list selects the corresponding default from PLAN.md.
    std::size_t point_samples = 0;
    std::vector<double> quantiles;
};

struct PointPrior {
    std::size_t stride;
    std::vector<double> sorted_radii;  // Includes zeros for exact samples.
    std::vector<double> candidates;    // Sorted positive quantiles, or the first positive radius if all miss.
};

// Sample the same original field passed to F0. The result seeds the search.
PointPrior estimate_point_prior(DType dtype, const void* original, std::size_t array_bytes,
                                const PointResolution& resolved, int threads, const PriorOptions& options = {});

}  // namespace qoiw::frontend
