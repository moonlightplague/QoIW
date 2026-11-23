#pragma once

#include "qoi/pointwise.hpp"
#include "qoiw/report.hpp"
#include "qoiw/types.hpp"

namespace qoiw::backend {

struct PointCorrections {
    std::vector<std::uint8_t> lossless;
    std::size_t restored = 0;
};

// Check the reconstruction in its storage type, encode the corrections, and
// restore the original bits. verify_patch restores through the encoded layer.
// Both buffers hold array_bytes bytes and are disjoint.
PointCorrections correct_pointwise(DType dtype, const void* original, void* decoded,
                                   std::size_t array_bytes, const ResolvedPointRequest& request, int threads,
                                   bool verify_patch = false);

Verification verify_pointwise(DType dtype, const void* original, const void* decoded,
                              std::size_t array_bytes, const ResolvedPointRequest& request, int threads);

}  // namespace qoiw::backend
