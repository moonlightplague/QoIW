#pragma once

#include "container/container.hpp"
#include "qoiw/core.hpp"

namespace qoiw::pipeline {

// Decode one parsed field into caller-owned storage. Use the output only after
// this returns successfully. Returns the number of exact values restored.
std::size_t decode_field(const container::Stream& stream, std::size_t field_index, const Core& core,
                         int threads, void* out, std::size_t out_bytes);

}  // namespace qoiw::pipeline
