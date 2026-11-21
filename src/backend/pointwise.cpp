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

template <class T>
Mask check_chunk(const T* original, const T* decoded, std::size_t count, const ResolvedPointRequest& request) {
    Mask restore;
    if (std::memcmp(original, decoded, count * sizeof(T)) == 0) return restore;

    // Batch only changed, finite pairs that still need an expression check.
    std::array<double, kChunk> x, y;
    std::array<std::size_t, kChunk> offsets;
    std::size_t pending = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (std::memcmp(original + i, decoded + i, sizeof(T)) == 0) continue;
        const double a = original[i], b = decoded[i];
        if (!std::isfinite(a) || !std::isfinite(b) ||
            (request.data_bound_abs && !(std::abs(a - b) <= *request.data_bound_abs))) {
            restore.set(i);
        } else if (request.exact || !request.targets.empty()) {
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
        check_expression(*request.exact, [](double a, double b) { return a != 0 || b != 0; });
    }
    for (const auto& target : request.targets) {
        check_expression(target.expression, [&](double a, double b) {
            return !(std::abs(a - b) <= target.tolerance_abs);
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
        chunks[chunk] = check_chunk(original + begin, decoded + begin, std::min(kChunk, count - begin), request);
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
}  // namespace

PointCorrections correct_pointwise(DType dtype, const void* original, void* decoded,
                                   std::size_t array_bytes, const ResolvedPointRequest& request, int threads) {
    const auto width = common::element_bytes(dtype);
    if (!width || !original || !decoded || !array_bytes || array_bytes % width || threads < 0)
        throw std::invalid_argument("invalid point-wise correction buffers or thread count");
    auto valid = [](double bound) { return std::isfinite(bound) && bound >= 0; };
    if (request.data_bound_abs && !valid(*request.data_bound_abs)) throw std::invalid_argument("invalid data bound");
    for (const auto& target : request.targets) {
        if (!valid(target.tolerance_abs)) throw std::invalid_argument("invalid point-wise tolerance");
    }

    const auto count = array_bytes / width;
    const auto indices = dtype == DType::Float32
        ? find_indices(static_cast<const float*>(original), static_cast<const float*>(decoded), count, request, threads)
        : find_indices(static_cast<const double*>(original), static_cast<const double*>(decoded), count, request, threads);
    PointCorrections result;
    result.lossless = encode_lossless(dtype, original, array_bytes, indices);
    result.restored = apply_lossless({result.lossless.data(), result.lossless.size()}, dtype, decoded, array_bytes);
    return result;
}

}  // namespace qoiw::backend
