#include "pipeline/decode_field.hpp"

#include <chrono>

#include "backend/lossless.hpp"
#include "common/shape.hpp"

namespace qoiw::pipeline {

std::size_t decode_field(const container::Stream& stream, std::size_t field_index, const Core& core,
                         int threads, void* out, std::size_t out_bytes, DecompressionTimes* times) {
    const auto bytes = common::array_bytes(stream.dtype, stream.dims);
    if (!bytes || !out || bytes > out_bytes || field_index >= stream.fields.size() || threads < 0)
        throw std::invalid_argument("invalid field decode arguments");
    if (core.name() != stream.core_name || !core.supports(stream.dtype, stream.dims))
        throw std::invalid_argument("core does not match the stored field");
    const auto& field = stream.fields[field_index];
    using Clock = std::chrono::steady_clock;
    const auto seconds = [](Clock::time_point from, Clock::time_point to) {
        return std::chrono::duration<double>(to - from).count();
    };
    const auto start = Clock::now();
    core.decompress(field.core_stream, field.decode_params, stream.dtype, stream.dims, threads, out, bytes);
    const auto decoded = Clock::now();
    const auto restored = backend::apply_lossless(field.lossless, stream.dtype, out, bytes);
    if (times) {
        times->core_decode = seconds(start, decoded);
        times->correction = seconds(decoded, Clock::now());
    }
    return restored;
}

}  // namespace qoiw::pipeline
