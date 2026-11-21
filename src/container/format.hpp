#pragma once

#include <cmath>
#include <limits>

#include "container/container.hpp"

namespace qoiw::container {

// Provisional M0 format. The header CRC covers the body, excluding its length.
constexpr std::uint8_t kMagic[] = {'Q', 'O', 'I', 'W'};
constexpr std::uint8_t kMajor = 0;
constexpr std::uint8_t kMinor = 0;
constexpr std::uint8_t kHasDataBound = 1;
constexpr std::uint16_t kSkippable = 1;
enum class Section : std::uint8_t {
    Core = 0x01, Residual = 0x10, Lossless = 0x11, Steps = 0x12, Transform = 0x20, Note = 0x7e, End = 0xff
};

inline bool valid_shape(DType dtype, const std::vector<std::uint64_t>& dims) {
    if (dtype != DType::Float32 && dtype != DType::Float64) return false;
    if (dims.empty() || dims.size() > 4) return false;
    std::size_t bytes = dtype == DType::Float32 ? 4 : 8;
    for (auto extent : dims) {
        if (extent == 0 || extent > std::numeric_limits<std::size_t>::max() / bytes) return false;
        bytes *= static_cast<std::size_t>(extent);
    }
    return true;
}

inline bool valid_bound(double bound) { return std::isfinite(bound) && bound >= 0; }

}  // namespace qoiw::container
