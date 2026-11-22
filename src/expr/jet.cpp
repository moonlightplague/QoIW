// Adapted from QPET. Copyright (c) 2026, Jinyang Liu. See LICENSES/QPET.txt.
#include "qoiw/expr.hpp"

#include "expr/bytecode.hpp"

#include <algorithm>

namespace qoiw {
using namespace expr_detail;
namespace {
// distance in x to the point where u reaches 0, to first order
inline double zero_dist(const Jet &u) {
    if (u.d1 != 0) return std::fabs(u.v / u.d1);
    return u.v == 0 ? 0 : INF;
}

// g(u) given g, g', g'' at u.v
inline Jet chain(const Jet &u, double g, double g1, double g2, double dist) {
    Jet r;
    r.v = g;
    r.d1 = g1 * u.d1;
    r.d2 = g2 * u.d1 * u.d1 + g1 * u.d2;
    r.dist = std::min(u.dist, dist);
    r.brk = u.brk;
    return r;
}

inline Jet jet_const(double c) {
    Jet r;
    r.v = c;
    return r;
}

inline Jet jet_mul(const Jet &a, const Jet &b) {
    Jet r;
    r.v = a.v * b.v;
    r.d1 = a.d1 * b.v + a.v * b.d1;
    r.d2 = a.d2 * b.v + 2 * a.d1 * b.d1 + a.v * b.d2;
    r.dist = std::min(a.dist, b.dist);
    r.brk = std::min(a.brk, b.brk);
    return r;
}

// distance in x to the point where a and b cross, to first order
inline double cross_dist(const Jet &a, const Jet &b) {
    Jet d;
    d.v = a.v - b.v;
    d.d1 = a.d1 - b.d1;
    return zero_dist(d);
}

inline double truth_break(const Jet &u) {
    if (u.d1 == 0 && u.d2 == 0) return u.brk;
    return std::min(u.brk, zero_dist(u));
}

// a condition or a choice between a and b: the result of the comparison, and where it flips
inline Jet jet_compare(OpCode op, const Jet &a, const Jet &b) {
    if (op == OP_MIN || op == OP_MAX) {
        const bool choose_a = op == OP_MIN ? a.v < b.v : a.v > b.v;
        Jet r = choose_a ? a : b;
        r.brk = std::min({a.brk, b.brk, cross_dist(a, b)});
        return r;
    }
    Jet r;
    r.v = binary_value(op, a.v, b.v);
    r.dist = std::min(a.dist, b.dist);
    r.brk = std::min(a.brk, b.brk);
    if (op == OP_AND || op == OP_OR) r.brk = std::min(truth_break(a), truth_break(b));
    else r.brk = std::min(r.brk, cross_dist(a, b));
    return r;
}

inline Jet jet_div(const Jet &a, const Jet &b) {
    // Differentiate r*b = a. Forming a reciprocal first can overflow even when r is finite.
    Jet r;
    r.v = a.v / b.v;
    r.d1 = (a.d1 - r.v * b.d1) / b.v;
    r.d2 = (a.d2 - r.v * b.d2 - 2 * r.d1 * b.d1) / b.v;
    r.dist = std::min({a.dist, b.dist, zero_dist(b)});
    r.brk = std::min(a.brk, b.brk);
    return r;
}

inline Jet jet_powc(const Jet &u, double p) {
    if (p == 0) return jet_const(1);
    if (p == 1) return u;
    if (p == 3) return chain(u, std::pow(u.v, p), 3 * u.v * u.v, 6 * u.v, INF);
    if (p == 2) return chain(u, u.v * u.v, 2 * u.v, 2, INF);
    bool smooth = p >= 0 && p == std::floor(p);
    double g = std::pow(u.v, p);
    double g1 = p * std::pow(u.v, p - 1);
    double g2 = p * (p - 1) * std::pow(u.v, p - 2);
    return chain(u, g, g1, g2, smooth ? INF : zero_dist(u));
}

inline Jet jet_unary(OpCode op, const Jet &u) {
    switch (op) {
        case OP_NEG: return chain(u, -u.v, -1, 0, INF);
        case OP_SIN: return chain(u, std::sin(u.v), std::cos(u.v), -std::sin(u.v), INF);
        case OP_COS: return chain(u, std::cos(u.v), -std::sin(u.v), -std::cos(u.v), INF);
        case OP_TAN: {
            double t = std::tan(u.v), s = 1 + t * t;
            double dist = u.d1 != 0 ? std::fabs(std::cos(u.v) / u.d1) : INF;
            return chain(u, t, s, 2 * t * s, dist);
        }
        case OP_ASIN: {
            double w = 1 - u.v * u.v, s = std::sqrt(w);
            double dist = u.d1 != 0 ? std::fabs((1 - std::fabs(u.v)) / u.d1) : INF;
            return chain(u, std::asin(u.v), 1 / s, u.v / (w * s), dist);
        }
        case OP_ACOS: {
            double w = 1 - u.v * u.v, s = std::sqrt(w);
            double dist = u.d1 != 0 ? std::fabs((1 - std::fabs(u.v)) / u.d1) : INF;
            return chain(u, std::acos(u.v), -1 / s, -u.v / (w * s), dist);
        }
        case OP_ATAN: {
            double w = 1 + u.v * u.v;
            return chain(u, std::atan(u.v), 1 / w, -2 * u.v / (w * w), INF);
        }
        case OP_SINH: return chain(u, std::sinh(u.v), std::cosh(u.v), std::sinh(u.v), INF);
        case OP_COSH: return chain(u, std::cosh(u.v), std::sinh(u.v), std::cosh(u.v), INF);
        case OP_TANH: {
            double t = std::tanh(u.v), s = 1 - t * t;
            return chain(u, t, s, -2 * t * s, INF);
        }
        case OP_EXP: {
            double g = std::exp(u.v);
            return chain(u, g, g, g, INF);
        }
        case OP_LOG: return chain(u, std::log(u.v), 1 / u.v, -1 / (u.v * u.v), zero_dist(u));
        case OP_LOG2: return chain(u, std::log2(u.v), 1 / (u.v * M_LN2), -1 / (u.v * u.v * M_LN2), zero_dist(u));
        case OP_LOG10: return chain(u, std::log10(u.v), 1 / (u.v * M_LN10), -1 / (u.v * u.v * M_LN10), zero_dist(u));
        case OP_SQRT: {
            double g = std::sqrt(u.v);
            return chain(u, g, 0.5 / g, -0.25 / (g * u.v), zero_dist(u));
        }
        case OP_CBRT: {
            double g = std::cbrt(u.v);
            return chain(u, g, g / (3 * u.v), -2 * g / (9 * u.v * u.v), zero_dist(u));
        }
        case OP_ABS: {
            double s = u.v < 0 ? -1 : 1;
            Jet r = chain(u, std::fabs(u.v), s, 0, INF);
            r.brk = std::min(r.brk, zero_dist(u));
            return r;
        }
        case OP_SIGMOID: {
            double g = sigmoid(u.v), s = g * (1 - g);
            return chain(u, g, s, s * (1 - 2 * g), INF);
        }
        case OP_NOT: {
            Jet r = jet_const(u.v == 0);
            r.brk = truth_break(u);
            r.dist = u.dist;
            return r;
        }
        case OP_FLOOR:
        case OP_CEIL:
        case OP_ROUND:
        case OP_SIGN: {
            // constant around u, up to the nearest value of u where the step changes
            double g = unary_value(op, u.v), to_step;
            if (op == OP_SIGN) to_step = std::fabs(u.v);
            else if (op == OP_ROUND) to_step = 0.5 - std::fabs(u.v - g);
            else to_step = std::min(u.v - std::floor(u.v), std::ceil(u.v) - u.v);
            Jet r = chain(u, g, 0, 0, INF);
            double d = u.d1 != 0 ? std::fabs(to_step / u.d1) : (to_step == 0 ? 0 : INF);
            r.brk = std::min(r.brk, std::isfinite(u.v) ? d : INF);
            return r;
        }
        default: return jet_const(std::nan(""));
    }
}
}  // namespace

Jet Expr::jet(double x) const {
    Jet st[MAX_DEPTH];
    size_t sp = 0;
    for (const Op &op : prog_) {
        switch (op.code) {
            case OP_CONST: st[sp++] = jet_const(op.c); break;
            case OP_VAR:
                st[sp] = Jet();
                st[sp].v = x;
                st[sp].d1 = 1;
                sp++;
                break;
            case OP_ADD:
            case OP_SUB: {
                Jet &a = st[sp - 2];
                const Jet &b = st[sp - 1];
                double s = op.code == OP_ADD ? 1 : -1;
                a.v += s * b.v;
                a.d1 += s * b.d1;
                a.d2 += s * b.d2;
                a.dist = std::min(a.dist, b.dist);
                a.brk = std::min(a.brk, b.brk);
                sp--;
                break;
            }
            case OP_MIN:
            case OP_MAX:
            case OP_LT:
            case OP_LE:
            case OP_GT:
            case OP_GE:
            case OP_EQ:
            case OP_NE:
            case OP_AND:
            case OP_OR:
                st[sp - 2] = jet_compare(static_cast<OpCode>(op.code), st[sp - 2], st[sp - 1]);
                sp--;
                break;
            case OP_SELECT: {
                const double flips = truth_break(st[sp - 3]);
                const double dist = st[sp - 3].dist;
                st[sp - 3] = st[sp - 3].v != 0 ? st[sp - 2] : st[sp - 1];
                st[sp - 3].dist = std::min(st[sp - 3].dist, dist);
                st[sp - 3].brk = std::min(st[sp - 3].brk, flips);
                sp -= 2;
                break;
            }
            case OP_MUL:
                st[sp - 2] = jet_mul(st[sp - 2], st[sp - 1]);
                sp--;
                break;
            case OP_DIV:
                st[sp - 2] = jet_div(st[sp - 2], st[sp - 1]);
                sp--;
                break;
            case OP_POW: {  // Differentiate exp(w log u) but evaluate the specified pow operation.
                const double value = std::pow(st[sp - 2].v, st[sp - 1].v);
                Jet e = jet_mul(st[sp - 1], jet_unary(OP_LOG, st[sp - 2]));
                st[sp - 2] = chain(e, value, value, value, INF);
                sp--;
                break;
            }
            case OP_ADDC: st[sp - 1].v += op.c; break;
            case OP_MULC:
                st[sp - 1].v *= op.c;
                st[sp - 1].d1 *= op.c;
                st[sp - 1].d2 *= op.c;
                break;
            case OP_DIVC:
                st[sp - 1].v /= op.c;
                st[sp - 1].d1 /= op.c;
                st[sp - 1].d2 /= op.c;
                break;
            case OP_RDIVC:
                st[sp - 1] = jet_div(jet_const(op.c), st[sp - 1]);
                break;
            case OP_POWC: st[sp - 1] = jet_powc(st[sp - 1], op.c); break;
            default: st[sp - 1] = jet_unary(static_cast<OpCode>(op.code), st[sp - 1]); break;
        }
    }
    return st[0];
}

}  // namespace qoiw
