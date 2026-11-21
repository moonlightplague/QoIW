#pragma once

#include <limits>
#include <vector>

#include "qoiw/types.hpp"

namespace qoiw::common {

// Zero denotes an unsupported shape or a byte count that the host cannot address.
inline std::size_t array_bytes(DType dtype, const std::vector<std::uint64_t>& dims) {
    if (dtype != DType::Float32 && dtype != DType::Float64) return 0;
    if (dims.empty() || dims.size() > 4) return 0;
    std::size_t bytes = dtype == DType::Float32 ? 4 : 8;
    for (auto extent : dims) {
        if (extent == 0 || extent > std::numeric_limits<std::size_t>::max() / bytes) return 0;
        bytes *= static_cast<std::size_t>(extent);
    }
    return bytes;
}

}  // namespace qoiw::common
