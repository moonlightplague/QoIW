# QoIW

QoIW is a C++17 wrapper under development for preserving quantities of interest
when compressing fixed-grid scientific arrays. The libraries include a
single-variable expression engine, a sectioned container, and a lossless
correction layer that restores selected values after core decompression.
The encoder resolves point-wise tolerances from the original field and checks
them against the core reconstruction. Bound prediction, the SZ3 adapter, and
the CLI are in development for M1.
[Development plan](PLAN.md)

## Build and test

Use CMake 3.19 or newer, a C++17 compiler, and the zstd development package
(1.4 or newer). The correction layer uses zstd in all builds. The default
build also uses the pinned SZ3 submodule.

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
| `QOIW_FETCH_DEPS=ON` | Fetch zstd 1.5.6 for the correction layer if system zstd is unavailable. |
| `QOIW_BUILD_TESTS=OFF` | Build only the libraries. |
| `QOIW_WARNINGS_AS_ERRORS=ON` | Treat QoIW warnings as errors under GCC and Clang. |
| `QOIW_SANITIZE=address`, `undefined`, or `thread` | Instrument a build with one sanitizer. The default is `none`. |

The encoder uses OpenMP when available. Configure with
`-DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=ON` for a serial build. The
ThreadSanitizer job uses Clang and LLVM Archer so the race detector observes
OpenMP synchronization. [LLVM Archer documentation](https://github.com/llvm/llvm-project/blob/release/18.x/openmp/tools/archer/README.md)

The common tests use known byte sequences and the CRC32C check value for
`123456789`. The container tests cover a fixed wire fixture, truncated inputs,
and multi-field round trips. With SZ3 enabled, a separate test builds against its public C++ API
and calls its compressed-size bound function to check linkage.

## Container and lossless corrections

The provisional version 0.0 stream carries C-order dimensions, per-field bounds,
opaque decode parameters, fingerprint metadata, and one CORE section per field.
Its request header records each point-wise expression and its resolved absolute
tolerance. A point-wise record consists of kind byte `0x01`, a little-endian
float64 tolerance, and the expression text prefixed by its little-endian
`u16` byte length. The decoder reads the text as metadata.
The reader checks the header CRC32C before interpreting its contents. It returns
views into the input buffer for strings and payloads, so callers must retain the
buffer while using the parsed stream.

A field with corrections also carries one required LOSSLESS section (type
`0x11`, flags zero). The payload starts with two little-endian `u64` values
giving the correction count and the compressed byte length of the index frame.
Two zstd frames follow, each with its uncompressed size recorded. The first
holds unsigned LEB128 gaps between increasing flat indices, starting with the
absolute first index. The second holds the corresponding original float32 or
float64 bit patterns in little-endian order. Fields with zero corrections omit
the section.

Field decoding first runs the core, then validates the complete correction
layer and overwrites the selected values. The stored corrected-stream fixture
checks restoration of negative zero and a specific NaN payload. The original
M0 fixtures continue to check core-only streams. The format freeze is scheduled
for M1 and decoded-value fingerprint verification for M3.

The encoder's point-wise pass takes one original field, its core reconstruction
in the same storage type, compiled target expressions with absolute tolerances,
and optional data and exact-value constraints. It compares in double precision.
A changed value needs restoration if it exceeds any bound, either value is
non-finite, either QoI result is non-finite, or either value satisfies the exact
condition. The pass applies its serialized corrections to the reconstruction,
so the encoder sees the same final bits as a reader. The
`core.pointwise_roundtrip` test checks this for all six mock cores.

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

### Resolving point-wise tolerances

Frontend step F0 scans the original field and computes each relative tolerance
as its specified fraction times the finite range of that target. A relative
data bound uses the range of the input itself. Absolute tolerances pass through
directly. If a range is constant or contains no eligible values, the specified
fraction becomes the absolute tolerance.

An exact condition such as `x > 1e30` excludes fill values from every range.
NaN and infinite inputs are also excluded. At a finite, unmasked input where
one expression is undefined, the other targets still contribute their finite
values to their own ranges. F0 marks that input for exact preservation.

## Reference benchmarks

The M0 benchmark guide gives the pinned QPET build, the independently checked
Hurricane result, and the field inventory for parity testing. [Reference baseline](bench/reference/README.md)
