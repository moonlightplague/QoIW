#pragma once

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

#include "container/container.hpp"

namespace fixture {
using Buffer = std::vector<std::uint8_t>;

inline Buffer load() {
    std::ifstream input(QOIW_GOLDEN_FILE, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open M0 golden stream");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

inline void check(bool condition, const char* message) {
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

inline qoiw::container::Stream read(const Buffer& input) {
    return qoiw::container::read({input.data(), input.size()});
}
}  // namespace fixture
