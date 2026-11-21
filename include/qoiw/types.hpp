#pragma once

#include <cstddef>
#include <cstdint>

namespace qoiw {

struct Bytes {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

enum class DType : std::uint8_t { Float32 = 1, Float64 = 2 };

}  // namespace qoiw
