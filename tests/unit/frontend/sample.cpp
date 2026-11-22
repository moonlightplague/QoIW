#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "frontend/sample.hpp"

namespace {
using namespace qoiw::frontend;
using qoiw::DType;
using Shape = std::vector<std::uint64_t>;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void default_layouts() {
    struct Case {
        Shape field, block;
        std::size_t count;
    };
    const Case cases[] = {
        {{10000000}, {256411}, 5},
        {{1024, 1024}, {128, 128}, 12},
        {{8192, 8192}, {235, 235}, 13},
        {{16384, 32768}, {512, 512}, 21},
        {{100, 500, 500}, {50, 63, 63}, 5},
        {{48, 96, 72, 120}, {24, 24, 24, 24}, 5},
    };
    for (const auto& c : cases) {
        const auto layout = plan_blocks(DType::Float32, c.field);
        check(!layout.whole_input_sampled && layout.dims == c.block && layout.origins.size() == c.count,
              "rank-specific default block shape or count");
    }
}

void halton_origins() {
    // Both terminal tiles are shifted back to stay inside this uneven grid.
    const auto uneven = plan_blocks(DType::Float64, {17, 19}, {.01, 5, 5, 5, 5});
    check(uneven.dims == Shape{5, 5} &&
              uneven.origins == std::vector<std::size_t>{195, 105, 228, 5, 204}, "Halton origins on uneven grid");
    // Block edge 1 makes each C-order offset directly encode its four tile coordinates.
    const auto four = plan_blocks(DType::Float32, {10, 10, 10, 10}, {.01, 1, 1, 1, 4});
    check(four.origins == std::vector<std::size_t>{5321, 2642, 7164, 1485}, "four-dimensional Halton bases");
}

void whole_input_cutoff() {
    const BlockSamplingOptions ten{.01, 1, 1, 10, 10};
    for (std::uint64_t n : {1, 19, 20, 21}) {
        const auto layout = plan_blocks(DType::Float32, {n}, ten);
        check(layout.whole_input_sampled == (n <= 20), "half-input cutoff");
        if (n <= 20) check(layout.dims == Shape{n} && layout.origins == std::vector<std::size_t>{0},
                           "whole input must be a single original-shaped block");
        else check(layout.dims == Shape{1} && layout.origins.size() == 10, "odd-length cutoff");
    }
    const auto huge = std::numeric_limits<std::size_t>::max();
    const auto layout = plan_blocks(DType::Float32, {huge / sizeof(float)}, {1., huge, huge, huge, huge});
    check(layout.whole_input_sampled && layout.origins.size() == 1, "overflow in whole-input cutoff");
}

std::array<std::size_t, 4> padded(const Shape& shape) {
    std::array<std::size_t, 4> dims{1, 1, 1, 1};
    if (shape.empty() || shape.size() > dims.size()) throw std::runtime_error("unexpected sample rank");
    for (std::size_t i = 0; i < shape.size(); ++i) dims[dims.size() - shape.size() + i] = shape[i];
    return dims;
}

template <class T>
void copied_blocks(const Shape& shape, bool whole) {
    const auto dtype = std::is_same_v<T, float> ? DType::Float32 : DType::Float64;
    const auto dims = padded(shape);
    const auto elems = dims[0] * dims[1] * dims[2] * dims[3];
    std::vector<T> original(elems);
    using Word = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
    const Word nan_bits = sizeof(T) == 4 ? 0x7fc00000u : 0x7ff8000000000000ull;
    for (std::size_t i = 0; i < elems; ++i) {
        if (i % 7 == 0) {
            const Word bits = nan_bits | static_cast<Word>(i);
            std::memcpy(&original[i], &bits, sizeof(T));
        } else {
            original[i] = i % 11 == 0 ? -T{0} : static_cast<T>(i);
        }
    }
    for (int threads : {1, 4}) {
        auto input = original;
        const BlockSamplingOptions options = whole ? BlockSamplingOptions{} : BlockSamplingOptions{.01, 3, 3, 2, 4};
        const auto sample = sample_blocks(dtype, input.data(), elems * sizeof(T), shape, threads, options);
        check(std::memcmp(input.data(), original.data(), elems * sizeof(T)) == 0, "sampling changed the input");
        std::fill(input.begin(), input.end(), T{1});
        const auto& blocks = std::get<std::vector<std::vector<T>>>(sample.blocks);
        const auto& layout = sample.layout;
        check(layout.whole_input_sampled == whole && blocks.size() == layout.origins.size(), "sample ownership or count");
        check(layout.dims.size() == shape.size(), "sample rank");
        const auto b = padded(layout.dims);
        for (std::size_t k = 0; k < blocks.size(); ++k) {
            const auto& block = blocks[k];
            check(block.size() == b[0] * b[1] * b[2] * b[3], "sample block length");
            std::size_t cursor = 0;
            for (std::size_t i = 0; i < b[0]; ++i)
                for (std::size_t j = 0; j < b[1]; ++j)
                    for (std::size_t z = 0; z < b[2]; ++z) {
                        const auto offset = layout.origins[k] + ((i * dims[1] + j) * dims[2] + z) * dims[3];
                        check(std::memcmp(block.data() + cursor, original.data() + offset, b[3] * sizeof(T)) == 0,
                              "sample differs from its original C-order row");
                        cursor += b[3];
                    }
        }
    }
}

void invalid_inputs() {
    auto rejects = [](DType dtype, const Shape& dims, double rate) {
        try {
            plan_blocks(dtype, dims, {rate});
        } catch (const std::invalid_argument&) {
            return;
        }
        throw std::runtime_error("invalid block geometry accepted");
    };
    for (const Shape& shape : {Shape{}, {2, 0}, {1, 1, 1, 1, 1}, {std::numeric_limits<std::uint64_t>::max(), 2}})
        rejects(DType::Float32, shape, .01);
    rejects(static_cast<DType>(0), {1, 2}, .01);
    for (double rate : {0., -.1, 1.1, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
        rejects(DType::Float64, {101}, rate);

    const float field[]{1, 2};
    auto bad_field = [&](const void* data, std::size_t bytes, int threads) {
        try {
            sample_blocks(DType::Float32, data, bytes, {2}, threads);
        } catch (const std::invalid_argument&) {
            return;
        }
        throw std::runtime_error("invalid block input accepted");
    };
    bad_field(nullptr, sizeof(field), 1);
    bad_field(field, sizeof(float), 1);
    bad_field(field, 2 * sizeof(field), 1);
    bad_field(field, sizeof(field), -1);
}
}  // namespace

int main() {
    try {
        default_layouts();
        halton_origins();
        whole_input_cutoff();
        for (const Shape& dims : {Shape{37}, {17, 19}, {9, 10, 11}, {7, 8, 9, 10}, {19, 21, 2, 3}, {1, 17, 1, 19}}) {
            copied_blocks<float>(dims, false);
            copied_blocks<double>(dims, false);
        }
        copied_blocks<float>({2, 3, 2, 2}, true);
        copied_blocks<double>({2, 3, 2, 2}, true);
        invalid_inputs();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
