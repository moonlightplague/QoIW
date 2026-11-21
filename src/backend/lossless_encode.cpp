#include "backend/lossless.hpp"

#include <cstring>
#include <stdexcept>

#include <zstd.h>

#include "common/byte_io.hpp"
#include "common/shape.hpp"

namespace qoiw::backend {
namespace {
std::vector<std::uint8_t> compress_frame(const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint8_t> frame(ZSTD_compressBound(raw.size()));
    const auto size = ZSTD_compress(frame.data(), frame.size(), raw.data(), raw.size(), 3);
    if (ZSTD_isError(size)) throw std::runtime_error(ZSTD_getErrorName(size));
    frame.resize(size);
    return frame;
}
}  // namespace

std::vector<std::uint8_t> encode_lossless(DType dtype, const void* original, std::size_t array_bytes,
                                          const std::vector<std::size_t>& indices) {
    const auto width = common::element_bytes(dtype);
    if (!width || !original || !array_bytes || array_bytes % width)
        throw std::invalid_argument("invalid original array for lossless corrections");
    if (indices.empty()) return {};

    const auto elements = array_bytes / width;
    if (indices.size() > elements) throw std::invalid_argument("more corrections than values");
    std::vector<std::uint8_t> gaps, values;
    gaps.reserve(indices.size());
    values.reserve(indices.size() * width);
    common::ByteWriter gap_writer(gaps), value_writer(values);
    std::size_t previous = 0;
    for (std::size_t k = 0; k < indices.size(); ++k) {
        const auto index = indices[k];
        if (index >= elements || (k && index <= previous)) throw std::invalid_argument("invalid correction index");
        gap_writer.uleb128(index - previous);
        previous = index;
        const auto* src = static_cast<const std::uint8_t*>(original) + index * width;
        if (width == 4) {
            std::uint32_t bits;
            std::memcpy(&bits, src, sizeof(bits));
            value_writer.u32(bits);
        } else {
            std::uint64_t bits;
            std::memcpy(&bits, src, sizeof(bits));
            value_writer.u64(bits);
        }
    }
    const auto gap_frame = compress_frame(gaps);
    const auto value_frame = compress_frame(values);
    std::vector<std::uint8_t> payload;
    common::ByteWriter writer(payload);
    writer.u64(indices.size());
    writer.u64(gap_frame.size());
    writer.bytes(gap_frame.data(), gap_frame.size());
    writer.bytes(value_frame.data(), value_frame.size());
    return payload;
}

}  // namespace qoiw::backend
