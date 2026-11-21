// Adapted from QPET. Copyright (c) 2026, Jinyang Liu. See LICENSES/QPET.txt.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "qoiw/expr.hpp"

static int failures = 0;

#define CHECK(cond, ...) \
    do { \
        if (!(cond)) { \
            failures++; \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__); \
            std::printf("\n"); \
        } \
    } while (0)

static bool close(double a, double b, double rel = 1e-9) {
    if (std::isnan(a) || std::isnan(b)) return std::isnan(a) && std::isnan(b);
    if (!std::isfinite(a) || !std::isfinite(b)) return a == b;
    return std::fabs(a - b) <= rel * std::max({1.0, std::fabs(a), std::fabs(b)});
}

struct Case {
    std::string text;
    std::function<double(double)> ref;
    double lo, hi;  // test interval
};

int main() {
    const std::vector<Case> cases = {
        {"x", [](double x) { return x; }, -5, 5},
        {"x^2", [](double x) { return x * x; }, -5, 5},
        {"x**3", [](double x) { return x * x * x; }, -5, 5},
        {"x^2+x+1", [](double x) { return x * x + x + 1; }, -5, 5},
        {"-x^2", [](double x) { return -(x * x); }, -5, 5},
        {"2^x", [](double x) { return std::pow(2.0, x); }, -5, 5},
        {"e^x", [](double x) { return std::exp(x); }, -5, 5},
        {"exp(-x)", [](double x) { return std::exp(-x); }, -5, 5},
        {"log2(x)", [](double x) { return std::log2(x); }, 0.1, 50},
        {"ln(x)", [](double x) { return std::log(x); }, 0.1, 50},
        {"Log(x, 2)", [](double x) { return std::log(x) / std::log(2.0); }, 0.1, 50},
        {"log10(x)", [](double x) { return std::log10(x); }, 0.1, 50},
        {"1/x", [](double x) { return 1 / x; }, 0.2, 5},
        {"(x+3)^-1", [](double x) { return 1 / (x + 3); }, -2, 5},
        {"1/(x+1)", [](double x) { return 1 / (x + 1); }, 0, 5},
        {"sqrt(x)", [](double x) { return std::sqrt(x); }, 0.1, 50},
        {"x^0.5", [](double x) { return std::sqrt(x); }, 0.1, 50},
        {"sin(10x)", [](double x) { return std::sin(10 * x); }, -2, 2},
        {"sin(10*x)", [](double x) { return std::sin(10 * x); }, -2, 2},
        {"cos(x)*x", [](double x) { return std::cos(x) * x; }, -5, 5},
        {"tan(x)", [](double x) { return std::tan(x); }, -1, 1},
        {"tanh(x)", [](double x) { return std::tanh(x); }, -3, 3},
        {"(e^x-e^-x)/(e^x+e^-x)", [](double x) { return std::tanh(x); }, -3, 3},
        {"1/(1+exp(-x))", [](double x) { return 1 / (1 + std::exp(-x)); }, -5, 5},
        {"sigmoid(x)", [](double x) { return 1 / (1 + std::exp(-x)); }, -5, 5},
        {"|x|", [](double x) { return std::fabs(x); }, 0.5, 5},
        {"abs(x - 1)", [](double x) { return std::fabs(x - 1); }, 2, 5},
        {"pow(x, 2.5)", [](double x) { return std::pow(x, 2.5); }, 0.1, 5},
        {"x^x", [](double x) { return std::pow(x, x); }, 0.5, 3},
        {"asin(x)", [](double x) { return std::asin(x); }, -.9, .9},
        {"acos(x)", [](double x) { return std::acos(x); }, -.9, .9},
        {"sinh(x)", [](double x) { return std::sinh(x); }, -3, 3},
        {"cosh(x)", [](double x) { return std::cosh(x); }, -3, 3},
        {"atan(2x)", [](double x) { return std::atan(2 * x); }, -3, 3},
        {"3 - x/2 + pi", [](double x) { return 3 - x / 2 + M_PI; }, -5, 5},
        {"2(x+1)*x", [](double x) { return 2 * (x + 1) * x; }, -5, 5},
        {"cbrt(x)", [](double x) { return std::cbrt(x); }, 0.1, 5},
    };

    for (const Case &c : cases) {
        qoiw::Expr f(c.text);
        const int steps = 97;
        std::vector<double> xs(steps), batch(steps);
        for (int i = 0; i < steps; i++) xs[i] = c.lo + (c.hi - c.lo) * (i + 0.37) / steps;
        f.eval(xs.data(), batch.data(), steps);
        for (int i = 0; i < steps; i++) {
            double x = xs[i], v = c.ref(x);
            CHECK(close(f.eval(x), v), "%s: eval(%g) = %.17g, want %.17g", c.text.c_str(), x, f.eval(x), v);
            CHECK(f.eval(x) == batch[i], "%s: batch and scalar eval differ at %g", c.text.c_str(), x);
            qoiw::Jet j = f.jet(x);
            CHECK(close(j.v, v), "%s: jet value at %g", c.text.c_str(), x);
            double h = 1e-4 * std::max(1.0, std::fabs(x));
            double d1 = (c.ref(x + h) - c.ref(x - h)) / (2 * h);
            double d2 = (c.ref(x + h) - 2 * v + c.ref(x - h)) / (h * h);
            CHECK(close(j.d1, d1, 1e-5), "%s: f'(%g) = %.12g, finite difference %.12g", c.text.c_str(), x, j.d1, d1);
            CHECK(close(j.d2, d2, 2e-4), "%s: f''(%g) = %.12g, finite difference %.12g", c.text.c_str(), x, j.d2, d2);

        }
    }

    // Piecewise functions: evaluate both sides and the breakpoint itself.
    const std::vector<Case> pieces = {
        {"x < 0 ? 0 : sqrt(x)", [](double x) { return x < 0 ? 0 : std::sqrt(x); }, -3, 6},
        {"x < 1 ? x^2 : x < 5 ? x : 5", [](double x) { return x < 1 ? x * x : x < 5 ? x : 5; }, -3, 8},
        {"if(x >= 2 and x < 3, 10, x)", [](double x) { return x >= 2 && x < 3 ? 10 : x; }, -1, 6},
        {"x >= 2 ? log2(x) : 0", [](double x) { return x >= 2 ? std::log2(x) : 0; }, -1, 6},
        {"X <= 0 OR x > 3 ? 1 : 0", [](double x) { return x <= 0 || x > 3 ? 1.0 : 0.0; }, -2, 6},
        {"not (x > 1) ? -1 : 1", [](double x) { return !(x > 1) ? -1.0 : 1.0; }, -2, 4},
        {"max(x, 0)", [](double x) { return std::max(x, 0.0); }, -3, 3},
        {"min(x^2, 4)", [](double x) { return std::min(x * x, 4.0); }, -4, 4},
        {"x != 2 ? 1/(x - 2) : 0", [](double x) { return x != 2 ? 1 / (x - 2) : 0; }, 2.5, 6},
        {"x == 1 ? 7 : 3x", [](double x) { return x == 1 ? 7 : 3 * x; }, -2, 4},
        {"(x > 0 ? x : -x) + 2", [](double x) { return std::fabs(x) + 2; }, -3, 3},
        {"floor(x)", [](double x) { return std::floor(x); }, -3, 3},
        {"ceil(2x)/2", [](double x) { return std::ceil(2 * x) / 2; }, -3, 3},
        {"round(x/0.3)", [](double x) { return std::round(x / 0.3); }, -3, 3},
        {"sign(x - 1) * x^2", [](double x) { return (x > 1 ? 1.0 : x < 1 ? -1.0 : 0.0) * x * x; }, -3, 3},
        {"floor(4 sigmoid(x))", [](double x) { return std::floor(4 / (1 + std::exp(-x))); }, -5, 5},
    };
    for (const Case &c : pieces) {
        qoiw::Expr f(c.text);
        CHECK(!f.is_linear(), "%s is not linear", c.text.c_str());
        const int steps = 601;
        std::vector<double> xs(steps), batch(steps);
        for (int i = 0; i < steps; i++) xs[i] = c.lo + (c.hi - c.lo) * i / (steps - 1);  // hits the breakpoints exactly
        f.eval(xs.data(), batch.data(), steps);
        for (int i = 0; i < steps; i++) {
            double x = xs[i], v = c.ref(x);
            CHECK(close(f.eval(x), v) && f.eval(x) == batch[i], "%s: eval(%g) = %g, want %g", c.text.c_str(), x, f.eval(x), v);
            CHECK(close(f.jet(x).v, v), "%s: jet value at %g", c.text.c_str(), x);
        }
    }
    CHECK(qoiw::Expr("x").is_linear(), "x is linear");
    CHECK(qoiw::Expr("3 - x/2").is_linear(), "3 - x/2 is linear");
    CHECK(qoiw::Expr("2(x+1)").is_linear(), "2(x+1) is linear");
    CHECK(!qoiw::Expr("x^2").is_linear(), "x^2 is not linear");
    CHECK(!qoiw::Expr("1/x").is_linear(), "1/x is not linear");

    for (const char *bad : {"", "x +", "foo(x)", "y", "(x", "2 ** ", "sin(x, 2)", "x x", "|x", "x < ", "x ? 1", "x > 0 ? 1 :",
                            "if(x, 1)", "min(x)", "x and", "sin(x, 1, 2)", "x = 1"}) {
        bool threw = false;
        try {
            qoiw::Expr f(bad);
        } catch (const std::invalid_argument &) {
            threw = true;
        }
        CHECK(threw, "\"%s\" should not parse", bad);
    }

    if (failures) std::printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}
