#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "backend/pointwise.hpp"

namespace {
using qoiw::DType;
using qoiw::Expr;
using qoiw::ResolvedPointRequest;
using qoiw::backend::correct_pointwise;

template <class T>
void check_case(const std::vector<T>& original, std::vector<T> decoded, const ResolvedPointRequest& request,
                const std::vector<std::size_t>& expected_indices) {
    const auto bytes = original.size() * sizeof(T);
    auto expected = decoded;
    for (auto i : expected_indices) std::memcpy(&expected.at(i), &original.at(i), sizeof(T));
    const auto dtype = std::is_same_v<T, float> ? DType::Float32 : DType::Float64;
    auto readback = decoded;
    const auto result = correct_pointwise(dtype, original.data(), decoded.data(), bytes, request, 1);
    if (result.restored != expected_indices.size() || result.lossless.empty() != expected_indices.empty() ||
        std::memcmp(decoded.data(), expected.data(), bytes) != 0) throw std::runtime_error("wrong point corrections");
    const auto checked = correct_pointwise(dtype, original.data(), readback.data(), bytes, request, 1, true);
    if (checked.restored != result.restored || checked.lossless != result.lossless ||
        std::memcmp(readback.data(), decoded.data(), bytes) != 0) throw std::runtime_error("patch readback differs");
}

void boundaries() {
    ResolvedPointRequest data_only{{}, 0.5, {}};
    check_case<double>({0, 0, 0}, {0.5, std::nextafter(0.5, 1.), -0.25}, data_only, {1});
    // One third is still in range before the reconstruction rounds to binary32.
    data_only.data_bound_abs = 1. / 3;
    check_case<float>({1}, {static_cast<float>(4. / 3)}, data_only, {0});
    const double max = std::numeric_limits<double>::max();
    data_only.data_bound_abs = max;
    check_case<double>({max}, {-max}, data_only, {0});

    ResolvedPointRequest targets{{{Expr("x^2"), 0.75}, {Expr("x > 0"), 0}}, {}, {}};
    check_case<double>({0.5, -1, 1, 0.5}, {1, 1, 1.5, 0.75}, targets, {1, 2});
    targets.data_bound_abs = 0.25;
    check_case<double>({0.5, -1, 1, 0.5}, {1, 1, 1.5, 0.75}, targets, {0, 1, 2});
    const double above = std::nextafter(0.5, 1.);
    check_case<double>({0, 0}, {0.5, above}, {{{Expr("x"), 0.5}}, {}, {}}, {1});
}

void special_values() {
    const double tiny = 5.5e-14;
    const ResolvedPointRequest logarithm{{{Expr("ln(x)"), 10}}, {}, {}};
    check_case<double>({tiny, -tiny, -0., -2, 1}, {-tiny, -2 * tiny, +0., -1, 2}, logarithm, {0, 1, 2, 3});
    const ResolvedPointRequest exact{{}, {}, Expr("x > 1e30 or x == 0")};
    check_case<float>({1e36f, 1, -0.f, 1e36f, 1}, {1, 1e36f, 0.f, 2e36f, 2}, exact, {0, 1, 2, 3});

    const std::uint64_t bits[] = {0x7ff8000000000042, 0x7ff8000000000001};
    double nan[2];
    std::memcpy(nan, bits, sizeof(bits));
    const auto inf = std::numeric_limits<double>::infinity();
    check_case<double>({nan[0], nan[0], inf, -inf, 1, 1, -0.},
                       {nan[0], nan[1], -inf, -inf, inf, nan[1], +0.}, {}, {1, 2, 4, 5});
    check_case<double>({nan[0], -0.}, {nan[0], -0.}, logarithm, {});
    check_case<double>({-0.}, {+0.}, {{}, 0., {}}, {});
}

void chunk_boundaries() {
    const std::vector<float> original(32769, 1);
    auto raw = original;
    for (auto i : {0, 2047, 2048, 6144, 32768}) raw[i] = -1;
    raw[8191] = 1.125f;
    auto expected = original;
    expected[8191] = raw[8191];
    const ResolvedPointRequest request{{{Expr("x > 0"), 0}}, {}, {}};
    std::vector<std::uint8_t> serial;
    for (int threads : {1, 2, 7, 16, 0}) {
        auto decoded = raw;
        const auto result = correct_pointwise(DType::Float32, original.data(), decoded.data(),
                                              original.size() * sizeof(float), request, threads);
        if (result.restored != 5 || decoded != expected) throw std::runtime_error("wrong cross-chunk corrections");
        if (threads == 1) serial = result.lossless;
        if (result.lossless != serial) throw std::runtime_error("correction order depends on workers");
    }
}

void invalid_arguments() {
    const double original = 0;
    double decoded = 1;
    auto rejects = [&](DType dtype, const void* src, void* dst, std::size_t bytes,
                       const ResolvedPointRequest& request, int threads) {
        try {
            correct_pointwise(dtype, src, dst, bytes, request, threads);
        } catch (const std::invalid_argument&) {
            if (decoded != 1) throw std::runtime_error("invalid request changed output");
            return;
        }
        throw std::runtime_error("invalid request accepted");
    };
    const auto dtype = DType::Float64;
    for (double invalid : {-1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        rejects(dtype, &original, &decoded, 8, {{}, invalid, {}}, 1);
        rejects(dtype, &original, &decoded, 8, {{{Expr("x"), invalid}}, {}, {}}, 1);
    }
    rejects(dtype, nullptr, &decoded, 8, {}, 1);
    rejects(dtype, &original, nullptr, 8, {}, 1);
    rejects(dtype, &original, &decoded, 0, {}, 1);
    rejects(dtype, &original, &decoded, 7, {}, 1);
    rejects(dtype, &original, &decoded, 8, {}, -1);
    rejects(static_cast<DType>(0), &original, &decoded, 8, {}, 1);
}
}  // namespace

int main() {
    try {
        boundaries();
        special_values();
        chunk_boundaries();
        invalid_arguments();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
