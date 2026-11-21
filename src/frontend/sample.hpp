#pragma once

#include <variant>
#include <vector>

#include "qoiw/types.hpp"

namespace qoiw::frontend {

struct BlockSamplingOptions {
    double sample_rate = .01;
    // Zero selects the rank-specific default for each size.
    std::size_t block_edge = 0, min_edge = 0, min_blocks = 0, max_blocks = 0;
};

struct BlockLayout {
    std::vector<std::uint64_t> dims;   // Shape shared by all sample blocks.
    std::vector<std::size_t> origins;  // C-order offsets in the original field, in elements.
    bool whole_input_sampled;
};

BlockLayout plan_blocks(DType dtype, const std::vector<std::uint64_t>& dims,
                        const BlockSamplingOptions& options = {});

struct BlockSample {
    BlockLayout layout;
    // Each owned, contiguous array has layout.dims and the original storage type.
    std::variant<std::vector<std::vector<float>>, std::vector<std::vector<double>>> blocks;
};

BlockSample sample_blocks(DType dtype, const void* original, std::size_t array_bytes,
                          const std::vector<std::uint64_t>& dims, int threads, const BlockSamplingOptions& options = {});

}  // namespace qoiw::frontend
