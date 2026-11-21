// Adapted from QPET. Copyright (c) 2026, Jinyang Liu. See LICENSES/QPET.txt.
#include "frontend/sample.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "common/parallel_for.hpp"
#include "common/shape.hpp"

namespace qoiw::frontend {
namespace {
std::size_t ceil_div(std::size_t n, std::size_t divisor) {
    return 1 + (n - 1) / divisor;
}

double halton(std::size_t index, unsigned base) {
    double fraction = 1., value = 0.;
    for (; index; index /= base) {
        fraction /= base;
        value += fraction * (index % base);
    }
    return value;
}

template <class T>
std::vector<std::vector<T>> copy_blocks(const T* original, const std::vector<std::uint64_t>& dims,
                                        const BlockLayout& layout, int threads) {
    std::array<std::size_t, 4> strides{};
    strides[dims.size() - 1] = 1;
    for (auto d = dims.size() - 1; d > 0; --d) strides[d - 1] = strides[d] * dims[d];

    // Fold full trailing dimensions into each memcpy. A whole-field sample
    // takes one copy even when the fastest dimension has just one element.
    auto outer = dims.size() - 1;
    std::size_t row_values = layout.dims[outer];
    while (outer > 0 && layout.dims[outer] == dims[outer]) {
        --outer;
        row_values *= layout.dims[outer];
    }
    std::size_t rows = 1;
    for (std::size_t d = 0; d < outer; ++d) rows *= layout.dims[d];

    std::vector<std::vector<T>> blocks(layout.origins.size());
    common::parallel_for(blocks.size(), threads, [&](std::size_t k) {
        auto& block = blocks[k];
        block.resize(rows * row_values);
        for (std::size_t row = 0; row < rows; ++row) {
            auto offset = layout.origins[k];
            auto index = row;
            for (auto d = outer; d > 0; --d) {
                offset += (index % layout.dims[d - 1]) * strides[d - 1];
                index /= layout.dims[d - 1];
            }
            std::memcpy(block.data() + row * row_values, original + offset, row_values * sizeof(T));
        }
    });
    return blocks;
}
}  // namespace

BlockLayout plan_blocks(DType dtype, const std::vector<std::uint64_t>& dims, const BlockSamplingOptions& options) {
    const auto bytes = common::array_bytes(dtype, dims);
    if (!bytes || !(options.sample_rate > 0 && options.sample_rate <= 1))
        throw std::invalid_argument("invalid block sample shape, dtype or rate");
    const auto elements = bytes / common::element_bytes(dtype);
    const auto rank = dims.size();
    constexpr std::size_t edges[]{262144, 512, 64, 24};
    const auto max_edge = options.block_edge ? options.block_edge : edges[rank - 1];
    const auto min_edge = options.min_edge ? options.min_edge : (rank == 2 ? 128u : edges[rank - 1]);
    const auto min_blocks = options.min_blocks ? options.min_blocks : (rank == 2 ? 12u : 5u);
    const auto max_blocks = std::max(min_blocks, options.max_blocks ? options.max_blocks : 64u);
    const double target = options.sample_rate * elements;
    const auto fit = static_cast<std::size_t>(std::pow(target / min_blocks, 1. / rank));
    const auto edge = std::min(max_edge, std::max(fit, min_edge));

    BlockLayout layout{{}, {}, false};
    std::size_t block_values = 1;
    for (auto extent : dims) {
        const auto tiles = ceil_div(extent, edge);
        const auto block_extent = ceil_div(extent, tiles);
        layout.dims.push_back(block_extent);
        block_values *= block_extent;
    }
    const auto count = std::clamp(static_cast<std::size_t>(std::ceil(target / block_values)), min_blocks, max_blocks);
    // count * block_values >= half the input, without overflowing the product.
    if (count > (elements - 1) / 2 / block_values) return {dims, {0}, true};

    constexpr unsigned bases[]{2, 3, 5, 7};
    layout.origins.resize(count);
    for (std::size_t k = 0; k < count; ++k) {
        std::size_t origin = 0;
        for (std::size_t d = 0; d < rank; ++d) {
            const std::size_t extent = dims[d], block = layout.dims[d];
            const auto tiles = ceil_div(extent, block);
            const auto tile = std::min(tiles - 1, static_cast<std::size_t>(halton(k + 1, bases[d]) * tiles));
            // The last tile shifts back to fit if the division left a remainder.
            origin = origin * extent + std::min(tile * block, extent - block);
        }
        layout.origins[k] = origin;
    }
    return layout;
}

BlockSample sample_blocks(DType dtype, const void* original, std::size_t array_bytes,
                          const std::vector<std::uint64_t>& dims, int threads, const BlockSamplingOptions& options) {
    if (!original || !array_bytes || array_bytes != common::array_bytes(dtype, dims) || threads < 0)
        throw std::invalid_argument("invalid field or thread count for block sampling");
    BlockSample sample{plan_blocks(dtype, dims, options), {}};
    if (dtype == DType::Float32) {
        sample.blocks = copy_blocks(static_cast<const float*>(original), dims, sample.layout, threads);
    } else {
        sample.blocks = copy_blocks(static_cast<const double*>(original), dims, sample.layout, threads);
    }
    return sample;
}

}  // namespace qoiw::frontend
