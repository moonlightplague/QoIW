# QoIW

QoIW is a C++17 wrapper under development for preserving quantities of interest
when compressing fixed-grid scientific arrays. M0 includes the build, SZ3 dependency integration,
binary I/O, a container for streams with zero QoI targets, test-only mock cores,
and a single-variable expression engine. The compression pipeline and CLI are
planned for M1. [Development plan](PLAN.md)

## Build and test

Use CMake 3.19 or newer and a C++17 compiler. The default build uses the pinned
SZ3 submodule. On Linux, install the zstd development package to build SZ3
against the system library.

```sh
git submodule update --init SZ3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
(cd build && ctest --output-on-failure)
```

| CMake option | Effect |
| --- | --- |
| `QOIW_CORE_SZ3=OFF` | Build and test the libraries without SZ3. |
| `QOIW_SZ3_SOURCE_DIR=/path/to/SZ3` | Use a compatible SZ3 source checkout. |
| `QOIW_SZ3_USE_INSTALLED=ON` | Find the SZ3 3.4 CMake package. Use `CMAKE_PREFIX_PATH` for a custom install prefix. |
| `QOIW_BUILD_TESTS=OFF` | Build only the libraries. |
| `QOIW_WARNINGS_AS_ERRORS=ON` | Treat QoIW warnings as errors under GCC and Clang. |
| `QOIW_SANITIZE=address`, `undefined`, or `thread` | Instrument a build with one sanitizer. The default is `none`. |

The common tests use known byte sequences and the CRC32C check value for
`123456789`. The container tests cover a fixed wire fixture, truncated inputs,
and multi-field round trips. With SZ3 enabled, a separate test builds against its public C++ API
and calls its compressed-size bound function to check linkage.

## M0 container

The provisional version 0.0 stream carries C-order dimensions, per-field bounds,
opaque decode parameters, fingerprint metadata, and one CORE section per field.
The reader checks the header CRC32C before interpreting its contents. It returns
views into the input buffer for strings and payloads, so callers must retain the
buffer while using the parsed stream.

The reader skips informational extensions and rejects unsupported correction
layers. The format freeze is scheduled for M1 and decoded-value fingerprint
verification for M3.

## Mock cores

The M0 integration test wraps the output of a test core in an empty-request
container, then selects the decoder by the stored core name. Run it with
`ctest --test-dir build -R core.mock_roundtrip --output-on-failure`.
The adapters build with `QOIW_BUILD_TESTS=ON`.

| Core | Decoded values |
| --- | --- |
| `mock:identity` | Original IEEE bits, including signed zero and NaN payloads. |
| `mock:quantizer` | Nearest multiple of twice the bound, with halfway cases rounded away from zero. The stream stores exact bits for exceptional values, out-of-range indices, and reconstructions that would exceed the bound after storage rounding. |
| `mock:noise` | Alternating positive and negative offsets of `factor * bound`. Set the `factor` core option to a number greater than 1. The default is 2. |
| `mock:garbage` | Alternating +1 and −1 regardless of input. |
| `mock:signflip` | Flipped signs where the absolute input value is at most the bound. |
| `mock:nan` | Quiet NaNs at even flat indices, starting at zero. |

The quantizer golden stream contains a 1 × 4 float32 array compressed at bound
0.25. Its stored step of 0.5 and signed indices −3, −1, 0, 2 reconstruct
−1.5, −0.5, 0, 1. The test checks both the wire bytes and these decoded values.

## Point-wise expressions

Link against `QOIW::qoiw` and include `<qoiw/expr.hpp>` to compile a formula
once and evaluate it on doubles. For example, `qoiw::Expr f("x < 0 ? 0 : x^2")`
gives `f.eval(3) == 9`. At the same input, `f.jet(3)` gives first derivative 6
and second derivative 2. Use `f.eval(input, output, count)` for batches. Input
and output can be the same array.

The grammar supports comparisons, conditional branches, implicit multiplication
such as `10x`, and the constants `pi`, `e`, `nan`, and `inf`. Powers associate
to the right. `round` rounds halfway cases away from zero. The jet fields
`dist` and `brk` give local estimates of the distance in `x` to a singularity
and a breakpoint, respectively. A positive infinity indicates that the
analysis found no such boundary. The compiler supports 64 levels of syntax
nesting, 512 levels in the folded syntax tree, and 64 evaluation stack slots.

The parser, bytecode evaluation, differentiation, and reference tests are
adapted from public QPET commit `a80e6861fc15e7a66eb1f36b6172911a541b9d3c`.
[QPET BSD 3-Clause license](LICENSES/QPET.txt)

## Reference benchmarks

The M0 benchmark guide gives the pinned QPET build, the independently checked
Hurricane result, and the field inventory for parity testing. [Reference baseline](bench/reference/README.md)
