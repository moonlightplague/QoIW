#include "container/container.hpp"

#include "common/byte_io.hpp"
#include "container/format.hpp"

namespace qoiw::container {
namespace {
using common::ByteWriter;

void write_string(ByteWriter& writer, std::string_view value) {
    if (value.size() > UINT16_MAX) throw std::invalid_argument("string exceeds u16 length");
    writer.u16(static_cast<std::uint16_t>(value.size()));
    writer.bytes(reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
}

void write_section(ByteWriter& writer, Section type, std::uint8_t field, Bytes payload) {
    writer.u8(static_cast<std::uint8_t>(type));
    writer.u8(field);
    writer.u16(0);
    writer.u64(payload.size);
    writer.bytes(payload.data, payload.size);
}

std::vector<std::uint8_t> header_body(const Stream& stream) {
    if (!common::array_bytes(stream.dtype, stream.dims)) throw std::invalid_argument("invalid array type or dimensions");
    if (stream.fields.empty() || stream.fields.size() > UINT8_MAX) throw std::invalid_argument("invalid field count");
    if (stream.core_name.empty()) throw std::invalid_argument("missing core name");

    std::vector<std::uint8_t> body;
    ByteWriter writer(body);
    writer.u8(static_cast<std::uint8_t>(stream.dtype));
    writer.u8(static_cast<std::uint8_t>(stream.dims.size()));
    writer.u8(static_cast<std::uint8_t>(stream.fields.size()));
    writer.u8(0);
    for (auto dim : stream.dims) writer.u64(dim);
    write_string(writer, stream.wrapper_version);
    write_string(writer, stream.core_name);
    write_string(writer, stream.core_version);
    write_string(writer, stream.core_format_version);
    for (const auto& field : stream.fields) {
        const double data_bound = field.data_bound_abs.value_or(0);
        if (!valid_bound(field.bound) || !valid_bound(data_bound)) throw std::invalid_argument("invalid field bound");
        write_string(writer, field.name);
        writer.f64(field.bound);
        writer.f64(data_bound);
        writer.u8(field.data_bound_abs ? kHasDataBound : 0);
        if (body.size() > UINT32_MAX - 12 || field.decode_params.size > UINT32_MAX - 12 - body.size())
            throw std::invalid_argument("decode parameters exceed header capacity");
        writer.u32(static_cast<std::uint32_t>(field.decode_params.size));
        writer.bytes(field.decode_params.data, field.decode_params.size);
        writer.u64(field.fingerprint);
    }
    if (stream.targets.size() > UINT8_MAX) throw std::invalid_argument("too many target records");
    writer.u8(static_cast<std::uint8_t>(stream.targets.size()));
    for (const auto& target : stream.targets) {
        if (target.expression.empty() || !valid_bound(target.tolerance_abs))
            throw std::invalid_argument("invalid point-wise target record");
        writer.u8(kPointwiseTarget);
        writer.f64(target.tolerance_abs);
        write_string(writer, target.expression);
    }
    write_string(writer, stream.exact_condition);
    if (body.size() > UINT32_MAX) throw std::invalid_argument("header exceeds u32 length");
    return body;
}
}  // namespace

std::vector<std::uint8_t> write(const Stream& stream) {
    auto body = header_body(stream);
    std::vector<std::uint8_t> output;
    // Reserve once so appending END never forces a copy of a large core stream.
    const std::size_t framing = 16 + 12 * (stream.fields.size() + 1);
    const auto limit = output.max_size();
    if (body.size() > limit - framing) throw std::length_error("container exceeds vector capacity");
    std::size_t size = body.size() + framing;
    for (const auto& field : stream.fields) {
        if (field.core_stream.size && !field.core_stream.data) throw std::invalid_argument("null CORE payload");
        if (field.core_stream.size > limit - size) throw std::length_error("container exceeds vector capacity");
        size += field.core_stream.size;
        if (field.lossless.size) {
            if (!field.lossless.data) throw std::invalid_argument("null LOSSLESS payload");
            if (limit - size < 12 || field.lossless.size > limit - size - 12)
                throw std::length_error("container exceeds vector capacity");
            size += 12 + field.lossless.size;
        }
    }
    output.reserve(size);
    ByteWriter writer(output);
    writer.bytes(kMagic, sizeof(kMagic));
    writer.u8(kMajor);
    writer.u8(kMinor);
    writer.u16(0);
    writer.u32(static_cast<std::uint32_t>(body.size()));
    writer.bytes(body.data(), body.size());
    writer.u32(common::crc32c(body.data(), body.size()));
    for (std::size_t i = 0; i < stream.fields.size(); ++i) {
        const auto id = static_cast<std::uint8_t>(i);
        const auto& field = stream.fields[i];
        write_section(writer, Section::Core, id, field.core_stream);
        if (field.lossless.size) write_section(writer, Section::Lossless, id, field.lossless);
    }
    write_section(writer, Section::End, 0, {});
    return output;
}

}  // namespace qoiw::container
