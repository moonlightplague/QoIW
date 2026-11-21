#pragma once

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

}  // namespace qoiw::frontend
