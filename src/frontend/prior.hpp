#pragma once

#include "frontend/resolve.hpp"
#include "qoiw/options.hpp"

namespace qoiw::frontend {

struct BlockSample;

// F1 estimate at one storage value. The cap and resolved tolerance must be
// finite and nonnegative. The correction pass checks actual reconstructions.
double pointwise_radius(const PointTarget& target, double x, double cap);

using qoiw::PriorOptions;

struct PointPrior {
    std::size_t stride;
    std::vector<double> sorted_radii;  // Includes zeros for exact samples.
    std::vector<double> candidates;    // Sorted positive quantiles, or the first positive radius if all miss.
};

// Sample the same original field passed to F0. The result seeds the search.
PointPrior estimate_point_prior(DType dtype, const void* original, std::size_t array_bytes,
                                const PointResolution& resolved, int threads, const PriorOptions& options = {});

// Sorted radii from regularly spaced points in each F3 block, using the F0
// field range, absolute tolerances, and exact predicate from the full input.
std::vector<double> estimate_block_radii(const BlockSample& sample, const PointResolution& resolved, int threads);

}  // namespace qoiw::frontend
