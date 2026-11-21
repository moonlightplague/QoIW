#include "fixture.hpp"

#include <iostream>

#include "common/byte_io.hpp"

namespace {
using fixture::Buffer;
using fixture::check;
using fixture::expect_error;
using qoiw::common::CorruptStream;
using qoiw::container::UnsupportedFormat;

// Offsets refer to the independently packed fixture, including its 12-byte preamble.
constexpr std::size_t kBody = 12;
constexpr std::size_t kBound = kBody + 42;
constexpr std::size_t kCore = 92;
constexpr std::size_t kEnd = kCore + 15;

void golden(const Buffer& bytes) {
    auto stream = fixture::read(bytes);
    check(stream.dtype == qoiw::DType::Float32 && stream.dims == std::vector<std::uint64_t>{1, 3}, "shape");
    check(stream.wrapper_version == "m0" && stream.core_name == "fixture" &&
              stream.core_version == "1" && stream.core_format_version == "0", "versions");
    check(stream.fields.size() == 1 && stream.exact_condition.empty(), "request");
    const auto& field = stream.fields[0];
    check(field.name == "x" && field.bound == .25 && field.data_bound_abs == .5, "field bounds");
    check(field.fingerprint == 0x123456789abcdef0ULL, "fingerprint");
    check(field.decode_params.size == 2 && field.decode_params.data[0] == 255 && field.decode_params.data[1] == 0,
          "decode parameters");
    check(field.core_stream.data == bytes.data() + kCore + 12 && field.core_stream.size == 3, "borrowed CORE range");
    check(field.core_stream.data[0] == 0xaa && field.core_stream.data[1] == 0 && field.core_stream.data[2] == 0xbb,
          "opaque payload");
}

void repair_crc(Buffer& bytes) {
    const auto crc = qoiw::common::crc32c(bytes.data() + kBody, kCore - 4 - kBody);
    for (unsigned i = 0; i < 4; ++i) bytes[kCore - 4 + i] = static_cast<std::uint8_t>(crc >> (8 * i));
}

template <class Error = CorruptStream>
void reject_byte(const Buffer& golden_bytes, std::size_t index, std::uint8_t value, bool update_crc = false) {
    auto bytes = golden_bytes;
    bytes.at(index) = value;
    if (update_crc) repair_crc(bytes);
    expect_error<Error>([&] { fixture::read(bytes); }, "malformed fixture accepted");
}

void header_errors(const Buffer& bytes) {
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        expect_error<CorruptStream>([&] { qoiw::container::read({bytes.data(), length}); }, "truncated stream accepted");
    }
    reject_byte(bytes, 0, 'Z');
    reject_byte<UnsupportedFormat>(bytes, 4, 1);
    reject_byte<UnsupportedFormat>(bytes, 6, 1);
    reject_byte(bytes, 11, 255);  // Declared header length exceeds input.
    reject_byte(bytes, kCore - 1, 255);  // CRC alone changed.
    reject_byte(bytes, kBody, 5, true);
    reject_byte(bytes, kBody + 1, 5, true);
    reject_byte(bytes, kBody + 2, 0, true);
    reject_byte(bytes, kBody + 4, 0, true);  // Zero extent.
    reject_byte(bytes, kBody + 11, 255, true);  // Decoded byte count would overflow.
    reject_byte(bytes, kBody + 21, 255, true);  // String length exceeds header body.
    reject_byte(bytes, kBound + 7, 0xbf, true);  // Negative core bound.
    reject_byte<UnsupportedFormat>(bytes, kBound + 16, 2, true);
    reject_byte(bytes, kBound + 20, 255, true);  // Decode-params length exceeds body.
    reject_byte<UnsupportedFormat>(bytes, kCore - 7, 1, true);  // Target records require a later reader.
}

void sections(const Buffer& golden_bytes) {
    reject_byte(golden_bytes, kCore + 1, 1);
    reject_byte<UnsupportedFormat>(golden_bytes, kCore + 2, 1);
    reject_byte(golden_bytes, kCore + 11, 255);
    reject_byte(golden_bytes, kEnd + 1, 1);
    reject_byte(golden_bytes, kEnd + 2, 1);
    auto bytes = golden_bytes;
    bytes.push_back(0);
    expect_error<CorruptStream>([&] { fixture::read(bytes); }, "trailing byte accepted");
    bytes = golden_bytes;
    bytes.erase(bytes.begin() + kCore, bytes.begin() + kEnd);
    expect_error<CorruptStream>([&] { fixture::read(bytes); }, "missing CORE accepted");
    bytes = golden_bytes;
    bytes.insert(bytes.begin() + kEnd, golden_bytes.begin() + kCore, golden_bytes.begin() + kEnd);
    expect_error<CorruptStream>([&] { fixture::read(bytes); }, "duplicate CORE accepted");

    bytes[kEnd] = 0x11;  // A required, field-specific LOSSLESS payload.
    const auto layer = fixture::read(bytes).fields[0].lossless;
    check(layer.data == bytes.data() + kEnd + 12 && layer.size == 3, "borrowed LOSSLESS range");
    reject_byte(bytes, kEnd + 1, 1);
    reject_byte<UnsupportedFormat>(bytes, kEnd + 2, 1);
    auto duplicate = bytes;
    duplicate.insert(duplicate.begin() + kEnd, bytes.begin() + kEnd, bytes.begin() + kEnd + 15);
    expect_error<CorruptStream>([&] { fixture::read(duplicate); }, "duplicate LOSSLESS accepted");
    bytes[kEnd] = 0x7e;  // Known informational NOTE.
    fixture::read(bytes);
    bytes[kEnd] = 0x81;
    expect_error<UnsupportedFormat>([&] { fixture::read(bytes); }, "unknown required section accepted");
    bytes[kEnd + 2] = 1;  // Future skippable section in a newer minor version.
    bytes[5] = 1;
    auto stream = fixture::read(bytes);
    check(stream.fields[0].core_stream.size == 3, "failed to skip extension");
    for (std::uint8_t type : {0x10, 0x11, 0x12, 0x20}) {
        bytes[kEnd] = type;
        expect_error<UnsupportedFormat>([&] { fixture::read(bytes); }, "required layer skipped");
    }
}
}  // namespace

int main() {
    try {
        auto bytes = fixture::load();
        check(bytes.size() == kEnd + 12, "unexpected golden fixture length");
        golden(bytes);
        header_errors(bytes);
        sections(bytes);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
