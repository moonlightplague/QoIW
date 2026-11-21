#include "container/container.hpp"

#include <algorithm>
#include <array>
#include <iterator>

#include "common/byte_io.hpp"
#include "container/format.hpp"

namespace qoiw::container {
namespace {
using common::ByteReader;
using common::CorruptStream;

std::string_view read_string(ByteReader& in) {
    const auto chars = in.take(in.u16());
    return {reinterpret_cast<const char*>(chars.data()), chars.remaining()};
}

Field read_field(ByteReader& body) {
    Field field;
    field.name = read_string(body);
    field.bound = body.f64();
    const double data_bound = body.f64();
    const auto flags = body.u8();
    if (flags & ~kHasDataBound) throw UnsupportedFormat("unknown field flags");
    if (!valid_bound(field.bound) || !valid_bound(data_bound)) throw CorruptStream("invalid bound");
    if (flags & kHasDataBound) field.data_bound_abs = data_bound;
    const auto params = body.take(body.u32());
    field.decode_params = {params.data(), params.remaining()};
    field.fingerprint = body.u64();
    return field;
}

Stream read_header(ByteReader& input) {
    const auto magic = input.take(sizeof(kMagic));
    if (!std::equal(std::begin(kMagic), std::end(kMagic), magic.data())) throw CorruptStream("invalid QoIW magic");
    if (input.u8() != kMajor) throw UnsupportedFormat("unsupported QoIW major version");
    input.u8();  // Minor versions can introduce skippable sections.
    if (input.u16() != 0) throw UnsupportedFormat("unknown stream flags");

    auto body = input.take(input.u32());
    if (input.u32() != common::crc32c(body.data(), body.remaining())) throw CorruptStream("invalid header checksum");
    Stream stream;
    stream.dtype = static_cast<DType>(body.u8());
    const auto ndims = body.u8();
    const auto nfields = body.u8();
    if (body.u8() != 0) throw UnsupportedFormat("unknown header extension");
    if (ndims == 0 || ndims > 4 || nfields == 0) throw CorruptStream("invalid rank or field count");
    for (unsigned i = 0; i < ndims; ++i) stream.dims.push_back(body.u64());
    if (!common::array_bytes(stream.dtype, stream.dims)) throw CorruptStream("invalid array type or dimensions");

    stream.wrapper_version = read_string(body);
    stream.core_name = read_string(body);
    if (stream.core_name.empty()) throw CorruptStream("missing core name");
    stream.core_version = read_string(body);
    stream.core_format_version = read_string(body);
    for (unsigned i = 0; i < nfields; ++i) stream.fields.push_back(read_field(body));
    if (body.u8() != 0) throw UnsupportedFormat("target records are not supported yet");
    stream.exact_condition = read_string(body);
    if (body.remaining() != 0) throw CorruptStream("trailing header bytes");
    return stream;
}
}  // namespace

Stream read(Bytes bytes) {
    ByteReader input(bytes.data, bytes.size);
    auto stream = read_header(input);
    std::array<bool, UINT8_MAX> seen{};
    std::size_t core_count = 0;
    for (;;) {
        const auto type = static_cast<Section>(input.u8());
        const auto field = input.u8();
        const auto flags = input.u16();
        const auto payload = input.take(input.u64());
        switch (type) {
            case Section::Core:
                if (flags != 0) throw UnsupportedFormat("unknown CORE flags");
                if (field >= stream.fields.size() || seen[field]) throw CorruptStream("invalid or duplicate CORE field");
                stream.fields[field].core_stream = {payload.data(), payload.remaining()};
                seen[field] = true;
                ++core_count;
                break;
            case Section::End:
                if (field != 0 || flags != 0 || payload.remaining() != 0 || input.remaining() != 0)
                    throw CorruptStream("invalid END section");
                if (core_count != stream.fields.size()) throw CorruptStream("missing CORE section");
                return stream;
            case Section::Residual:
            case Section::Lossless:
            case Section::Steps:
            case Section::Transform:
                throw UnsupportedFormat("unsupported correction or transform layer");
            case Section::Note:
                // Informational only. The M0 reader does not retain notes.
                break;
            default:
                if (!(flags & kSkippable)) throw UnsupportedFormat("unknown required section");
        }
    }
}

}  // namespace qoiw::container
