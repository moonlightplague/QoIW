#pragma once

#include <cmath>

#include "common/shape.hpp"

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

inline bool valid_bound(double bound) { return std::isfinite(bound) && bound >= 0; }

}  // namespace qoiw::container
