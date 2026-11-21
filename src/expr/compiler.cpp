// Adapted from QPET. Copyright (c) 2026, Jinyang Liu. See LICENSES/QPET.txt.
#include "qoiw/expr.hpp"

#include "expr/bytecode.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace qoiw {
using namespace expr_detail;
namespace {
struct Func {
    const char *name;
    OpCode op;
};
const Func FUNCS[] = {
    {"sin", OP_SIN},     {"cos", OP_COS},   {"tan", OP_TAN},   {"asin", OP_ASIN}, {"acos", OP_ACOS},
    {"atan", OP_ATAN},   {"sinh", OP_SINH}, {"cosh", OP_COSH}, {"tanh", OP_TANH}, {"exp", OP_EXP},
    {"log", OP_LOG},     {"ln", OP_LOG},    {"log2", OP_LOG2}, {"log10", OP_LOG10}, {"sqrt", OP_SQRT},
    {"cbrt", OP_CBRT},   {"abs", OP_ABS},   {"sigmoid", OP_SIGMOID},
    {"floor", OP_FLOOR}, {"ceil", OP_CEIL}, {"round", OP_ROUND}, {"sign", OP_SIGN},
};

struct Node {
    // degree(), bytecode emission, and destruction traverse this tree recursively.
    explicit Node(OpCode code, size_t child_height = 0) : op(code), height(child_height + 1) {
        if (height > 512) throw std::invalid_argument("QoIW: expression tree exceeds 512 levels");
    }
    OpCode op;
    size_t height;
    double c = 0;
    std::unique_ptr<Node> a, b, c3;
};
using NodePtr = std::unique_ptr<Node>;

NodePtr make_const(double c) {
    auto n = std::make_unique<Node>(OP_CONST);
    n->c = c;
    return n;
}

NodePtr make_unary(OpCode op, NodePtr a) {
    if (a->op == OP_CONST) return make_const(unary_value(op, a->c));
    auto n = std::make_unique<Node>(op, a->height);
    n->a = std::move(a);
    return n;
}

NodePtr make_binary(OpCode op, NodePtr a, NodePtr b) {
    if (a->op == OP_CONST && b->op == OP_CONST) return make_const(binary_value(op, a->c, b->c));
    auto n = std::make_unique<Node>(op, std::max(a->height, b->height));
    n->a = std::move(a);
    n->b = std::move(b);
    return n;
}

NodePtr make_select(NodePtr cond, NodePtr a, NodePtr b) {
    if (cond->op == OP_CONST) return cond->c != 0 ? std::move(a) : std::move(b);
    auto n = std::make_unique<Node>(OP_SELECT, std::max({cond->height, a->height, b->height}));
    n->a = std::move(cond);
    n->b = std::move(a);
    n->c3 = std::move(b);
    return n;
}

class Parser {
   public:
    explicit Parser(const std::string &s) : s_(s) {}

    NodePtr parse() {
        NodePtr n = expr();
        skip();
        if (pos_ != s_.size()) fail("unexpected '" + std::string(1, s_[pos_]) + "'");
        return n;
    }

   private:
    const std::string &s_;
    size_t pos_ = 0;
    size_t nesting_ = 0;

    // Only recursive grammar edges consume a nesting level. Iterative operator
    // chains are limited separately by tree height and the VM stack.
    NodePtr nested(NodePtr (Parser::*rule)()) {
        if (++nesting_ > MAX_DEPTH) fail("expression exceeds parser nesting limit");
        auto node = (this->*rule)();
        --nesting_;
        return node;
    }

    [[noreturn]] void fail(const std::string &what) const {
        throw std::invalid_argument("QOIW: cannot parse QoI \"" + s_ + "\": " + what + " at position " +
                                    std::to_string(pos_));
    }

    void skip() {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) pos_++;
    }

    char peek() {
        skip();
        return pos_ < s_.size() ? s_[pos_] : '\0';
    }

    bool accept(char c) {
        if (peek() != c) return false;
        pos_++;
        return true;
    }

    void expect(char c) {
        if (!accept(c)) fail(std::string("expected '") + c + "'");
    }

    // is the next token the word (such as "and"), not followed by more letters?
    bool at_word(const char *word) {
        skip();
        size_t n = std::strlen(word);
        if (pos_ + n > s_.size()) return false;
        for (size_t i = 0; i < n; i++)
            if (std::tolower(static_cast<unsigned char>(s_[pos_ + i])) != word[i]) return false;
        return !(pos_ + n < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[pos_ + n])) || s_[pos_ + n] == '_'));
    }

    bool accept_word(const char *word) {
        if (!at_word(word)) return false;
        pos_ += std::strlen(word);
        return true;
    }

    // condition ? value : value, the lowest precedence; chains to the right
    NodePtr expr() {
        NodePtr n = disjunction();
        if (!accept('?')) return n;
        NodePtr a = nested(&Parser::expr);
        expect(':');
        return make_select(std::move(n), std::move(a), nested(&Parser::expr));
    }

    NodePtr disjunction() {
        NodePtr n = conjunction();
        while (accept_word("or")) n = make_binary(OP_OR, std::move(n), conjunction());
        return n;
    }

    NodePtr conjunction() {
        NodePtr n = negation();
        while (accept_word("and")) n = make_binary(OP_AND, std::move(n), negation());
        return n;
    }

    NodePtr negation() {
        if (accept_word("not")) return make_unary(OP_NOT, nested(&Parser::negation));
        return comparison();
    }

    NodePtr comparison() {
        NodePtr n = sum();
        char c = peek(), next = pos_ + 1 < s_.size() ? s_[pos_ + 1] : '\0';
        OpCode op;
        size_t len = 1;
        if (c == '<') op = next == '=' ? (len = 2, OP_LE) : OP_LT;
        else if (c == '>') op = next == '=' ? (len = 2, OP_GE) : OP_GT;
        else if (c == '=' && next == '=') op = (len = 2, OP_EQ);
        else if (c == '!' && next == '=') op = (len = 2, OP_NE);
        else return n;
        pos_ += len;
        return make_binary(op, std::move(n), sum());
    }

    NodePtr sum() {
        NodePtr n = term();
        for (;;) {
            if (accept('+')) n = make_binary(OP_ADD, std::move(n), term());
            else if (accept('-')) n = make_binary(OP_SUB, std::move(n), term());
            else return n;
        }
    }

    NodePtr term() {
        NodePtr n = unary();
        for (;;) {
            if (peek() == '*' && !(pos_ + 1 < s_.size() && s_[pos_ + 1] == '*')) {
                pos_++;
                n = make_binary(OP_MUL, std::move(n), unary());
            } else if (accept('/')) {
                n = make_binary(OP_DIV, std::move(n), unary());
            } else {
                return n;
            }
        }
    }

    NodePtr unary() {
        if (accept('-')) return make_unary(OP_NEG, nested(&Parser::unary));
        if (accept('+')) return nested(&Parser::unary);
        return power();
    }

    NodePtr power() {
        NodePtr base = primary();
        if (accept('^')) return make_binary(OP_POW, std::move(base), nested(&Parser::unary));
        if (peek() == '*' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '*') {
            pos_ += 2;
            return make_binary(OP_POW, std::move(base), nested(&Parser::unary));
        }
        return base;
    }

    NodePtr primary() {
        char c = peek();
        if (c == '(') {
            pos_++;
            NodePtr n = nested(&Parser::expr);
            expect(')');
            return n;
        }
        if (c == '|') {
            pos_++;
            NodePtr n = nested(&Parser::expr);
            expect('|');
            return make_unary(OP_ABS, std::move(n));
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            const char *begin = s_.c_str() + pos_;
            double v;
            const auto number = std::from_chars(begin, s_.data() + s_.size(), v);
            if (number.ec != std::errc{}) fail("invalid or out-of-range numeric literal");
            pos_ += number.ptr - begin;
            NodePtr n = make_const(v);
            // implicit multiplication: 10x, 2(x+1), 3sin(x)
            char next = peek();
            if ((std::isalpha(static_cast<unsigned char>(next)) || next == '_' || next == '(') && !at_word("and") &&
                !at_word("or"))
                return make_binary(OP_MUL, std::move(n), nested(&Parser::power));
            return n;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string id;
            while (pos_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_'))
                id += static_cast<char>(std::tolower(static_cast<unsigned char>(s_[pos_++])));
            if (peek() == '(') return call(id);
            if (id == "x") return std::make_unique<Node>(OP_VAR);
            if (id == "nan") return make_const(std::numeric_limits<double>::quiet_NaN());
            if (id == "inf") return make_const(INF);
            if (id == "pi") return make_const(M_PI);
            if (id == "e") return make_const(M_E);
            fail("unknown name '" + id + "'");
        }
        fail(c ? "unexpected '" + std::string(1, c) + "'" : "unexpected end");
    }

    NodePtr call(const std::string &id) {
        expect('(');
        NodePtr a = nested(&Parser::expr);
        NodePtr b, c;
        if (accept(',')) b = nested(&Parser::expr);
        if (b && accept(',')) c = nested(&Parser::expr);
        expect(')');
        if (id == "if") {  // if(condition, value if true, value if false)
            if (!c) fail("if takes three arguments");
            return make_select(std::move(a), std::move(b), std::move(c));
        }
        if (c) fail(id + " does not take three arguments");
        if (id == "min" || id == "max") {
            if (!b) fail(id + " takes two arguments");
            return make_binary(id == "min" ? OP_MIN : OP_MAX, std::move(a), std::move(b));
        }
        if (id == "pow") {
            if (!b) fail("pow takes two arguments");
            return make_binary(OP_POW, std::move(a), std::move(b));
        }
        if (id == "log" && b)  // log(x, base)
            return make_binary(OP_DIV, make_unary(OP_LOG, std::move(a)), make_unary(OP_LOG, std::move(b)));
        if (b) fail(id + " takes one argument");
        for (const Func &f : FUNCS)
            if (id == f.name) return make_unary(f.op, std::move(a));
        fail("unknown function '" + id + "'");
    }
};

// polynomial degree in x if it is 0 or 1, otherwise 2
int degree(const Node &n) {
    switch (n.op) {
        case OP_CONST: return std::isfinite(n.c) ? 0 : 2;
        case OP_VAR: return 1;
        case OP_NEG: return degree(*n.a);
        case OP_ADD:
        case OP_SUB: return std::max(degree(*n.a), degree(*n.b));
        case OP_MUL: return std::min(2, degree(*n.a) + degree(*n.b));
        case OP_DIV: return n.b->op == OP_CONST && n.b->c != 0 && std::isfinite(n.b->c) ? degree(*n.a) : 2;
        case OP_POW:
            if (n.b->op == OP_CONST && n.b->c == 0) return 0;
            if (n.b->op == OP_CONST && n.b->c == 1) return degree(*n.a);
            return 2;
        default: return 2;
    }
}

}  // namespace

struct ExprCompiler {
    std::vector<Expr::Op> prog;
    size_t depth = 0, max_depth = 0;

    void push(OpCode op, double c, int stack_change) {
        prog.push_back({op, c});
        depth += stack_change;
        max_depth = std::max(max_depth, depth);
    }

    void emit(const Node &n) {
        switch (n.op) {
            case OP_CONST: push(OP_CONST, n.c, 1); return;
            case OP_VAR: push(OP_VAR, 0, 1); return;
            case OP_ADD:
            case OP_SUB:
            case OP_MUL:
            case OP_DIV:
            case OP_POW: emit_binary(n); return;
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
                emit(*n.a);
                emit(*n.b);
                push(n.op, 0, -1);
                return;
            case OP_SELECT:
                emit(*n.a);
                emit(*n.b);
                emit(*n.c3);
                push(OP_SELECT, 0, -2);
                return;
            default:
                emit(*n.a);
                push(n.op, 0, 0);
                return;
        }
    }

    void emit_binary(const Node &n) {
        const Node &a = *n.a, &b = *n.b;
        if (b.op == OP_CONST) {
            emit(a);
            switch (n.op) {
                case OP_ADD: push(OP_ADDC, b.c, 0); return;
                case OP_SUB: push(OP_ADDC, -b.c, 0); return;
                case OP_MUL: push(OP_MULC, b.c, 0); return;
                case OP_DIV: push(OP_DIVC, b.c, 0); return;
                default: push(OP_POWC, b.c, 0); return;
            }
        }
        if (a.op == OP_CONST && n.op != OP_POW) {
            emit(b);
            switch (n.op) {
                case OP_ADD: push(OP_ADDC, a.c, 0); return;
                case OP_SUB:
                    push(OP_NEG, 0, 0);
                    push(OP_ADDC, a.c, 0);
                    return;
                case OP_MUL: push(OP_MULC, a.c, 0); return;
                default: push(OP_RDIVC, a.c, 0); return;
            }
        }
        emit(a);
        emit(b);
        push(n.op, 0, -1);
    }
};

Expr::Expr(const std::string &text) : text_(text) {
    NodePtr root = Parser(text).parse();
    linear_ = degree(*root) <= 1;
    ExprCompiler c;
    c.emit(*root);
    if (c.max_depth > MAX_DEPTH) throw std::invalid_argument("QOIW: QoI expression is nested too deeply");
    prog_ = std::move(c.prog);
    depth_ = c.max_depth;
}


}  // namespace qoiw
