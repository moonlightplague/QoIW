#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <stdexcept>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "common/byte_io.hpp"
#include "qoiw/core.hpp"
#include "qoiw/qoiw.hpp"

namespace {
using namespace qoiw;
using Shape = std::vector<std::uint64_t>;
using Buffer = std::vector<std::uint8_t>;

void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

template <class F>
void rejects(F call) {
    try {
        call();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("invalid SZ3 call succeeded");
}

Bytes view(const Buffer& b) { return {b.data(), b.size()}; }

template <class T>
void round_trip(const Core& core, const Shape& dims, const std::vector<T>& input, CoreParams params) {
    const auto dtype = sizeof(T) == 4 ? DType::Float32 : DType::Float64;
    check(core.supports(dtype, dims), "unsupported rank or dtype");
    const auto copy = input;
    const auto bytes = input.size() * sizeof(T);
    const auto payload = core.compress(input.data(), dtype, dims, params);
    check(!payload.stream.empty() && payload.decode_params.empty(), "unexpected SZ3 payload");
    check(!std::memcmp(copy.data(), input.data(), bytes), "SZ3 modified input");
    std::vector<T> back(input.size() + 1, 123), second(input.size());
    core.decompress(view(payload.stream), {}, dtype, dims, 1, back.data(), bytes);
    core.decompress(view(payload.stream), {}, dtype, dims, 4, second.data(), bytes);
    check(back.back() == 123 && !std::memcmp(second.data(), back.data(), bytes), "SZ3 decode extent or determinism");
    if (params.bound == 0) {
        check(!std::memcmp(copy.data(), back.data(), bytes), "zero bound changed storage bits");
    } else {
        for (std::size_t i = 0; i < input.size(); ++i)
            check(std::abs(static_cast<double>(back[i]) - copy[i]) <= params.bound, "SZ3 exceeded bound");
    }
}

template <class T>
void matrix(const Core& core) {
    for (const Shape& dims : {Shape{1}, Shape{1, 1, 1, 1}, Shape{257}, Shape{17, 31},
                             Shape{9, 11, 7}, Shape{5, 7, 3, 9}, Shape{1, 13, 1, 9}}) {
        const auto n = std::accumulate(dims.begin(), dims.end(), std::size_t{1}, std::multiplies<>{});
        std::vector<T> data(n);
        for (std::size_t i = 0; i < n; ++i) data[i] = std::sin(.02 * i) + .001 * (i % 43);
        for (const auto* algo : {"interp_lorenzo", "interp", "lorenzo_reg", "nopred"}) {
            CoreParams params;
            params.bound = .013;
            params.options["algo"] = algo;
            round_trip(core, dims, data, params);
        }
    }
    round_trip<T>(core, {100}, std::vector<T>(100, 8), {.1, 1, false, {}});
    round_trip<T>(core, {5}, {-T{0}, std::numeric_limits<T>::quiet_NaN(), std::numeric_limits<T>::infinity(),
                              -std::numeric_limits<T>::infinity(), std::numeric_limits<T>::denorm_min()}, {});
}

void check_arguments(const Core& core) {
    const auto type = DType::Float32;
    const Shape dims{4, 8};
    std::vector<float> data(32, 7), out(32, -123);
    for (const auto& bad : {Shape{}, Shape{0}, Shape{1, 1, 1, 1, 1}, Shape{UINT64_MAX, 2}})
        check(!core.supports(type, bad), "invalid dimensions accepted");
    check(!core.supports(static_cast<DType>(0), dims), "invalid type accepted");
    for (const auto& params : {CoreParams{-1, 1, false, {}}, CoreParams{NAN, 1, false, {}},
                               CoreParams{INFINITY, 1, false, {}}, CoreParams{1, -1, false, {}},
                               CoreParams{1, 1, false, {{"algo", "other"}}}, CoreParams{1, 1, false, {{"other", "1"}}}})
        rejects([&] { core.compress(data.data(), type, dims, params); });
    rejects([&] { core.compress(nullptr, type, dims, {}); });
    const auto payload = core.compress(data.data(), type, dims, {});
    const auto s = view(payload.stream);
    const auto bytes = out.size() * sizeof(float);
    rejects([&] { core.decompress(s, {}, type, dims, 1, out.data(), bytes - 1); });
    rejects([&] { core.decompress(s, {}, type, {8, 4}, 1, out.data(), bytes); });
    rejects([&] { core.decompress(s, {}, type, {4}, 1, out.data(), bytes); });
    std::vector<double> wide(32, -123);
    rejects([&] { core.decompress(s, {}, DType::Float64, dims, 1, wide.data(), wide.size() * sizeof(double)); });
    rejects([&] { core.decompress({s.data, s.size - 1}, {}, type, dims, 1, out.data(), bytes); });
    rejects([&] { core.decompress(s, {s.data, 1}, type, dims, 1, out.data(), bytes); });
    check(out == std::vector<float>(32, -123) && wide == std::vector<double>(32, -123),
          "SZ3 wrote output before validating the layout");
}

void thread_modes(const Core& core) {
    const Shape shape{128, 47};
    std::vector<float> data(128 * 47);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = std::sin(.021 * i);
    auto encode = [&](int threads, bool chunked) {
        return core.compress(data.data(), DType::Float32, shape, {.01, threads, chunked, {}}).stream;
    };
    const auto serial = encode(1, false);
    check(encode(4, false) == serial && encode(0, false) == serial && encode(1, true) == serial,
          "thread count alone changed SZ3's stream");
#ifdef _OPENMP
    const int previous_threads = omp_get_max_threads(), previous_levels = omp_get_max_active_levels();
#endif
    const auto two = encode(2, true), four = encode(4, true);
#ifdef _OPENMP
    auto chunks = [](const Buffer& bytes) {
        common::ByteReader reader(bytes.data(), bytes.size());
        reader.take(8);
        return reader.take(reader.u64()).u32();
    };
    check(chunks(two) == 2 && chunks(four) == 4 && serial != two, "opt-in SZ3 chunk count");
    check(encode(0, true) == encode(previous_threads, true), "default thread request");
#else
    check(two == serial && four == serial, "serial-only build generated chunked output");
#endif
    auto other = std::async(std::launch::async, [&] { return encode(2, true); });
    check(encode(4, true) == four && other.get() == two, "concurrent SZ3 calls interfered");

    const auto bytes = data.size() * sizeof(float);
    std::vector<float> reference(data.size()), out(data.size());
    core.decompress(view(four), {}, DType::Float32, shape, 1, reference.data(), bytes);
    for (std::size_t i = 0; i < data.size(); ++i)
        check(std::abs(static_cast<double>(reference[i]) - data[i]) <= .01, "chunked SZ3 exceeded bound");
    for (int workers : {0, 2, 4}) {
        std::fill(out.begin(), out.end(), 17);
        core.decompress(view(four), {}, DType::Float32, shape, workers, out.data(), bytes);
        check(!std::memcmp(out.data(), reference.data(), bytes), "chunked decode depends on thread count");
    }
#ifdef _OPENMP
    check(previous_threads == omp_get_max_threads() && previous_levels == omp_get_max_active_levels(),
          "SZ3 changed caller's OpenMP settings");
#endif
}

void chunked_golden(bool sz3_enabled) {
    // SZ3 3.4.0, algo=nopred, two chunks. Each original is an integer plus
    // (0, .125), so a step of .25 reconstructs that integer before correction.
    std::ifstream file(QOIW_SZ3_GOLDEN, std::ios::binary);
    check(file.good(), "missing SZ3 golden stream");
    const Buffer blob{std::istreambuf_iterator<char>(file), {}};
    const auto stream = inspect(blob);
    check(stream.dims == Shape{16, 32} && stream.dtype == DType::Float32 && stream.core_name == "sz3" &&
          stream.core_version == "3.4.0" && stream.core_format_version == "3.4.0" &&
          stream.num == 512 && stream.fields.at(0).bound == .125 && !stream.fields[0].decode_params_bytes &&
          stream.targets.at(0).expression == "x^2" && stream.targets[0].tolerance_abs == 1, "SZ3 golden metadata");
    std::vector<float> expected(512), out(512);
    std::uint32_t state = 1;
    for (auto& y : expected) {
        state = 1664525u * state + 1013904223u;
        const int bucket = static_cast<int>((state >> 19) % 17) - 8;
        const double x = bucket + (1 + ((state >> 9) % 127)) / 1024.;
        y = static_cast<float>(std::abs(x * x - bucket * bucket) > 1 ? x : bucket);
    }
    if (!sz3_enabled) {
        try {
            decompress(blob, out.data(), out.size());
        } catch (const Error& error) {
            if (error.code() == ErrorCode::Unsupported) return;
            throw;
        }
        throw std::runtime_error("decoded SZ3 stream in a build without SZ3");
    }
    for (int threads : {1, 2, 7}) {
        std::fill(out.begin(), out.end(), 17);
        const auto bytes = out.size() * sizeof(float);
        decompress(blob, out.data(), out.size(), threads);
        check(!std::memcmp(expected.data(), out.data(), bytes), "chunked golden decode");
    }
}
}  // namespace

int main() {
    try {
        rejects([] { get_core("unknown"); });
        chunked_golden(EXPECT_SZ3);
        if (!EXPECT_SZ3) {
            rejects([] { get_core("sz3"); });
            check(available_cores().empty(), "SZ3 listed in disabled build");
            return 0;
        }
        const auto& core = get_core("sz3");
        check(available_cores() == std::vector<std::string>{"sz3"} && core.name() == "sz3" &&
              !core.version().empty() && !core.format_version().empty(), "unexpected built-in cores or version");
        auto traits = core.traits(DType::Float32, 3);
        check(traits.bound_kind == BoundKind::StrictAbs && traits.thread_safe && !traits.fixed_call_overhead,
              "unexpected SZ3 traits");
        matrix<float>(core);
        matrix<double>(core);
        check_arguments(core);
        thread_modes(core);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
