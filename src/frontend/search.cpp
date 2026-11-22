// Geometric refinement adapted from QPET. Copyright (c) 2026, Jinyang Liu. See LICENSES/QPET.txt.
#include "frontend/search.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace qoiw::frontend {
namespace {
double geometric_midpoint(double low, double high) {
    const double product = low * high;
    if (std::isnormal(product)) return std::sqrt(product);
    // The midpoint is representable even when the product overflows or underflows.
    return std::clamp(std::sqrt(low) * std::sqrt(high), low, high);
}
}  // namespace

BoundSelection search_pointwise_bounds(std::vector<double> candidates, const MeasureBound& measure,
                                      const SearchOptions& options) {
    if (options.refine_rounds < 0) throw std::invalid_argument("negative bound refinement count");
    for (double bound : candidates) {
        if (!std::isfinite(bound) || bound < 0) throw std::invalid_argument("invalid candidate bound");
    }
    candidates.erase(std::remove(candidates.begin(), candidates.end(), 0.), candidates.end());
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    if (candidates.empty()) throw std::invalid_argument("bound search needs a positive candidate");

    BoundSelection result{};
    std::map<double, std::size_t> indices;
    auto trial = [&](double bound) -> const CandidateStats& {
        const auto [it, inserted] = indices.emplace(bound, result.trace.size());
        if (inserted) {
            auto stats = measure(bound);
            if (!std::isfinite(stats.bytes_per_value) || stats.bytes_per_value < 0)
                throw std::invalid_argument("candidate cost must be finite and nonnegative");
            stats.bound = bound;
            result.trace.push_back(stats);
        }
        return result.trace[it->second];
    };

    std::size_t best = 0;
    double best_cost = trial(candidates.front()).bytes_per_value;
    for (std::size_t i = 1; i < candidates.size(); ++i) {
        const double cost = trial(candidates[i]).bytes_per_value;
        if (cost < best_cost) {
            best = i;
            best_cost = cost;
        }
    }
    double bound = candidates[best];
    double low = best > 0 ? candidates[best - 1] : bound;
    double high = best + 1 < candidates.size() ? candidates[best + 1] : bound;
    for (int i = 0; i < options.refine_rounds && high / low > 1.05; ++i) {
        const double left = geometric_midpoint(low, bound);
        const double right = geometric_midpoint(bound, high);
        const double left_cost = trial(left).bytes_per_value;
        const double right_cost = trial(right).bytes_per_value;
        if (right_cost < best_cost && right_cost < left_cost) {
            low = bound;
            bound = right;
            best_cost = right_cost;
        } else if (left_cost < best_cost) {
            high = bound;
            bound = left;
            best_cost = left_cost;
        } else {
            if (left == low && right == high) break;
            low = left;
            high = right;
        }
    }
    result.bound = bound;
    return result;
}

}  // namespace qoiw::frontend
