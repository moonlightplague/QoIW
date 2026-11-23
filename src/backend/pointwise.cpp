#include "backend/pointwise.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "backend/lossless.hpp"
#include "common/parallel_for.hpp"
#include "common/shape.hpp"

namespace qoiw::backend {
namespace {
constexpr std::size_t kChunk = 2048;
using Mask = std::bitset<kChunk>;

template <bool Diagnose, class T>
Mask check_chunk(const T* original, const T* decoded, std::size_t count, const ResolvedPointRequest& request,
                 Verification* stats = nullptr) {
    Mask restore;
    if (std::memcmp(original, decoded, count * sizeof(T)) == 0) return restore;

    // Batch only changed, finite pairs that still need an expression check.
    std::array<double, kChunk> x, y;
    std::array<std::size_t, kChunk> offsets;
    std::size_t pending = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (std::memcmp(original + i, decoded + i, sizeof(T)) == 0) continue;
        const double a = original[i], b = decoded[i];
        if (!std::isfinite(a) || !std::isfinite(b)) {
            restore.set(i);
            if constexpr (Diagnose) {
                ++stats->data.violations;
                ++stats->data.nonfinite_violations;
            }
            continue;
        }
        if (Diagnose || request.data_bound_abs) {
            const double error = std::abs(a - b);
            const bool violates = request.data_bound_abs && !(error <= *request.data_bound_abs);
            if constexpr (Diagnose) {
                stats->data.max_abs_error = std::max(stats->data.max_abs_error, error);
                stats->data.violations += violates;
            }
            if (violates) {
                restore.set(i);
                if constexpr (!Diagnose) continue;
            }
        }
        if (request.exact || !request.targets.empty()) {
            offsets[pending] = i;
            x[pending] = a;
            y[pending] = b;
            ++pending;
        }
    }
    if (!pending) return restore;

    std::array<double, kChunk> left, right;
    auto check_expression = [&](const Expr& expr, auto violates) {
        expr.eval(x.data(), left.data(), pending);
        expr.eval(y.data(), right.data(), pending);
        for (std::size_t j = 0; j < pending; ++j) {
            if (violates(left[j], right[j])) restore.set(offsets[j]);
        }
    };
    if (request.exact) {
        // A changed original may neither leave nor enter the exact set, nor
        // change bits while remaining in it.
        check_expression(*request.exact, [&](double a, double b) {
            const bool violates = a != 0 || b != 0;
            if constexpr (Diagnose) stats->exact_violations += violates;
            return violates;
        });
    }
    for (std::size_t t = 0; t < request.targets.size(); ++t) {
        const auto& target = request.targets[t];
        check_expression(target.expression, [&](double a, double b) {
            const double error = std::abs(a - b);
            const bool violates = !(error <= target.tolerance_abs);
            if constexpr (Diagnose) {
                auto& summary = stats->targets[t].errors;
                if (std::isfinite(a) && std::isfinite(b))
                    summary.max_abs_error = std::max(summary.max_abs_error, error);
                else
                    ++summary.nonfinite_violations;
                summary.violations += violates;
            }
            return violates;
        });
    }
    return restore;
}

template <class T>
std::vector<std::size_t> find_indices(const T* original, const T* decoded, std::size_t count,
                                      const ResolvedPointRequest& request, int threads) {
    std::vector<Mask> chunks(1 + (count - 1) / kChunk);
    common::parallel_for(chunks.size(), threads, [&](std::size_t chunk) {
        const auto begin = chunk * kChunk;
        chunks[chunk] = check_chunk<false>(original + begin, decoded + begin, std::min(kChunk, count - begin), request);
    });

    std::size_t total = 0;
    for (const auto& mask : chunks) total += mask.count();
    std::vector<std::size_t> indices;
    indices.reserve(total);
    for (std::size_t chunk = 0; chunk < chunks.size(); ++chunk) {
        if (chunks[chunk].none()) continue;
        const auto begin = chunk * kChunk;
        const auto length = std::min(kChunk, count - begin);
        for (std::size_t i = 0; i < length; ++i) {
            if (chunks[chunk][i]) indices.push_back(begin + i);
        }
    }
    return indices;
}

template <class T>
PointCorrections correct_values(DType dtype, const T* original, T* decoded, std::size_t count,
                                const ResolvedPointRequest& request, int threads, bool verify_patch) {
    const auto indices = find_indices(original, decoded, count, request, threads);
    const auto bytes = count * sizeof(T);
    PointCorrections result{encode_lossless(dtype, original, bytes, indices), indices.size()};
    if (verify_patch) {
        result.restored = apply_lossless(result.lossless, dtype, decoded, bytes);
    } else {
        for (auto i : indices) std::memcpy(decoded + i, original + i, sizeof(T));
    }
    return result;
}

std::size_t checked_count(DType dtype, const void* original, const void* decoded, std::size_t array_bytes,
                          const ResolvedPointRequest& request, int threads) {
    const auto width = common::element_bytes(dtype);
    if (!width || !original || !decoded || !array_bytes || array_bytes % width || threads < 0)
        throw std::invalid_argument("invalid point-wise buffers or thread count");
    auto valid = [](double bound) { return std::isfinite(bound) && bound >= 0; };
    if (request.data_bound_abs && !valid(*request.data_bound_abs)) throw std::invalid_argument("invalid data bound");
    for (const auto& target : request.targets) {
        if (!valid(target.tolerance_abs)) throw std::invalid_argument("invalid point-wise tolerance");
    }

    return array_bytes / width;
}

void merge_errors(ErrorStats& into, const ErrorStats& from) {
    into.max_abs_error = std::max(into.max_abs_error, from.max_abs_error);
    into.violations += from.violations;
    into.nonfinite_violations += from.nonfinite_violations;
}

template <class T>
Verification verify_values(const T* original, const T* decoded, std::size_t count,
                           const ResolvedPointRequest& request, int threads) {
    // Reuse one set of diagnostic counters across several expression batches.
    constexpr std::size_t group_size = 32 * kChunk;
    std::vector<Verification> groups(1 + (count - 1) / group_size);
    common::parallel_for(groups.size(), threads, [&](std::size_t group) {
        auto& stats = groups[group];
        stats.targets.resize(request.targets.size());
        const std::size_t begin = group * group_size;
        const std::size_t end = begin + std::min(group_size, count - begin);
        for (std::size_t offset = begin; offset < end;) {
            const auto length = std::min(kChunk, end - offset);
            stats.violating_values += check_chunk<true>(original + offset, decoded + offset, length, request,
                                                        &stats).count();
            offset += length;
        }
    });

    Verification result;
    result.values = count;
    result.data_bound_abs = request.data_bound_abs;
    for (const auto& target : request.targets)
        result.targets.push_back({{target.expression.text(), target.tolerance_abs}, {}});
    for (const auto& stats : groups) {
        result.violating_values += stats.violating_values;
        result.exact_violations += stats.exact_violations;
        merge_errors(result.data, stats.data);
        for (std::size_t t = 0; t < result.targets.size(); ++t)
            merge_errors(result.targets[t].errors, stats.targets[t].errors);
    }
    return result;
}
}  // namespace

PointCorrections correct_pointwise(DType dtype, const void* original, void* decoded,
                                   std::size_t array_bytes, const ResolvedPointRequest& request, int threads,
                                   bool verify_patch) {
    const auto count = checked_count(dtype, original, decoded, array_bytes, request, threads);
    return dtype == DType::Float32
        ? correct_values(dtype, static_cast<const float*>(original), static_cast<float*>(decoded),
                         count, request, threads, verify_patch)
        : correct_values(dtype, static_cast<const double*>(original), static_cast<double*>(decoded),
                         count, request, threads, verify_patch);
}

Verification verify_pointwise(DType dtype, const void* original, const void* decoded,
                              std::size_t array_bytes, const ResolvedPointRequest& request, int threads) {
    const auto count = checked_count(dtype, original, decoded, array_bytes, request, threads);
    return dtype == DType::Float32
        ? verify_values(static_cast<const float*>(original), static_cast<const float*>(decoded), count, request, threads)
        : verify_values(static_cast<const double*>(original), static_cast<const double*>(decoded), count, request, threads);
}

}  // namespace qoiw::backend
