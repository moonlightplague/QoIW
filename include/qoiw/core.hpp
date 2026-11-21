#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "qoiw/types.hpp"

namespace qoiw {

struct CoreParams {
    double bound = 0;
    int threads = 1;
    bool allow_stream_changing_parallelism = false;
    std::map<std::string, std::string> options;
};

struct CoreOutput {
    std::vector<std::uint8_t> stream;
    std::vector<std::uint8_t> decode_params;
};

// One field per call. Dimensions are in C order. A core instance supports
// simultaneous calls on disjoint buffers; its identifying strings live with it.
class Core {
   public:
    virtual ~Core() = default;
    virtual std::string_view name() const = 0;
    virtual std::string_view version() const = 0;
    virtual std::string_view format_version() const = 0;
    virtual bool supports(DType dtype, const std::vector<std::uint64_t>& dims) const = 0;

    // Input points to an array of the given storage type and shape.
    virtual CoreOutput compress(const void* data, DType dtype, const std::vector<std::uint64_t>& dims,
                                const CoreParams& params) const = 0;
    // Output is caller-owned storage of the given type, disjoint from the stream.
    virtual void decompress(Bytes stream, Bytes decode_params, DType dtype, const std::vector<std::uint64_t>& dims,
                            int threads, void* out, std::size_t out_bytes) const = 0;
};

}  // namespace qoiw
