// Adapted from QPET. Copyright (c) 2026, Jinyang Liu. See LICENSES/QPET.txt.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace qoiw::expr_detail {

const double INF = std::numeric_limits<double>::infinity();
const size_t MAX_DEPTH = 64;
enum OpCode : uint8_t {
    OP_CONST,
    OP_VAR,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_POW,
    OP_MIN,
    OP_MAX,
    // conditions: the result is 1 (true) or 0
    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE,
    OP_EQ,
    OP_NE,
    OP_AND,
    OP_OR,
    OP_SELECT,  // condition, value if true, value if false
    // one operand and a constant
    OP_ADDC,
    OP_MULC,
    OP_DIVC,   // u / c
    OP_RDIVC,  // c / u
    OP_POWC,   // u ^ c
    // one operand
    OP_NEG,
    OP_SIN,
    OP_COS,
    OP_TAN,
    OP_ASIN,
    OP_ACOS,
    OP_ATAN,
    OP_SINH,
    OP_COSH,
    OP_TANH,
    OP_EXP,
    OP_LOG,
    OP_LOG2,
    OP_LOG10,
    OP_SQRT,
    OP_CBRT,
    OP_ABS,
    OP_SIGMOID,
    OP_NOT,
    // steps: constant between breakpoints
    OP_FLOOR,
    OP_CEIL,
    OP_ROUND,
    OP_SIGN,
};

inline double sigmoid(double u) { return 1.0 / (1.0 + std::exp(-u)); }
inline double sign(double u) { return u > 0 ? 1.0 : u < 0 ? -1.0 : u; }  // 0 at 0, NaN stays NaN

inline double unary_value(OpCode op, double u) {
    switch (op) {
        case OP_NEG: return -u;
        case OP_SIN: return std::sin(u);
        case OP_COS: return std::cos(u);
        case OP_TAN: return std::tan(u);
        case OP_ASIN: return std::asin(u);
        case OP_ACOS: return std::acos(u);
        case OP_ATAN: return std::atan(u);
        case OP_SINH: return std::sinh(u);
        case OP_COSH: return std::cosh(u);
        case OP_TANH: return std::tanh(u);
        case OP_EXP: return std::exp(u);
        case OP_LOG: return std::log(u);
        case OP_LOG2: return std::log2(u);
        case OP_LOG10: return std::log10(u);
        case OP_SQRT: return std::sqrt(u);
        case OP_CBRT: return std::cbrt(u);
        case OP_ABS: return std::fabs(u);
        case OP_SIGMOID: return sigmoid(u);
        case OP_NOT: return u == 0;
        case OP_FLOOR: return std::floor(u);
        case OP_CEIL: return std::ceil(u);
        case OP_ROUND: return std::round(u);
        case OP_SIGN: return sign(u);
        default: return std::nan("");
    }
}

inline double binary_value(OpCode op, double a, double b) {
    switch (op) {
        case OP_ADD: return a + b;
        case OP_SUB: return a - b;
        case OP_MUL: return a * b;
        case OP_DIV: return a / b;
        case OP_POW: return std::pow(a, b);
        case OP_MIN: return a < b ? a : b;
        case OP_MAX: return a > b ? a : b;
        case OP_LT: return a < b;
        case OP_LE: return a <= b;
        case OP_GT: return a > b;
        case OP_GE: return a >= b;
        case OP_EQ: return a == b;
        case OP_NE: return a != b;
        case OP_AND: return a != 0 && b != 0;
        case OP_OR: return a != 0 || b != 0;
        default: return std::nan("");
    }
}

}  // namespace qoiw::expr_detail
