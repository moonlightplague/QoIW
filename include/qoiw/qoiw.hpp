#pragma once

#include "qoiw/options.hpp"
#include "qoiw/report.hpp"
#include "qoiw/request.hpp"

namespace qoiw {

// Compress one field with point-wise targets. Assign the report on success.
std::vector<std::uint8_t> compress(const ArrayView& input, const Request& request = {},
                                   const Options& options = {}, Report* report = nullptr);

// Inspect metadata and payload lengths without loading a core or decoding.
StreamInfo inspect(Bytes stream);

// Decode one field using its stored core and corrections. Capacity counts
// values of the specified output type. Use the output only after success.
void decompress(Bytes stream, float* out, std::size_t capacity, int threads = 1);
void decompress(Bytes stream, double* out, std::size_t capacity, int threads = 1);

// Check two fields of the same shape and storage type. Resolve relative
// tolerances on the original field. Neither input is modified.
Verification verify(const ArrayView& original, const ArrayView& reconstructed, const Request& request = {},
                    int threads = 1);

}  // namespace qoiw
