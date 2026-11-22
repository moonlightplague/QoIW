# Expressions

Targets and the exact predicate are text expressions of one variable `x`,
compiled once by `qoiw::Expr` (`include/qoiw/expr.hpp`, `src/expr/`). The
parser, bytecode, evaluation, differentiation and reference tests are adapted
from public QPET commit `a80e6861fc15e7a66eb1f36b6172911a541b9d3c`
([licence](../LICENSES/QPET.txt)).

```cpp
qoiw::Expr f("x < 0 ? 0 : x^2");
f.eval(3);                 // 9
qoiw::Jet j = f.jet(3);    // j.v = 9, j.d1 = 6, j.d2 = 2
f.eval(in, out, n);        // batch; in and out may be the same array
f.is_linear();             // false
```

## Grammar

| Element | Forms |
| --- | --- |
| Variable | `x` |
| Numbers and constants | `2`, `0.5`, `1e-3`, `pi`, `e`, `nan`, `inf` |
| Arithmetic | `+ - * /`, `^` or `**` (right-associative), unary `-` and `+`, `( )`, `\|...\|` for absolute value |
| Implicit product | a number before a name or parenthesis: `10x`, `2(x+1)` |
| Functions | `sin cos tan asin acos atan sinh cosh tanh exp log ln log2 log10 sqrt cbrt abs sigmoid`, `log(x, base)`, `pow(a, b)`, `min(a, b)`, `max(a, b)` |
| Steps | `floor`, `ceil`, `round` (halves away from zero, unlike NumPy), `sign` |
| Comparisons and logic | `< <= > >= == !=`, `and`, `or`, `not` |
| Conditionals | `c ? a : b`, `if(c, a, b)` |

`log` and `ln` are the natural logarithm. An expression that does not parse
throws `std::invalid_argument`, reported by the API as `InvalidArgument`.

Limits: 64 levels of syntax nesting, 512 levels in the folded syntax tree,
and 64 evaluation stack slots.

## Evaluation

The text is compiled to a postfix program with constant folding and run in
double precision with the C math library. Encoder checks and `verify` use the
same evaluation, so a check that sits exactly on a tolerance can differ from
an analysis that uses a mathematically equal formula or single precision.

For a predicate, any non-zero result (including NaN) counts as true.

## Derivatives

`jet(x)` returns the value, first and second derivative from second-order
forward-mode automatic differentiation, plus two distances used by the prior:

- `dist`: estimated distance in `x` to the nearest singularity (for example
  0 for `log`), or `+inf`;
- `brk`: estimated distance to the nearest point where a condition of a
  piecewise function flips, or `+inf`.

`is_linear()` is true when the compiled arithmetic is provably `a x + b` with
finite coefficients. The F2 single-candidate shortcut requires it for every
target.

## Not supported yet

Several variables, regional aggregates (`mean`, `sum`, `var`), `dec()` and
`err()`, and the `float32` threshold rule of plan section 6.5 are later
milestones.
