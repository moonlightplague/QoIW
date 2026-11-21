#include <array>
#include <iostream>
#include <numeric>

#include "backend/lossless.hpp"
#include "common/byte_io.hpp"

namespace {
using qoiw::Bytes;
using qoiw::DType;
using qoiw::backend::apply_lossless;
using qoiw::backend::encode_lossless;
using Buffer = std::vector<std::uint8_t>;

Bytes view(const Buffer& buffer) { return {buffer.data(), buffer.size()}; }

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Independent wire fixture: one short zstd frame with a single uncompressed block.
Buffer frame(const Buffer& raw) {
    check(raw.size() < 32, "fixture is too large");
    Buffer bytes{0x28, 0xb5, 0x2f, 0xfd, 0x20, static_cast<std::uint8_t>(raw.size()),
                 static_cast<std::uint8_t>(raw.size() * 8 + 1), 0, 0};
    bytes.insert(bytes.end(), raw.begin(), raw.end());
    return bytes;
}

Buffer layer(std::uint64_t count, const Buffer& gaps, const Buffer& raw) {
    Buffer payload;
    qoiw::common::ByteWriter writer(payload);
    writer.u64(count);
    auto gap_frame = frame(gaps);
    auto value_frame = frame(raw);
    writer.u64(gap_frame.size());
    writer.bytes(gap_frame.data(), gap_frame.size());
    writer.bytes(value_frame.data(), value_frame.size());
    return payload;
}

void independent_decode() {
    const auto payload = layer(2, {0, 0x80, 1}, {0, 0, 0, 0x80, 0x53, 0, 0xc0, 0x7f});
    std::array<std::uint32_t, 129> dest;
    dest.fill(0x12345678);
    check(apply_lossless(view(payload), DType::Float32, dest.data(), sizeof(dest)) == 2, "restored count");
    check(dest.front() == 0x80000000 && dest.back() == 0x7fc00053, "signed zero or NaN payload lost");
    for (std::size_t i = 1; i + 1 < dest.size(); ++i) check(dest[i] == 0x12345678, "wrong index restored");
}

void round_trip() {
    constexpr std::size_t count = 8193;
    std::vector<std::uint64_t> source(count, 0x7ff8000000000042);
    source.front() = 0x8000000000000000;
    source.back() = 1;
    std::vector<std::size_t> indices(count);
    std::iota(indices.begin(), indices.end(), 0);
    const auto bytes = count * sizeof(source.front());
    const auto payload = encode_lossless(DType::Float64, source.data(), bytes, indices);
    std::vector<std::uint64_t> dest(count);
    check(apply_lossless(view(payload), DType::Float64, dest.data(), bytes) == count, "dense layer count");
    check(dest == source, "dense float64 layer");
    check(encode_lossless(DType::Float64, source.data(), bytes, {}).empty(), "empty layer encoded");
    check(apply_lossless({}, DType::Float64, dest.data(), bytes) == 0 && dest == source, "empty layer applied");

    for (auto invalid : {std::vector<std::size_t>{2, 1}, {2, 2}, {count}}) {
        try {
            encode_lossless(DType::Float64, source.data(), bytes, invalid);
        } catch (const std::invalid_argument&) {
            continue;
        }
        throw std::runtime_error("invalid indices encoded");
    }
}

void reject(const Buffer& payload) {
    std::array<std::uint64_t, 16> dest{};
    dest.fill(0x1122334455667788);
    const auto before = dest;
    try {
        apply_lossless(view(payload), DType::Float64, dest.data(), sizeof(dest));
    } catch (const qoiw::common::CorruptStream&) {
        check(dest == before, "invalid layer changed the destination");
        return;
    }
    throw std::runtime_error("invalid layer accepted");
}

void malformed() {
    const Buffer values(16);
    reject(layer(0, {}, {}));
    reject(layer(UINT64_MAX, {0, 1}, values));
    reject(layer(2, {1, 0}, values));          // Duplicate second index.
    reject(layer(2, {1, 15}, values));         // Index past the end.
    reject(layer(2, {0, 1, 2}, values));       // Extra gap.
    reject(layer(2, {0, 0x80}, values));       // Incomplete LEB128.
    reject(layer(2, {0, 1}, Buffer(15)));      // Wrong number of value bytes.
    auto valid = layer(2, {0, 1}, values);
    for (std::size_t n = 1; n < valid.size(); ++n) reject(Buffer(valid.begin(), valid.begin() + n));
    valid.push_back(0);
    reject(valid);
}
}  // namespace

int main() {
    try {
        independent_decode();
        round_trip();
        malformed();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
