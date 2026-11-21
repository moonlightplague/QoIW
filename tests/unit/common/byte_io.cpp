#include "common/byte_io.hpp"

#include <array>
#include <cmath>
#include <iostream>

namespace {
using qoiw::common::ByteReader;
using qoiw::common::ByteWriter;
using qoiw::common::CorruptStream;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <class F>
void expect_corrupt(F read, const char* message) {
    try {
        read();
    } catch (const CorruptStream&) {
        return;
    }
    throw std::runtime_error(message);
}

void fixed_width() {
    const std::vector<std::uint8_t> wire = {
        0xfe, 0x34, 0x12, 0xef, 0xcd, 0xab, 0x89, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0x80,
    };
    std::vector<std::uint8_t> bytes;
    ByteWriter out(bytes);
    out.u8(0xfe);
    out.u16(0x1234);
    out.u32(0x89abcdef);
    out.u64(0x0102030405060708);
    out.f64(-0.0);
    check(bytes == wire, "little-endian bytes differ");

    ByteReader in(wire.data(), wire.size());
    check(in.u8() == 0xfe && in.u16() == 0x1234 && in.u32() == 0x89abcdef, "small integers differ");
    check(in.u64() == 0x0102030405060708, "64-bit integer differs");
    const double zero = in.f64();
    check(zero == 0 && std::signbit(zero), "negative zero lost");
    check(in.remaining() == 0, "trailing bytes after fixed-width values");
}

void variable_width() {
    constexpr std::array<std::uint64_t, 7> values{0, 1, 127, 128, 16383, 624485, UINT64_MAX};
    const std::vector<std::uint8_t> wire = {
        0, 1, 0x7f, 0x80, 1, 0xff, 0x7f, 0xe5, 0x8e, 0x26, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 1,
    };
    std::vector<std::uint8_t> bytes;
    ByteWriter out(bytes);
    for (auto value : values) out.uleb128(value);
    check(bytes == wire, "ULEB128 bytes differ");
    ByteReader in(wire.data(), wire.size());
    for (auto value : values) check(in.uleb128() == value, "ULEB128 decoded value differs");
    check(in.remaining() == 0, "trailing ULEB128 bytes");

    constexpr std::array<std::int64_t, 7> signed_values{0, -1, 1, -2, 2, INT64_MIN, INT64_MAX};
    constexpr std::array<std::uint64_t, 7> unsigned_values{0, 1, 2, 3, 4, UINT64_MAX, UINT64_MAX - 1};
    bytes.clear();
    for (auto value : signed_values) out.zigzag(value);
    ByteReader encoded(bytes.data(), bytes.size());
    auto decoded = encoded;
    for (auto value : unsigned_values) check(encoded.uleb128() == value, "zigzag mapping differs");
    for (auto value : signed_values) check(decoded.zigzag() == value, "zigzag inverse differs");
}

void boundaries() {
    ByteReader empty(nullptr, 0);
    check(empty.take(0).remaining() == 0, "zero-length slice failed");
    expect_corrupt([&] { empty.take(UINT64_MAX); }, "oversized slice accepted");
    const std::array<std::uint8_t, 2> pair{1, 2};
    ByteReader parent(pair.data(), pair.size());
    auto child = parent.take(1);
    expect_corrupt([&] { child.u16(); }, "read across slice boundary");
    check(child.u8() == 1 && parent.u8() == 2, "failed fixed-width read consumed bytes");

    std::array<std::uint8_t, 10> bytes;
    bytes.fill(0x80);
    for (std::size_t n = 0; n <= bytes.size(); ++n) {
        ByteReader input(bytes.data(), n);
        expect_corrupt([&] { input.uleb128(); }, "unterminated ULEB128 accepted");
    }
    bytes.back() = 2;
    ByteReader overflow(bytes.data(), bytes.size());
    expect_corrupt([&] { overflow.uleb128(); }, "out-of-range ULEB128 accepted");
}

void checksum() {
    const std::uint8_t digits[] = "123456789";
    check(qoiw::common::crc32c(digits, 9) == 0xe3069283, "CRC32C check value differs");
    check(qoiw::common::crc32c(nullptr, 0) == 0, "empty CRC32C differs");
}
}  // namespace

int main() {
    try {
        fixed_width();
        variable_width();
        boundaries();
        checksum();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
