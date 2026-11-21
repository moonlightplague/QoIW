#include "common/byte_io.hpp"

#include <cstring>
#include <limits>

namespace qoiw::common {

ByteReader::ByteReader(const std::uint8_t* data, std::size_t size) : data_(data), remaining_(size) {
    if (!data && size != 0) throw std::invalid_argument("null input buffer");
}

ByteReader ByteReader::take(std::uint64_t size) {
    if (size > remaining_) throw CorruptStream("truncated input");
    ByteReader part(data_, static_cast<std::size_t>(size));
    if (size != 0) data_ += size;
    remaining_ -= static_cast<std::size_t>(size);
    return part;
}

std::uint8_t ByteReader::u8() { return *take(1).data_; }

template <class UInt>
UInt ByteReader::read_le() {
    const auto bytes = take(sizeof(UInt));
    UInt value = 0;
    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
        value |= static_cast<UInt>(bytes.data_[i]) << (8 * i);
    }
    return value;
}

std::uint16_t ByteReader::u16() { return read_le<std::uint16_t>(); }
std::uint32_t ByteReader::u32() { return read_le<std::uint32_t>(); }
std::uint64_t ByteReader::u64() { return read_le<std::uint64_t>(); }

double ByteReader::f64() {
    auto bits = u64();
    double value;
    static_assert(sizeof(value) == sizeof(bits) && std::numeric_limits<double>::is_iec559);
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint64_t ByteReader::uleb128() {
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 7) {
        const auto byte = u8();
        if (shift == 63 && byte > 1) throw CorruptStream("ULEB128 overflow");
        value |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) return value;
    }
    throw CorruptStream("unterminated ULEB128");
}

std::int64_t ByteReader::zigzag() {
    const auto encoded = uleb128();
    const auto magnitude = static_cast<std::int64_t>(encoded >> 1);
    return (encoded & 1) ? -1 - magnitude : magnitude;
}

std::uint32_t crc32c(const std::uint8_t* data, std::size_t size) {
    // Reflected Castagnoli polynomial, used for the short container header.
    std::uint32_t crc = UINT32_MAX;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0x82f63b78U : 0U);
        }
    }
    return ~crc;
}

}  // namespace qoiw::common
