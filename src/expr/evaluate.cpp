// Adapted from QPET. Copyright (c) 2026, Jinyang Liu. See LICENSES/QPET.txt.
#include "qoiw/expr.hpp"

#include "expr/bytecode.hpp"

#include <algorithm>
#include <stdexcept>

namespace qoiw {
using namespace expr_detail;
namespace {

#define QOIW_LOOP(expr) \
    for (size_t i = 0; i < n; i++) a[i] = (expr); \
    break

// st holds depth * n doubles
template <class Ops>
void run(const Ops &prog, const double *x, double *out, size_t n, double *st) {
    size_t sp = 0;
    for (const auto &op : prog) {
        double *a = st + (sp ? sp - 1 : 0) * n;
        const double c = op.c;
        switch (op.code) {
            case OP_CONST:
                a = st + sp++ * n;
                std::fill(a, a + n, c);
                break;
            case OP_VAR:
                a = st + sp++ * n;
                std::copy(x, x + n, a);
                break;
            case OP_ADD:
            case OP_SUB:
            case OP_MUL:
            case OP_DIV:
            case OP_POW:
            case OP_MIN:
            case OP_MAX:
            case OP_LT:
            case OP_LE:
            case OP_GT:
            case OP_GE:
            case OP_EQ:
            case OP_NE:
            case OP_AND:
            case OP_OR: {
                const double *b = a;
                a -= n;
                sp--;
                switch (op.code) {
                    case OP_ADD: QOIW_LOOP(a[i] + b[i]);
                    case OP_SUB: QOIW_LOOP(a[i] - b[i]);
                    case OP_MUL: QOIW_LOOP(a[i] * b[i]);
                    case OP_DIV: QOIW_LOOP(a[i] / b[i]);
                    case OP_POW: QOIW_LOOP(std::pow(a[i], b[i]));
                    case OP_MIN: QOIW_LOOP(a[i] < b[i] ? a[i] : b[i]);
                    case OP_MAX: QOIW_LOOP(a[i] > b[i] ? a[i] : b[i]);
                    case OP_LT: QOIW_LOOP(a[i] < b[i]);
                    case OP_LE: QOIW_LOOP(a[i] <= b[i]);
                    case OP_GT: QOIW_LOOP(a[i] > b[i]);
                    case OP_GE: QOIW_LOOP(a[i] >= b[i]);
                    case OP_EQ: QOIW_LOOP(a[i] == b[i]);
                    case OP_NE: QOIW_LOOP(a[i] != b[i]);
                    case OP_AND: QOIW_LOOP(a[i] != 0 && b[i] != 0);
                    default: QOIW_LOOP(a[i] != 0 || b[i] != 0);
                }
                break;
            }
            case OP_SELECT: {  // both branches were computed for every value; keep the one the condition picks
                const double *no = a, *yes = a - n;
                a -= 2 * n;
                sp -= 2;
                QOIW_LOOP(a[i] != 0 ? yes[i] : no[i]);
            }
            case OP_NOT: QOIW_LOOP(a[i] == 0);
            case OP_FLOOR: QOIW_LOOP(std::floor(a[i]));
            case OP_CEIL: QOIW_LOOP(std::ceil(a[i]));
            case OP_ROUND: QOIW_LOOP(std::round(a[i]));
            case OP_SIGN: QOIW_LOOP(sign(a[i]));
            case OP_ADDC: QOIW_LOOP(a[i] + c);
            case OP_MULC: QOIW_LOOP(a[i] * c);
            case OP_DIVC: QOIW_LOOP(a[i] / c);
            case OP_RDIVC: QOIW_LOOP(c / a[i]);
            case OP_POWC:
                if (c == 2) {
                    QOIW_LOOP(a[i] * a[i]);
                } else if (c == 3) {
                    QOIW_LOOP(a[i] * a[i] * a[i]);
                } else {
                    QOIW_LOOP(std::pow(a[i], c));
                }
            case OP_NEG: QOIW_LOOP(-a[i]);
            case OP_SIN: QOIW_LOOP(std::sin(a[i]));
            case OP_COS: QOIW_LOOP(std::cos(a[i]));
            case OP_TAN: QOIW_LOOP(std::tan(a[i]));
            case OP_ASIN: QOIW_LOOP(std::asin(a[i]));
            case OP_ACOS: QOIW_LOOP(std::acos(a[i]));
            case OP_ATAN: QOIW_LOOP(std::atan(a[i]));
            case OP_SINH: QOIW_LOOP(std::sinh(a[i]));
            case OP_COSH: QOIW_LOOP(std::cosh(a[i]));
            case OP_TANH: QOIW_LOOP(std::tanh(a[i]));
            case OP_EXP: QOIW_LOOP(std::exp(a[i]));
            case OP_LOG: QOIW_LOOP(std::log(a[i]));
            case OP_LOG2: QOIW_LOOP(std::log2(a[i]));
            case OP_LOG10: QOIW_LOOP(std::log10(a[i]));
            case OP_SQRT: QOIW_LOOP(std::sqrt(a[i]));
            case OP_CBRT: QOIW_LOOP(std::cbrt(a[i]));
            case OP_ABS: QOIW_LOOP(std::fabs(a[i]));
            case OP_SIGMOID: QOIW_LOOP(sigmoid(a[i]));
        }
    }
    std::copy(st, st + n, out);
}

#undef QOIW_LOOP

}  // namespace

double Expr::eval(double x) const {
    double st[MAX_DEPTH], out;
    run(prog_, &x, &out, 1, st);
    return out;
}

void Expr::eval(const double *x, double *out, size_t n) const {
    if (n == 0) return;
    if (!x || !out) throw std::invalid_argument("null expression batch buffer");
    constexpr size_t CHUNK = 256;
    std::vector<double> stack(depth_ * std::min(n, CHUNK));
    for (size_t i = 0; i < n; i += CHUNK) {
        size_t m = std::min(CHUNK, n - i);
        run(prog_, x + i, out + i, m, stack.data());
    }
}

}  // namespace qoiw
