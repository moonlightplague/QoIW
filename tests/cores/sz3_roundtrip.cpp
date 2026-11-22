#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>

#include "qoiw/core.hpp"

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
}  // namespace

int main() {
    try {
        rejects([] { get_core("unknown"); });
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
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
