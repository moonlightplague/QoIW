// Block cost model adapted from QPET. Copyright (c) 2026, Jinyang Liu. See LICENSES/QPET.txt.
#include "frontend/measure.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

#include "backend/pointwise.hpp"
#include "common/parallel_for.hpp"
#include "common/shape.hpp"

namespace qoiw::frontend {
namespace {
double share_below(const std::vector<double>& sorted, double bound) {
    const auto below = std::lower_bound(sorted.begin(), sorted.end(), bound) - sorted.begin();
    return static_cast<double>(below) / sorted.size();
}

std::size_t payload_bytes(const CoreOutput& output) {
    return output.stream.size() + output.decode_params.size();
}

// Sum of squares in units of 2^(2*exponent). Even a single binary64
// difference can exceed the finite range, or underflow when squared.
struct ErrorSquares {
    double sum = 0;
    int exponent = 0;

    void merge(ErrorSquares other) {
        if (other.sum == 0) return;
        if (sum == 0 || other.exponent > exponent) std::swap(*this, other);
        sum += std::ldexp(other.sum, 2 * (other.exponent - exponent));
    }

    void add(double a, double b) {
        if (a == b || !std::isfinite(a)) return;
        double error = a - b;
        const bool overflow = !std::isfinite(error);
        if (overflow) error = .5 * a - .5 * b;
        int power;
        const double fraction = std::frexp(error, &power);
        merge({fraction * fraction, power + overflow});
    }

    std::optional<double> psnr(const std::optional<Range>& range, std::size_t count) const {
        if (!count || !range) return {};
        if (sum == 0) return std::numeric_limits<double>::infinity();
        if (range->min == range->max) return -std::numeric_limits<double>::infinity();
        const double span = range->max - range->min;
        const double log_peak = std::isfinite(span) ? std::log2(span) : 1 + std::log2(range->scaled_width(.5));
        const double log_rmse = exponent + .5 * std::log2(sum / count);
        return 20 * std::log10(2.) * (log_peak - log_rmse);
    }
};

struct BlockCounts {
    std::size_t core, lossless, restored;
    ErrorSquares error;
};

template <class T>
void measure_blocks(const std::vector<std::vector<T>>& blocks, const std::vector<std::uint64_t>& dims, DType dtype,
                    const Core& core, const CoreParams& params, const CoreTraits& traits,
                    const PointResolution& resolved, std::size_t quality_values, int threads,
                    CandidateMeasurement& result) {
    const auto values = blocks.front().size();
    const auto bytes = values * sizeof(T);
    std::vector<BlockCounts> counts(blocks.size());
    std::vector<std::vector<T>> decoded(blocks.size());
    common::parallel_for(blocks.size(), traits.thread_safe ? threads : 1, [&](std::size_t k) {
        const auto* original = blocks[k].data();
        const auto payload = core.compress(original, dtype, dims, params);
        auto& raw = decoded[k];
        raw.resize(values);
        core.decompress({payload.stream.data(), payload.stream.size()},
                        {payload.decode_params.data(), payload.decode_params.size()}, dtype, dims, 1, raw.data(), bytes);
        auto verified = raw;
        const auto patch = backend::correct_pointwise(dtype, original, verified.data(), bytes, resolved.request, 1);
        counts[k] = {payload_bytes(payload), patch.lossless.size(), patch.restored, {}};
        // Finite exact values already have zero error after correction.
        for (std::size_t i = 0; i < values; ++i) counts[k].error.add(original[i], verified[i]);
    });

    std::size_t overhead = 0;
    if (traits.fixed_call_overhead && blocks.size() > 1) {
        // QPET measures the per-call baseline on a constant block of the
        // same shape and bound. Core adapters require nonempty dimensions.
        const T first = blocks.front().front();
        const std::vector<T> flat(values, std::isfinite(first) ? first : T{});
        overhead = payload_bytes(core.compress(flat.data(), dtype, dims, params));
    }
    ErrorSquares error;
    for (const auto& block : counts) {
        result.core_bytes += block.core;
        result.overhead_bytes += std::min(overhead, block.core);
        result.lossless_bytes += block.lossless;
        result.restored += block.restored;
        error.merge(block.error);
    }
    result.sample_psnr_db = error.psnr(resolved.data_range, quality_values);
    result.sample_values = values * blocks.size();
    result.decoded = std::move(decoded);
}
}  // namespace

PointMeasurer::PointMeasurer(const Core& core, const PointResolution& resolved, const PointSearch& search,
                             CoreParams params, double rare_outlier_share)
    : core_(core), resolved_(resolved), search_(search), params_(std::move(params)),
      threads_(params_.threads), rare_outlier_share_(rare_outlier_share) {
    if (threads_ < 0 || !(rare_outlier_share >= 0 && rare_outlier_share <= 1) || search.prior.sorted_radii.empty())
        throw std::invalid_argument("invalid candidate measurement options or prior");
    if (!core.supports(search.sample.dtype(), search.sample.layout.dims))
        throw std::invalid_argument("core does not support the sample block shape or dtype");
    traits_ = core.traits(search.sample.dtype(), search.sample.layout.dims.size());
    params_.threads = 1;
    if (search.sample.layout.whole_input_sampled) search.sample.values_per_block();
    else block_radii_ = estimate_block_radii(search.sample, resolved, threads_);
    std::visit([&](const auto& blocks) {
        std::vector<std::size_t> kept(blocks.size());
        common::parallel_for(blocks.size(), threads_, [&](std::size_t k) {
            kept[k] = std::count_if(blocks[k].begin(), blocks[k].end(), [&](double x) {
                return std::isfinite(x) && !(resolved.request.exact && resolved.request.exact->eval(x) != 0);
            });
        });
        quality_values_ = std::accumulate(kept.begin(), kept.end(), std::size_t{0});
    }, search.sample.blocks);
}

const CandidateMeasurement& PointMeasurer::measure(double bound) {
    if (!(bound >= 0) || !std::isfinite(bound)) throw std::invalid_argument("invalid candidate bound");
    const auto found = cache_.find(bound);
    if (found != cache_.end()) return found->second;

    CandidateMeasurement result;
    result.bound = bound;
    params_.bound = bound;
    const auto& sample = search_.sample;
    std::visit([&](const auto& blocks) {
        measure_blocks(blocks, sample.layout.dims, sample.dtype(), core_, params_, traits_, resolved_,
                       quality_values_, threads_, result);
    }, sample.blocks);

    result.input_sensitive_share = share_below(search_.prior.sorted_radii, bound);
    result.sample_sensitive_share = sample.layout.whole_input_sampled ? result.input_sensitive_share
                                                                     : share_below(block_radii_, bound);
    if (result.input_sensitive_share > 20 * result.sample_sensitive_share) {
        result.rare_outlier_fallback = true;
        result.correction_bytes_per_value = rare_outlier_share_ * result.input_sensitive_share *
                                            (common::element_bytes(sample.dtype()) + 1);
    } else {
        if (result.sample_sensitive_share > 0)
            result.correction_weight = result.input_sensitive_share / result.sample_sensitive_share;
        result.correction_bytes_per_value = result.correction_weight * result.lossless_bytes / result.sample_values;
    }
    result.bytes_per_value = result.core_bytes_per_value() + result.correction_bytes_per_value;
    return cache_.emplace(bound, std::move(result)).first->second;
}

}  // namespace qoiw::frontend
