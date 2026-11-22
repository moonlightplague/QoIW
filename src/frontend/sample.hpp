#pragma once

#include <variant>
#include <vector>

#include "qoiw/options.hpp"

namespace qoiw::frontend {

using qoiw::BlockSamplingOptions;

struct BlockLayout {
    std::vector<std::uint64_t> dims;   // Shape shared by all sample blocks.
    std::vector<std::size_t> origins;  // C-order offsets in the original field, in elements.
    bool whole_input_sampled;
};

BlockLayout plan_blocks(DType dtype, const std::vector<std::uint64_t>& dims,
                        const BlockSamplingOptions& options = {});

using SampleBlocks = std::variant<std::vector<std::vector<float>>, std::vector<std::vector<double>>>;

struct BlockSample {
    BlockLayout layout;
    // Each owned, contiguous array has layout.dims and the original storage type.
    SampleBlocks blocks;

    DType dtype() const;
    // Check the storage against the shared block shape before using it with a core.
    std::size_t values_per_block() const;
};

BlockSample sample_blocks(DType dtype, const void* original, std::size_t array_bytes,
                          const std::vector<std::uint64_t>& dims, int threads, const BlockSamplingOptions& options = {});

}  // namespace qoiw::frontend
