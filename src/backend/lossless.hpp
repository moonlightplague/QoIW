#pragma once

#include <vector>

#include "qoiw/types.hpp"

namespace qoiw::backend {

// Input and output arrays are in native byte order. An empty payload means no
// corrections. Indices refer to flat, C-order values and must strictly increase.
std::vector<std::uint8_t> encode_lossless(DType dtype, const void* original, std::size_t array_bytes,
                                          const std::vector<std::size_t>& indices);

// Validate the entire layer against the supplied array before modifying it.
// Returns the number of restored values.
std::size_t apply_lossless(Bytes payload, DType dtype, void* decoded, std::size_t array_bytes);

}  // namespace qoiw::backend
