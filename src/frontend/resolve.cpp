#include "frontend/resolve.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include "common/parallel_for.hpp"
#include "common/shape.hpp"

namespace qoiw::frontend {
namespace {
constexpr std::size_t kBatch = 4096;
constexpr std::size_t kWordBits = 64;
static_assert(kBatch % kWordBits == 0);  // Workers own disjoint mask words.

struct ChunkRanges {
    std::optional<Range> data;
    std::vector<std::optional<Range>> targets;
    std::size_t nonfinite_inputs = 0;
};

void include(std::optional<Range>& range, double low, double high) {
    if (range) {
        range->min = std::min(range->min, low);
        range->max = std::max(range->max, high);
    } else {
        range = Range{low, high};
    }
}

void check_tolerance(Tolerance tol) {
    if ((tol.mode != Tolerance::Mode::Absolute && tol.mode != Tolerance::Mode::Relative) ||
        !std::isfinite(tol.value) || tol.value < 0) throw std::invalid_argument("invalid point-wise tolerance");
}

double absolute(Tolerance tol, const std::optional<Range>& range) {
    if (tol.mode == Tolerance::Mode::Absolute || tol.value == 0 || !range || range->min == range->max)
        return tol.value;
    const double span = range->max - range->min;
    // Opposite finite extremes can overflow the subtraction even when a
    // fractional tolerance times their distance fits in double.
    const double bound = std::isfinite(span) ? tol.value * span
                                             : tol.value * range->max - tol.value * range->min;
    if (!std::isfinite(bound)) throw std::overflow_error("resolved tolerance exceeds double range");
    return bound;
}

template <class T>
void scan(const T* data, std::size_t count, const PointRequest& request,
          std::uint64_t* mask, ChunkRanges& ranges) {
    std::array<double, kBatch> x{}, values;
    std::array<std::size_t, kBatch> offsets;
    std::copy_n(data, count, x.begin());
    if (request.exact) request.exact->eval(x.data(), values.data(), count);
    auto mark = [&](std::size_t i) { mask[i / kWordBits] |= std::uint64_t{1} << (i % kWordBits); };
    std::size_t kept = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const double v = x[i];
        if (!std::isfinite(v)) {
            ++ranges.nonfinite_inputs;
            mark(i);
        } else if (request.exact && values[i] != 0) {
            mark(i);
        } else {
            include(ranges.data, v, v);
            x[kept] = v;
            offsets[kept++] = i;
        }
    }
    ranges.targets.resize(request.targets.size());
    for (std::size_t t = 0; t < request.targets.size(); ++t) {
        request.targets[t].expression.eval(x.data(), values.data(), kept);
        for (std::size_t i = 0; i < kept; ++i) {
            const double v = values[i];
            if (std::isfinite(v)) include(ranges.targets[t], v, v);
            else mark(offsets[i]);
        }
    }
}
}  // namespace

PointResolution resolve_pointwise(DType dtype, const void* original, std::size_t array_bytes,
                                  const PointRequest& request, int threads) {
    const auto width = common::element_bytes(dtype);
    if (!width || !original || !array_bytes || array_bytes % width || threads < 0)
        throw std::invalid_argument("invalid input for point-wise resolution");
    if (request.data_bound) check_tolerance(*request.data_bound);
    for (const auto& target : request.targets) check_tolerance(target.tolerance);

    const auto count = array_bytes / width;
    PointResolution result;
    result.exact_mask.resize(1 + (count - 1) / kWordBits);
    std::vector<ChunkRanges> chunks(1 + (count - 1) / kBatch);
    common::parallel_for(chunks.size(), threads, [&](std::size_t chunk) {
        const auto begin = chunk * kBatch;
        const auto length = std::min(kBatch, count - begin);
        auto* mask = result.exact_mask.data() + begin / kWordBits;
        if (dtype == DType::Float32) {
            scan(static_cast<const float*>(original) + begin, length, request, mask, chunks[chunk]);
        } else {
            scan(static_cast<const double*>(original) + begin, length, request, mask, chunks[chunk]);
        }
    });

    result.target_ranges.resize(request.targets.size());
    for (const auto& chunk : chunks) {
        result.nonfinite_inputs += chunk.nonfinite_inputs;
        if (chunk.data) include(result.data_range, chunk.data->min, chunk.data->max);
        for (std::size_t t = 0; t < chunk.targets.size(); ++t) {
            const auto& range = chunk.targets[t];
            if (range) include(result.target_ranges[t], range->min, range->max);
        }
    }
    if (request.data_bound) result.request.data_bound_abs = absolute(*request.data_bound, result.data_range);
    for (std::size_t t = 0; t < request.targets.size(); ++t) {
        const auto& target = request.targets[t];
        result.request.targets.push_back({target.expression, absolute(target.tolerance, result.target_ranges[t])});
    }
    result.request.exact = request.exact;
    return result;
}

}  // namespace qoiw::frontend
