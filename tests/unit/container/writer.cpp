#include "fixture.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <string>

namespace {
using fixture::check;
using fixture::expect_error;
using qoiw::container::Field;
using qoiw::container::Stream;

Stream sample() {
    static constexpr std::uint8_t params[] = {0xff, 0};
    static constexpr std::uint8_t payload[] = {0xaa, 0, 0xbb};
    Stream stream;
    stream.dtype = qoiw::DType::Float32;
    stream.dims = {1, 3};
    stream.wrapper_version = "m0";
    stream.core_name = "fixture";
    stream.core_version = "1";
    stream.core_format_version = "0";
    stream.fields = {{"x", 0.25, 0.5, {params, sizeof(params)}, 0x123456789abcdef0ULL, {payload, sizeof(payload)}}};
    return stream;
}

void round_trip() {
    auto stream = sample();
    check(write(stream) == fixture::load(), "writer differs from M0 golden stream");

    stream.dtype = qoiw::DType::Float64;
    stream.dims = {2, 1, 3, 4};
    stream.exact_condition = "x > 100";
    stream.fields.push_back(Field{});
    stream.fields.back().name = "v";
    stream.fields.front().data_bound_abs = 0;
    const auto bytes = write(stream);
    auto back = fixture::read(bytes);
    check(back.dtype == stream.dtype && back.dims == stream.dims, "multi-field shape");
    check(back.exact_condition == stream.exact_condition && back.fields.size() == 2, "multi-field metadata");
    check(back.fields[0].data_bound_abs == 0 && !back.fields[1].data_bound_abs, "absent versus zero bound");
    check(back.fields[1].core_stream.size == 0 && back.fields[1].name == "v", "empty CORE in second field");
    check(write(back) == bytes, "non-deterministic re-encoding");

    // A decoder locates payloads by field ID even when a producer emits them in a different order.
    auto reordered = bytes;
    const auto end = reordered.end() - 12;
    const auto second = end - 12;        // Empty CORE: 12-byte section header.
    const auto first = second - 15;      // Three payload bytes.
    std::rotate(first, second, end);
    auto shuffled = fixture::read(reordered);
    check(write(shuffled) == bytes, "CORE payload assigned to wrong field");

    stream.fields.resize(UINT8_MAX);
    const auto max_fields = write(stream);
    check(fixture::read(max_fields).fields.size() == UINT8_MAX, "maximum field count");
}

template <class Edit>
void invalid(Edit edit, const char* message) {
    auto stream = sample();
    edit(stream);
    expect_error<std::invalid_argument>([&] { write(stream); }, message);
}

void reordered_corrections() {
    auto stream = sample();
    static constexpr std::uint8_t layer[] = {0x51, 0x52};
    stream.fields.push_back(Field{});
    stream.fields[1].lossless = {layer, sizeof(layer)};
    const auto bytes = write(stream);
    auto reordered = bytes;
    auto end = reordered.end() - 12;
    auto correction = end - 14;
    // Move field 1's LOSSLESS ahead of both CORE sections.
    std::rotate(correction - 12 - 15, correction, end);
    auto back = fixture::read(reordered);
    check(back.fields[0].lossless.size == 0 && back.fields[1].lossless.size == sizeof(layer),
          "LOSSLESS assigned to wrong field");
    check(write(back) == bytes, "reordered LOSSLESS changed payload");
}

void invalid_inputs() {
    invalid([](Stream& s) { s.dims = {}; }, "empty dimensions accepted");
    invalid([](Stream& s) { s.dims = {UINT64_MAX}; }, "array byte count overflow accepted");
    invalid([](Stream& s) { s.fields.clear(); }, "empty field list accepted");
    invalid([](Stream& s) { s.fields.resize(256); }, "field count overflow accepted");
    invalid([](Stream& s) { s.core_name = ""; }, "missing core name accepted");
    invalid([](Stream& s) { s.fields[0].bound = -1; }, "negative bound accepted");
    invalid([](Stream& s) { s.fields[0].data_bound_abs = std::numeric_limits<double>::quiet_NaN(); },
            "NaN data bound accepted");
    invalid([](Stream& s) { s.fields[0].decode_params.size = UINT32_MAX; }, "header size overflow accepted");
    invalid([](Stream& s) { s.fields[0].core_stream = {nullptr, 1}; }, "invalid borrowed payload accepted");
    invalid([](Stream& s) { s.fields[0].lossless = {nullptr, 1}; }, "null LOSSLESS payload accepted");
    auto oversized = sample();
    oversized.fields[0].core_stream.size = std::numeric_limits<std::size_t>::max();
    expect_error<std::length_error>([&] { write(oversized); }, "total stream size overflow accepted");
    oversized = sample();
    oversized.fields[0].lossless = oversized.fields[0].core_stream;
    oversized.fields[0].lossless.size = std::numeric_limits<std::size_t>::max();
    expect_error<std::length_error>([&] { write(oversized); }, "LOSSLESS size overflow accepted");
    std::string too_long(65536, 'a');
    invalid([&](Stream& s) { s.fields[0].name = too_long; }, "unrepresentable string length accepted");
}
}  // namespace

int main() {
    try {
        round_trip();
        reordered_corrections();
        invalid_inputs();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
