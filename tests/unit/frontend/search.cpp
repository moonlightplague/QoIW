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

void near_ties() {
    // Compare every trial to the global minimum. 105 is close to 102.5
    // but outside the band around 100.
    auto choose = [](double middle, double tie) {
        return search_pointwise_bounds({1, 2, 4}, [=](double b) {
            return score(b == 1 ? 105 : (b == 2 ? middle : 100));
        }, {0, tie}).bound;
    };
    check(choose(102.5, .025) == 2, "inclusive tie threshold");
    check(choose(std::nextafter(102.5, 103.), .025) == 4, "cost above tie threshold");
    check(choose(100, 0) == 2 && choose(100.01, 0) == 4, "exact ties with zero tolerance");

    const auto refined_tie = search_pointwise_bounds({1, 4, 16}, [](double b) {
        if (b == 4) return score(100.);
        return score(b == 2 ? 102.5 : 110.);
    }, {1});
    check(refined_tie.bound == 2, "near-tie among measured midpoints");
    const auto cheaper = search_pointwise_bounds({1, 4, 64}, [](double b) {
        if (b == 16) return score(80.);
        if (b == 1) return score(102.);
        return score(b == 4 ? 100. : 110.);
    }, {1});
    check(cheaper.bound == 16, "near-tie diverted refinement from cheapest seed");
}

void data_bound_policy() {
    auto with_cap = [](std::vector<double> seeds, double cap_cost, std::size_t corrections) {
        int cap_calls = 0;
        auto result = search_pointwise_bounds(seeds, [&](double b) {
            if (b == 8) {
                ++cap_calls;
                return score(cap_cost, corrections);
            }
            return score(b == 4 ? 100 : 110);
        }, {0}, 8.);
        check(cap_calls == 1, "data-bound probe was skipped or measured twice");
        return result.bound;
    };
    check(with_cap({1, 4}, 102.5, 0) == 8, "prefer a clean data bound in the tie band");
    check(with_cap({1, 4}, 103, 0) == 4, "smaller bound with a material saving");
    check(with_cap({1, 4}, 50, 1) == 4, "probe needing correction entered candidate set");
    check(with_cap({1, 4, 8}, 50, 1) == 8, "discarded prior candidate needing correction");

    const auto bracket = search_pointwise_bounds({1, 4}, [](double b) {
        if (b == 16) return score(10, 0);
        return score(b == 8 ? 2 : 20);
    }, {1}, 16.);
    check(bracket.bound == 8, "clean data bound did not participate in refinement");

    const auto strict = search_pointwise_bounds({1, 8, 16}, [](double b) {
        check(b <= 4, "strict core measured above the data bound");
        return score(1 / b);
    }, {1}, 4.);
    check(strict.bound == 4 && strict.trace.size() == 3, "strict cap was not deduplicated");
    const auto nominal = search_pointwise_bounds({1, 16}, [](double b) { return score(1 / b); },
                                                {0}, 4., qoiw::BoundKind::NominalAbs);
    check(nominal.bound == 16, "discarded admissible nominal trial");
    const auto zero = search_pointwise_bounds({1, 4}, [](double b) {
        check(b == 0, "nonzero trial at zero strict cap");
        return score(1, 0);
    }, {}, 0.);
    check(zero.bound == 0 && zero.trace.size() == 1, "zero strict cap");
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
        rejects([&] { search_pointwise_bounds({1.}, unit, {1, invalid}); });
        rejects([&] { search_pointwise_bounds({1.}, unit, {}, invalid); });
    }
    rejects([&] { search_pointwise_bounds({0}, unit); });
    rejects([&] { search_pointwise_bounds({1}, unit, {-1}); });
}
}  // namespace

int main() {
    try {
        refinement();
        numeric_extremes();
        near_ties();
        data_bound_policy();
        invalid_inputs();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
