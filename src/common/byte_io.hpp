#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace qoiw::common {

// A non-owning cursor. The caller keeps the input alive for every derived reader.
class ByteReader {
   public:
    ByteReader(const std::uint8_t* data, std::size_t size);
    std::size_t remaining() const { return remaining_; }
    ByteReader take(std::uint64_t size);
    std::uint8_t u8();
    std::uint16_t u16();
    std::uint32_t u32();
    std::uint64_t u64();
    double f64();
    std::uint64_t uleb128();
    std::int64_t zigzag();

   private:
    template <class UInt>
    UInt read_le();

    const std::uint8_t* data_;
    std::size_t remaining_;
};

class CorruptStream : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

class ByteWriter {
   public:
    explicit ByteWriter(std::vector<std::uint8_t>& output) : output_(output) {}
    void u8(std::uint8_t value);
    void u16(std::uint16_t value);
    void u32(std::uint32_t value);
    void u64(std::uint64_t value);
    void f64(double value);
    void uleb128(std::uint64_t value);
    void zigzag(std::int64_t value);

   private:
    std::vector<std::uint8_t>& output_;
};

std::uint32_t crc32c(const std::uint8_t* data, std::size_t size);

}  // namespace qoiw::common
