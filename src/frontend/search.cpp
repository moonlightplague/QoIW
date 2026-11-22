// Geometric refinement adapted from QPET. Copyright (c) 2026, Jinyang Liu. See LICENSES/QPET.txt.
#include "frontend/search.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace qoiw::frontend {
namespace {
double geometric_midpoint(double low, double high) {
    if (low == high) return low;
    const double product = low * high;
    if (std::isnormal(product)) return std::sqrt(product);
    // The midpoint is representable even when the product overflows or underflows.
    return std::clamp(std::sqrt(low) * std::sqrt(high), low, high);
}

template <class Cost>
double refine(const std::vector<double>& candidates, int rounds, Cost cost) {
    std::size_t best = 0;
    double best_cost = cost(candidates.front());
    for (std::size_t i = 1; i < candidates.size(); ++i) {
        const double bytes = cost(candidates[i]);
        if (bytes < best_cost) {
            best = i;
            best_cost = bytes;
        }
    }
    double bound = candidates[best];
    double low = best > 0 ? candidates[best - 1] : bound;
    double high = best + 1 < candidates.size() ? candidates[best + 1] : bound;
    if (low == 0) low = bound;  // Zero can be measured but has no logarithmic midpoint.
    for (int i = 0; i < rounds && low > 0 && high / low > 1.05; ++i) {
        const double left = geometric_midpoint(low, bound);
        const double right = geometric_midpoint(bound, high);
        const double left_cost = cost(left);
        const double right_cost = cost(right);
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
    return best_cost;
}
}  // namespace

BoundSelection search_pointwise_bounds(std::vector<double> candidates, const MeasureBound& measure,
                                      const SearchOptions& options, std::optional<double> data_bound, BoundKind kind) {
    if (options.refine_rounds < 0 || !std::isfinite(options.tie_bytes) || options.tie_bytes < 0)
        throw std::invalid_argument("invalid bound search options");
    if (data_bound && (!std::isfinite(*data_bound) || *data_bound < 0))
        throw std::invalid_argument("invalid resolved data bound");
    candidates.erase(std::remove(candidates.begin(), candidates.end(), 0.), candidates.end());
    for (double& bound : candidates) {
        if (!std::isfinite(bound) || bound < 0) throw std::invalid_argument("invalid candidate bound");
        if (kind == BoundKind::StrictAbs && data_bound) bound = std::min(bound, *data_bound);
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    if (candidates.empty()) throw std::invalid_argument("bound search needs a positive prior candidate");

    BoundSelection result{};
    struct TrialIndex {
        std::size_t index;
        bool eligible;
    };
    std::map<double, TrialIndex> trials;
    auto trial = [&](double bound, bool eligible) -> const CandidateStats& {
        const auto [it, inserted] = trials.emplace(bound, TrialIndex{result.trace.size(), eligible});
        if (inserted) {
            auto stats = measure(bound);
            if (!std::isfinite(stats.bytes_per_value) || stats.bytes_per_value < 0)
                throw std::invalid_argument("candidate cost must be finite and nonnegative");
            stats.bound = bound;
            result.trace.push_back(stats);
        } else if (eligible) {
            it->second.eligible = true;
        }
        return result.trace[it->second.index];
    };

    const bool clean_cap = data_bound && trial(*data_bound, false).restored == 0;
    if (clean_cap) {
        const auto pos = std::lower_bound(candidates.begin(), candidates.end(), *data_bound);
        if (pos == candidates.end() || *pos != *data_bound) candidates.insert(pos, *data_bound);
    }
    const auto cost = [&](double bound) { return trial(bound, true).bytes_per_value; };
    const double cheapest = refine(candidates, options.refine_rounds, cost);
    const double cutoff = cheapest + cheapest * options.tie_bytes;
    if (clean_cap && cost(*data_bound) <= cutoff) {
        result.bound = *data_bound;
    } else {
        // The map is ordered by bound, including all measured midpoints.
        for (const auto& [bound, location] : trials) {
            if (location.eligible && result.trace[location.index].bytes_per_value <= cutoff) {
                result.bound = bound;
                break;
            }
        }
    }
    return result;
}

}  // namespace qoiw::frontend
