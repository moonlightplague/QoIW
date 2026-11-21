#pragma once

#include "qoi/pointwise.hpp"
#include "qoiw/types.hpp"

namespace qoiw::backend {

struct PointCorrections {
    std::vector<std::uint8_t> lossless;
    std::size_t restored = 0;
};

// Check the core's reconstruction in its storage type, then apply the encoded
// corrections to it. Both buffers hold array_bytes bytes and are disjoint.
PointCorrections correct_pointwise(DType dtype, const void* original, void* decoded,
                                   std::size_t array_bytes, const ResolvedPointRequest& request, int threads);

}  // namespace qoiw::backend
