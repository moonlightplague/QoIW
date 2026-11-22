#pragma once

#include <limits>
#include <vector>

#include "qoiw/types.hpp"

namespace qoiw::common {

inline std::size_t element_bytes(DType dtype) {
    switch (dtype) {
        case DType::Float32: return 4;
        case DType::Float64: return 8;
    }
    return 0;
}

// Zero denotes an unsupported shape or a byte count that the host cannot address.
inline std::size_t array_bytes(DType dtype, const std::vector<std::uint64_t>& dims) {
    std::size_t bytes = element_bytes(dtype);
    if (!bytes || dims.empty() || dims.size() > 4) return 0;
    for (auto extent : dims) {
        if (extent == 0 || extent > std::numeric_limits<std::size_t>::max() / bytes) return 0;
        bytes *= static_cast<std::size_t>(extent);
    }
    return bytes;
}

}  // namespace qoiw::common
