#include "common/byte_io.hpp"

#include <cstring>
#include <limits>

namespace qoiw::common {

void ByteWriter::bytes(const std::uint8_t* data, std::size_t size) {
    if (size == 0) return;
    if (!data) throw std::invalid_argument("null source buffer");
    output_.insert(output_.end(), data, data + size);
}

void ByteWriter::u8(std::uint8_t value) { output_.push_back(value); }

template <class UInt>
static void write_le(ByteWriter& output, UInt value) {
    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
        output.u8(static_cast<std::uint8_t>(value));
        value >>= 8;
    }
}

void ByteWriter::u16(std::uint16_t value) { write_le(*this, value); }
void ByteWriter::u32(std::uint32_t value) { write_le(*this, value); }
void ByteWriter::u64(std::uint64_t value) { write_le(*this, value); }

void ByteWriter::f64(double value) {
    std::uint64_t bits;
    static_assert(sizeof(value) == sizeof(bits) && std::numeric_limits<double>::is_iec559);
    std::memcpy(&bits, &value, sizeof(bits));
    u64(bits);
}

void ByteWriter::uleb128(std::uint64_t value) {
    while (value >= 0x80) {
        u8(static_cast<std::uint8_t>(value & 0x7f) | 0x80);
        value >>= 7;
    }
    u8(static_cast<std::uint8_t>(value));
}

void ByteWriter::zigzag(std::int64_t value) {
    const auto bits = static_cast<std::uint64_t>(value);
    uleb128((bits << 1) ^ (value < 0 ? UINT64_MAX : 0));
}

}  // namespace qoiw::common
