#include "backend/lossless.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

#include <zstd.h>

#include "common/byte_io.hpp"
#include "common/shape.hpp"

namespace qoiw::backend {
namespace {
using common::ByteReader;
using common::CorruptStream;

// Check framing before decoding. Grow the output only as zstd produces bytes,
// even when a frame advertises a large content size that fits the caller's array.
std::vector<std::uint8_t> decompress_frame(ByteReader frame, std::size_t minimum, std::size_t maximum) {
    const auto content = ZSTD_getFrameContentSize(frame.data(), frame.remaining());
    if (content == ZSTD_CONTENTSIZE_UNKNOWN || content == ZSTD_CONTENTSIZE_ERROR ||
        content < minimum || content > maximum) throw CorruptStream("invalid lossless frame content size");
    if (ZSTD_findFrameCompressedSize(frame.data(), frame.remaining()) != frame.remaining())
        throw CorruptStream("invalid lossless zstd frame length");

    std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> context(ZSTD_createDCtx(), ZSTD_freeDCtx);
    if (!context) throw std::bad_alloc();
    auto status = ZSTD_initDStream(context.get());
    if (ZSTD_isError(status)) throw std::runtime_error(ZSTD_getErrorName(status));
    ZSTD_inBuffer in{frame.data(), frame.remaining(), 0};
    std::vector<std::uint8_t> result;
    std::array<std::uint8_t, 16384> chunk;
    do {
        ZSTD_outBuffer out{chunk.data(), chunk.size(), 0};
        const auto before = in.pos;
        status = ZSTD_decompressStream(context.get(), &out, &in);
        if (ZSTD_isError(status) || out.pos > content - result.size() ||
            (status && in.pos == before && out.pos == 0)) throw CorruptStream("invalid compressed correction data");
        result.insert(result.end(), chunk.data(), chunk.data() + out.pos);
    } while (status != 0);
    if (in.pos != in.size || result.size() != content) throw CorruptStream("incomplete correction frame");
    return result;
}
}  // namespace

std::size_t apply_lossless(Bytes payload, DType dtype, void* decoded, std::size_t array_bytes) {
    const auto width = common::element_bytes(dtype);
    if (!width || !decoded || !array_bytes || array_bytes % width)
        throw std::invalid_argument("invalid decoded array for lossless corrections");
    if (payload.size == 0) return 0;

    ByteReader in(payload.data, payload.size);
    const auto elements = array_bytes / width;
    const auto count = in.u64();
    if (count == 0 || count > elements) throw CorruptStream("invalid lossless correction count");
    const auto gap_frame = in.take(in.u64());
    // Each gap uses at most ten bytes and the increasing indices fit the field.
    const auto max_gaps = count > elements / 10 ? elements : static_cast<std::size_t>(count) * 10;
    const auto gaps = decompress_frame(gap_frame, count, max_gaps);
    const auto value_bytes = static_cast<std::size_t>(count) * width;
    const auto values = decompress_frame(in, value_bytes, value_bytes);

    ByteReader positions(gaps.data(), gaps.size());
    std::vector<std::size_t> indices;
    indices.reserve(count);
    std::size_t index = 0;
    for (std::size_t k = 0; k < count; ++k) {
        const auto gap = positions.uleb128();
        if ((k && gap == 0) || gap >= elements - index) throw CorruptStream("invalid lossless correction index");
        index += static_cast<std::size_t>(gap);
        indices.push_back(index);
    }
    if (positions.remaining()) throw CorruptStream("trailing lossless index bytes");

    ByteReader raw(values.data(), values.size());
    for (auto i : indices) {
        auto* dest = static_cast<std::uint8_t*>(decoded) + i * width;
        if (width == 4) {
            const auto bits = raw.u32();
            std::memcpy(dest, &bits, sizeof(bits));
        } else {
            const auto bits = raw.u64();
            std::memcpy(dest, &bits, sizeof(bits));
        }
    }
    return indices.size();
}

}  // namespace qoiw::backend
