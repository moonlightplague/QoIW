#include <cmath>
#include <cstring>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "qoiw/expr.hpp"

namespace {
using qoiw::Expr;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void same_value(const Expr& f, double x, double expected) {
    const double scalar = f.eval(x);
    const auto jet = f.jet(x);
    double batch = x;
    f.eval(&batch, &batch, 1);
    for (double value : {scalar, jet.v, batch}) {
        if (std::isnan(expected)) check(std::isnan(value), "missing NaN");
        else check(std::memcmp(&value, &expected, sizeof(double)) == 0, f.text().c_str());
    }
}

void values() {
    same_value(Expr("-2^2"), 0, -4);
    same_value(Expr("2^3**2"), 0, 512);
    same_value(Expr("2x^2"), -3, 18);
    same_value(Expr("round(x)"), -0.5, -1);
    same_value(Expr("round(x)"), 0.5, 1);
    same_value(Expr("if(x < 0, NaN, sqrt(x))"), -1, std::numeric_limits<double>::quiet_NaN());
    same_value(Expr("x < 0 ? -INF : ln(x)"), -1, -std::numeric_limits<double>::infinity());
    same_value(Expr("max(x, -0.0)"), 0, -0.0);
    same_value(Expr("max(nan, x)"), 2, 2);
    same_value(Expr("if(x < 0, 1, sqrt(x))"), -3, 1);
    same_value(Expr("2^x == 8"), 3, 1);
    same_value(Expr("3/x"), 10, 3.0 / 10);
    same_value(Expr("x/(x+7)"), 3, 3.0 / 10);
    same_value(Expr("x^x"), -2, .25);
    same_value(Expr("floor(x/0.1)"), std::nextafter(1.0, 0.0), 9);
}

void derivatives_and_boundaries() {
    for (auto power : {0, 1}) {
        const Expr f("pow(x," + std::to_string(power) + ")");
        const auto j = f.jet(0);
        check(j.d1 == power && j.d2 == 0 && f.is_linear(), "constant and identity powers at zero");
    }
    auto quotient = Expr("x/x").jet(std::numeric_limits<double>::denorm_min());
    check(quotient.v == 1 && quotient.d1 == 0 && quotient.d2 == 0, "subnormal quotient");
    auto smooth = Expr("2x+1").jet(0);
    check(smooth.d1 == 2 && smooth.d2 == 0 && std::isinf(smooth.dist) && std::isinf(smooth.brk), "affine jet");
    check(!Expr("x/0").is_linear() && !Expr("inf").is_linear(), "non-finite affine coefficients");
    check(Expr("ln(2x-1)").jet(2).dist == 1.5, "affine log singularity");
    check(Expr("1/(x+3)").jet(-2).dist == 1, "division singularity");
    check(Expr("sqrt(x)").jet(0).dist == 0, "root at zero");
    check(Expr("if(1/x > 1, 1, 2)").jet(2).dist == 2, "condition singularity");
    check(std::isinf(Expr("x < 0 ? 0 : sqrt(x)").jet(-1).dist), "inactive branch singularity leaked");
    check(Expr("abs(2x-1)").jet(1).brk == .5, "absolute value kink");
    check(Expr("if(x >= 2, x^2, 0)").jet(2).brk == 0, "comparison boundary");
    check(Expr("x ? 1 : 0").jet(.5).brk == .5, "numeric condition boundary");
    check(Expr("not x").jet(.5).brk == .5, "numeric negation boundary");
    check(Expr("x and 1").jet(-2).brk == 2, "numeric conjunction boundary");
    check(Expr("max(x,1-x)").jet(.25).brk == .25, "crossing of linear branches");
    check(Expr("floor(2x)").jet(.375).brk == .125, "integer step");
    check(Expr("round(x)").jet(-.5).brk == 0, "rounding boundary");
}

void batches(const Expr& f) {
    for (std::size_t n : {1, 255, 256, 257, 2049}) {
        std::vector<double> values(n), expected(n);
        for (std::size_t i = 0; i < n; ++i) {
            values[i] = (static_cast<double>(i) - 128) / 32;
            expected[i] = f.eval(values[i]);
        }
        f.eval(values.data(), values.data(), n);
        check(values == expected, "in-place batch differs across chunk boundary");
    }
    f.eval(nullptr, nullptr, 0);
}

void reject(const std::string& text) {
    try {
        Expr expr(text);
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("invalid expression was accepted");
}
}  // namespace

int main() {
    try {
        values();
        derivatives_and_boundaries();
        Expr f("if(x < 0, 2x + 1, sqrt(x))");
        auto other = std::async(std::launch::async, [&] { batches(f); });
        batches(f);
        other.get();
        reject("1e9999");
        reject(std::string("x\0 + 1", 6));
        reject(std::string(1000, '(') + "x" + std::string(1000, ')'));
        reject(std::string(1000, '-') + "x");
        std::string sum = "x";
        for (int i = 0; i < 1000; ++i) sum += "+x";
        reject(sum);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
