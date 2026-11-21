#include <array>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <limits>
#include <type_traits>

#include "common/byte_io.hpp"
#include "container/container.hpp"
#include "core/adapters/mock.hpp"

namespace {
using qoiw::Bytes;
using qoiw::CoreParams;
using qoiw::DType;
using qoiw::test::mock_core;
using Buffer = std::vector<std::uint8_t>;
using Shape = std::vector<std::uint64_t>;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <class Error, class F>
void expect_error(F action, const char* message) {
    try {
        action();
    } catch (const Error&) {
        return;
    }
    throw std::runtime_error(message);
}

Bytes view(const Buffer& bytes) { return {bytes.data(), bytes.size()}; }

template <class T>
constexpr DType dtype = std::is_same_v<T, float> ? DType::Float32 : DType::Float64;

template <class T, std::size_t N>
Buffer pack(std::string_view name, const std::array<T, N>& values, const Shape& dims, const CoreParams& params) {
    const auto& core = mock_core(name);
    check(core.supports(dtype<T>, dims), "unsupported test shape");
    auto payload = core.compress(values.data(), dtype<T>, dims, params);
    qoiw::container::Stream stream;
    stream.dtype = dtype<T>;
    stream.dims = dims;
    stream.wrapper_version = "m0";
    stream.core_name = core.name();
    stream.core_version = core.version();
    stream.core_format_version = core.format_version();
    stream.fields = {{"x", params.bound, std::nullopt, view(payload.decode_params), 0, view(payload.stream)}};
    return write(stream);
}

template <class T, std::size_t N>
std::array<T, N> unpack(const Buffer& bytes, int threads) {
    const auto stream = qoiw::container::read(view(bytes));
    const auto& core = mock_core(stream.core_name);
    check(stream.dtype == dtype<T> && stream.fields.size() == 1, "unexpected output type or field count");
    check(core.format_version() == stream.core_format_version, "unexpected core format");
    const auto& field = stream.fields.front();
    std::array<T, N> output{};
    core.decompress(field.core_stream, field.decode_params, stream.dtype, stream.dims, threads,
                    output.data(), sizeof(output));
    return output;
}

template <class T, std::size_t N>
bool same_bits(const std::array<T, N>& a, const std::array<T, N>& b) {
    return std::memcmp(a.data(), b.data(), N * sizeof(T)) == 0;
}

template <class T>
void round_trips() {
    const std::array<T, 4> original{-1.25, -0.25, 0, 0.75};
    const auto nan = std::numeric_limits<T>::quiet_NaN();
    struct Case {
        std::string_view name;
        std::array<T, 4> expected;
    };
    const Case cases[] = {
        {"mock:identity", original}, {"mock:quantizer", {-1.5, -0.5, 0, 1}},
        {"mock:noise", {-0.5, -1, 0.75, 0}}, {"mock:garbage", {1, -1, 1, -1}},
        {"mock:signflip", {-1.25, 0.25, -0.0, 0.75}}, {"mock:nan", {nan, -0.25, nan, 0.75}},
    };
    for (const auto& c : cases) {
        CoreParams params;
        params.bound = .25;
        if (c.name == "mock:noise") params.options["factor"] = "3";
        for (const Shape& dims : {Shape{4}, Shape{1, 4}, Shape{2, 1, 2}, Shape{1, 2, 2, 1}}) {
            auto input = original;
            params.threads = 1;
            const auto reference = pack(c.name, input, dims, params);
            for (int threads : {1, 2, 7, 16}) {
                params.threads = threads;
                check(pack(c.name, input, dims, params) == reference, "stream depends on threads");
                check(same_bits(input, original), "compress modified input");
                check(same_bits(unpack<T, 4>(reference, threads), c.expected), "incorrect mock round trip");
            }
        }
    }
}

void exceptional_values() {
    const std::uint64_t patterns[] = {0x7ff8000000000055, 0xfff0000000000000, 0x8000000000000000,
                                      0x7fefffffffffffff, 0x1};
    std::array<double, 5> values;
    std::memcpy(values.data(), patterns, sizeof(patterns));
    CoreParams params;
    const auto identity = pack("mock:identity", values, {5}, params);
    check(same_bits(values, unpack<double, 5>(identity, 0)), "identity lost IEEE bits");
    const auto exact = pack("mock:quantizer", values, {5}, params);
    check(same_bits(values, unpack<double, 5>(exact, 1)), "zero bound changed data");
    params.bound = std::numeric_limits<double>::denorm_min();
    auto back = unpack<double, 5>(pack("mock:quantizer", values, {5}, params), 1);
    check(std::memcmp(values.data(), back.data(), 4 * sizeof(double)) == 0 && back[4] == 2 * params.bound,
          "quantizer escape or subnormal rounding failed");

    // Index 2 reconstructs to 4/3. Rounding to binary32 would exceed the 1/3 bound.
    const std::array<float, 1> one{1};
    params.bound = 1.0 / 3;
    check(same_bits(one, unpack<float, 1>(pack("mock:quantizer", one, {1}, params), 1)), "float32 rounding escape");
    const std::array<float, 1> large{16777216.f};
    params.bound = 0.51;
    check(same_bits(large, unpack<float, 1>(pack("mock:quantizer", large, {1}, params), 1)), "large-index escape");
}

void invalid_inputs() {
    const auto& core = mock_core("mock:quantizer");
    float value = 0;
    CoreParams params;
    for (const Shape& dims : {Shape{}, Shape{0}, Shape{UINT64_MAX}, Shape{1, 1, 1, 1, 1}}) {
        check(!core.supports(DType::Float32, dims), "accepted invalid shape");
        expect_error<std::invalid_argument>([&] { core.compress(&value, DType::Float32, dims, params); }, "shape");
    }
    params.bound = -1;
    expect_error<std::invalid_argument>([&] { core.compress(&value, DType::Float32, {1}, params); }, "negative bound");
    params.bound = .5;
    auto encoded = core.compress(&value, DType::Float32, {1}, params);
    auto decode = [&](Bytes payload, Bytes metadata, std::size_t size) {
        core.decompress(payload, metadata, DType::Float32, {1}, 1, &value, size);
    };
    using qoiw::common::CorruptStream;
    expect_error<std::invalid_argument>([&] { decode(view(encoded.stream), view(encoded.decode_params), 3); },
                                        "undersized output accepted");
    expect_error<CorruptStream>([&] { decode(view(encoded.stream), {}, 4); }, "missing step accepted");
    expect_error<CorruptStream>([&] { decode({}, view(encoded.decode_params), 4); }, "missing code accepted");
    expect_error<CorruptStream>([&] { decode(view(Buffer{0, 0}), view(encoded.decode_params), 4); }, "missing escape");
    Buffer invalid_step(8, 0xff);
    expect_error<CorruptStream>([&] { decode(view(encoded.stream), view(invalid_step), 4); }, "NaN step accepted");
    invalid_step.assign(8, 0);
    expect_error<CorruptStream>([&] { decode(view(encoded.stream), view(invalid_step), 4); }, "index at zero step");
    value = 23;
    encoded.stream.push_back(0);
    expect_error<CorruptStream>([&] { decode(view(encoded.stream), view(encoded.decode_params), 4); }, "trailing bytes");
    check(value == 23, "malformed payload changed output");
    expect_error<CorruptStream>([&] { mock_core("mock:identity").decompress({}, {}, DType::Float32, {1}, 1, &value, 4); },
                                "short raw stream accepted");
    for (const char* factor : {"1", "-2", "nan", "3x"}) {
        params.options["factor"] = factor;
        expect_error<std::invalid_argument>([&] { mock_core("mock:noise").compress(&value, DType::Float32, {1}, params); },
                                            "invalid noise factor accepted");
    }
}

void golden() {
    std::ifstream file(QOIW_MOCK_GOLDEN, std::ios::binary);
    check(file.good(), "cannot open mock quantizer golden stream");
    const Buffer bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const std::array<float, 4> original{-1.25, -0.25, 0, 0.75};
    const std::array<float, 4> expected{-1.5, -0.5, 0, 1};
    check(same_bits(unpack<float, 4>(bytes, 1), expected), "golden decode");
    CoreParams params;
    params.bound = .25;
    check(pack("mock:quantizer", original, {1, 4}, params) == bytes, "golden encoding");
}
}  // namespace

int main() {
    try {
        // Share the immutable adapters between simultaneous independent streams.
        auto floats = std::async(std::launch::async, round_trips<float>);
        round_trips<double>();
        floats.get();
        golden();
        exceptional_values();
        invalid_inputs();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
