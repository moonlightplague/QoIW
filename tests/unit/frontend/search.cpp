#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "frontend/search.hpp"

namespace {
using namespace qoiw::frontend;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

CandidateStats score(double cost, std::size_t restored = 1) {
    CandidateStats stats;
    stats.bytes_per_value = cost;
    stats.restored = restored;
    return stats;
}

void refinement() {
    std::vector<double> calls;
    auto curve = [&](double bound) {
        check(std::find(calls.begin(), calls.end(), bound) == calls.end(), "repeated measurement");
        calls.push_back(bound);
        return score(std::abs(std::log2(bound) - 3.));
    };
    // Seeds 1, 4, 64 start at 4. A flat first round narrows both sides.
    // The second right midpoint is the exact minimum at 8.
    const auto result = search_pointwise_bounds({64, 0, 4, 1, 64}, curve, {2});
    check(calls == std::vector<double>{1, 4, 64, 2, 16, std::sqrt(8.), 8}, "refinement order");
    check(result.bound == 8 && result.trace.size() == calls.size(), "missed geometric minimum");
    for (std::size_t i = 0; i < calls.size(); ++i)
        check(result.trace[i].bound == calls[i], "trace differs from measurement order");

    for (double optimum : {2., 16.}) {
        const auto move = search_pointwise_bounds({1, 4, 64},
            [=](double bound) { return score(std::abs(std::log2(bound / optimum))); });
        check(move.bound == optimum, "failed to move to cheaper midpoint");
    }
    const auto endpoint = search_pointwise_bounds({4, 64},
        [](double bound) { return score(std::abs(std::log2(bound / 32.))); });
    check(endpoint.bound == 32., "failed one-sided refinement");
    const auto narrow = search_pointwise_bounds({1, 1.02, 1.05}, [](double bound) { return score(bound); });
    check(narrow.trace.size() == 3 && narrow.bound == 1, "refined narrow bracket");
    const auto no_refine = search_pointwise_bounds({4, 64}, [](double) { return score(1); }, {0});
    check(no_refine.trace.size() == 2 && no_refine.bound == 4, "zero refinement rounds");
}

void numeric_extremes() {
    for (int exponent : {-1074, -900, 900}) {
        const auto low = std::ldexp(1., exponent);
        const auto mid = 4 * low;
        const auto high = 16 * low;
        const auto result = search_pointwise_bounds({low, high}, [=](double bound) {
            check(bound >= low && bound <= high, "midpoint escaped bracket");
            return score(bound == mid ? 0 : 1);
        }, {1});
        check(result.bound == mid && result.trace.size() == 3, "overflow or underflow in midpoint");
    }
    const double tiny = std::numeric_limits<double>::denorm_min();
    const auto adjacent = search_pointwise_bounds({tiny, 2 * tiny}, [](double) { return score(1); }, {100});
    check(adjacent.trace.size() == 2, "adjacent subnormals have no interior midpoint");
}

template <class Work>
void rejects(Work work) {
    try {
        work();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("invalid search input accepted");
}

void invalid_inputs() {
    auto unit = [](double) { return score(1); };
    for (double invalid : {-1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        rejects([&] { search_pointwise_bounds({1., invalid}, unit); });
        rejects([&] { search_pointwise_bounds({1.}, [=](double) { return score(invalid); }); });
    }
    rejects([&] { search_pointwise_bounds({0}, unit); });
    rejects([&] { search_pointwise_bounds({1}, unit, {-1}); });
}
}  // namespace

int main() {
    try {
        refinement();
        numeric_extremes();
        invalid_inputs();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
