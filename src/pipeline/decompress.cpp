#include "qoiw/qoiw.hpp"

#include <new>

#include "common/byte_io.hpp"
#include "common/shape.hpp"
#include "pipeline/decode_field.hpp"
#include "pipeline/stream.hpp"

namespace qoiw {
namespace pipeline {

StreamInfo describe_stream(const container::Stream& stream, std::size_t stream_bytes) {
    StreamInfo info;
    info.dtype = stream.dtype;
    info.dims = stream.dims;
    info.num = common::array_bytes(stream.dtype, stream.dims) / common::element_bytes(stream.dtype);
    info.stream_bytes = stream_bytes;
    info.wrapper_version = stream.wrapper_version;
    info.core_name = stream.core_name;
    info.core_version = stream.core_version;
    info.core_format_version = stream.core_format_version;
    info.fields.reserve(stream.fields.size());
    for (const auto& field : stream.fields) {
        info.fields.push_back({std::string(field.name), field.bound, field.data_bound_abs, field.decode_params.size,
                               field.core_stream.size, field.lossless.size});
    }
    info.targets.reserve(stream.targets.size());
    for (const auto& target : stream.targets)
        info.targets.push_back({std::string(target.expression), target.tolerance_abs});
    if (!stream.exact_condition.empty()) info.exact_condition = std::string(stream.exact_condition);
    return info;
}

}  // namespace pipeline

namespace {
container::Stream parse(Bytes bytes) {
    try {
        return container::read(bytes);
    } catch (const container::UnsupportedFormat& error) {
        throw Error(ErrorCode::Unsupported, error.what());
    } catch (const common::CorruptStream& error) {
        throw Error(ErrorCode::CorruptStream, error.what());
    } catch (const std::invalid_argument& error) {
        throw Error(ErrorCode::InvalidArgument, error.what());
    }
}

void decode(Bytes bytes, DType dtype, void* output, std::size_t capacity, int threads) {
    if (!output || threads < 0) throw Error(ErrorCode::InvalidArgument, "invalid decode buffer or thread count");
    const auto stream = parse(bytes);
    if (stream.fields.size() != 1) throw Error(ErrorCode::Unsupported, "expected a single-field stream");
    if (dtype != stream.dtype) throw Error(ErrorCode::InvalidArgument, "output type disagrees with the stream");
    const auto required = common::array_bytes(dtype, stream.dims);
    if (capacity < required / common::element_bytes(dtype))
        throw Error(ErrorCode::InvalidArgument, "output buffer is smaller than the stored field");
    const Core* core;
    try {
        core = &get_core(stream.core_name);
    } catch (const std::invalid_argument& error) {
        throw Error(ErrorCode::Unsupported, error.what());
    }
    if (!core->supports(dtype, stream.dims)) throw Error(ErrorCode::Unsupported, "core cannot decode stored field shape");
    try {
        pipeline::decode_field(stream, 0, *core, threads, output, required);
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& error) {
        throw Error(ErrorCode::CorruptStream, error.what());
    }
}
}  // namespace

StreamInfo inspect(Bytes stream) {
    return pipeline::describe_stream(parse(stream), stream.size);
}

void decompress(Bytes stream, float* out, std::size_t capacity, int threads) {
    decode(stream, DType::Float32, out, capacity, threads);
}

void decompress(Bytes stream, double* out, std::size_t capacity, int threads) {
    decode(stream, DType::Float64, out, capacity, threads);
}

}  // namespace qoiw
