#include <iostream>
#include <limits>
#include <stdexcept>

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
}
}  // namespace

int main() {
    try {
        default_layouts();
        halton_origins();
        whole_input_cutoff();
        invalid_inputs();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
