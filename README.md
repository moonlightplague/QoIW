# QoIW

QoIW compresses fixed-grid scientific arrays with an unmodified lossy
compressor (SZ3) so that user-stated quantities of interest (QoIs) stay
within their tolerances. It predicts one error bound for the compressor from
a sample, compresses once, checks every decoded value against the original,
and stores the values that break a tolerance losslessly.

Technical documentation: [docs/](docs/README.md) · Development plan:
[PLAN.md](docs/PLAN.md) · [Changelog](CHANGELOG.md)

## Build

You need CMake 3.19 or newer, a C++17 compiler and the zstd development
package (1.4 or newer). OpenMP is used when found.

```sh
git submodule update --init SZ3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The tool is `build/qoiw`. The build includes SZ3. Set `-DQOIW_FETCH_DEPS=ON`
to fetch zstd when no system package is found or `-DQOIW_BUILD_TESTS=OFF`
to skip the tests. See [build options](docs/building.md).

## Quick start

Keep `x^2` within 0.1 % of its range at every value of a `100 × 500 × 500`
float32 field, check the result, and decode it:

```sh
build/qoiw compress   -f --shape 100,500,500 -i U.f32 -q 'x^2' -m REL 1e-3 -z U.qoiw -a
build/qoiw inspect    -z U.qoiw
build/qoiw decompress -z U.qoiw -o U.out.f32
build/qoiw verify     -f --shape 100,500,500 -i U.f32 -o U.out.f32 -q 'x^2' -m REL 1e-3
```

On Hurricane ISABEL `Uf48.bin.f32` this gives a 4.5 MB stream (ratio 22.05),
with 308,049 values restored and every value within tolerance. Each command
prints a sectioned report with a runtime breakdown and ends with a summary line
of the compression ratio and total time; an annotated example is in
[docs/cli.md](docs/cli.md#text-reports).

From C++, link `QOIW::qoiw`:

```cpp
#include <qoiw/qoiw.hpp>

qoiw::Request request;
request.targets.push_back(qoiw::Target::pointwise("x^2").rel(1e-3));
const auto blob = qoiw::compress(qoiw::ArrayView(data.data(), {100, 500, 500}), request);

std::vector<float> decoded(qoiw::inspect(blob).num);
qoiw::decompress(blob, decoded.data(), decoded.size());
```

API reference: [docs/api.md](docs/api.md).

## Add your own core

1. In `src/core/adapters/`, write `mycore.cpp` to wrap your compressor's
   public API as a `qoiw::Core`. Declare `const Core& mycore_core();` inside
   `namespace qoiw` in `mycore.hpp` and include that header in `mycore.cpp`.
   Have the factory return a static adapter instance whose `name()` is
   `"mycore"`. Follow the [SZ3 adapter](src/core/adapters/sz3.cpp) and
   [header](src/core/adapters/sz3.hpp).
2. Add `src/core/adapters/mycore.cpp` to `QOIW_CORE_SOURCES` in
   [CMakeLists.txt](CMakeLists.txt). If your compressor needs a library,
   link it to `qoiw_decode`. CMake picks up the matching header and factory
   from the source filename.
3. Rebuild with `cmake --build build --parallel`. Check `build/qoiw cores`,
   then pass `--core mycore` to `build/qoiw compress` or set
   `options.core = "mycore"` in C++. Decoding picks the core from the file. See the
   [adapter contract](docs/cores.md#interface) for buffer and threading rules.

## Command line

```
qoiw compress   (-f|-d) (--shape N[,N...] | -1 .. -4 extents) -i FILE [targets] [-M ...] [-X PRED]
                [-c CORE] [--core-option NAME=VALUE]... [--fixed-bound B]
                [--allow-stream-changing-parallelism] [-z FILE] [-a] [-v] [-t N] [--json]
qoiw decompress -z FILE -o FILE [-t N] [--json]
qoiw inspect    -z FILE [--json]
qoiw verify     (-f|-d) (--shape ... | -1 .. -4 ...) -i FILE -o FILE [targets] [-M ...] [-X PRED] [-t N] [--json]
qoiw cores

Every command also takes --color auto|always|never.
```

`qoiw <command> --help` prints the same options.

### Raw data

| Option | Meaning |
| --- | --- |
| `-f`, `--float32` / `-d`, `--float64` | Value type of the raw files. Exactly one is required. |
| `--shape N[,N...]` | 1 to 4 positive extents in **C order**, slowest-varying first: `--shape 100,500,500` is `a[100][500][500]`. |
| `-1 nx`, `-2 nx ny`, `-3 nx ny nz`, `-4 nx ny nz nw` | The same shape, **fastest-varying first** (the SZ3 tool's convention): `-3 500 500 100` equals `--shape 100,500,500`. Give either this or `--shape`, not both. |
| `-i`, `--input FILE` | Original raw file: little-endian values, exactly the product of the extents times 4 or 8 bytes. |
| `-o`, `--reconstructed FILE` | `verify` only: the reconstructed raw file to check. |

### Requirements

| Option | Meaning |
| --- | --- |
| `-q`, `--qoi EXPR` | Starts a point-wise target, a function of `x` such as `'x^2'`, `'ln(x)'`, `'x > 273.15 ? 1 : 0'`. Repeat for several targets; all must hold. Syntax: [docs/expressions.md](docs/expressions.md). |
| `-m`, `--tolerance ABS V` / `REL F` | Required once after each `-q`, for that target: an absolute tolerance `V`, or a fraction `F` of the QoI's value range over the input. Values are finite and nonnegative. |
| `-M`, `--data-bound ABS V` / `REL F` / `NONE` | Bound on `\|x - x'\|` for every value; `REL` is a fraction of the data's value range. Default `NONE`. |
| `-X`, `--exact PRED` | Values where the predicate is non-zero (e.g. `'x > 1e30'` for fill values) are reproduced bit for bit. NaN and infinite values always are. |

Relative values use the range over finite values outside the exact
predicate. A constant or empty range makes the fraction an absolute value.

### Compression and output

| Option | Meaning |
| --- | --- |
| `-c`, `--core NAME` | Compressor; `sz3` (default). `qoiw cores` lists what the build has. |
| `--core-option NAME=VALUE` | Option for the core. SZ3: `algo=interp_lorenzo` (default), `interp`, `lorenzo_reg` or `nopred`. |
| `--fixed-bound B` | Give the core this absolute bound instead of tuning one. All requirements are still enforced. |
| `--allow-stream-changing-parallelism` | Let SZ3 use its chunked OpenMP mode, whose stream depends on the thread count. |
| `-z`, `--stream FILE` | Container to write (`compress`) or read. Without it, `compress` only measures. |
| `-o`, `--output FILE` | `decompress`: raw file to write, little-endian, C order. |
| `-a`, `--verify` | `compress`: decode the finished container and check it again before writing. |
| `-v`, `--verbose` | `compress`: print every bound the tuner measured. |
| `-t`, `--threads N` | Threads; default 1, `0` uses the OpenMP default. |
| `--json` | Print a JSON report instead of text. |
| `--color auto\|always\|never` | Style the text report. `auto` (default) colours only a terminal and honours `NO_COLOR`. |

### Exit status

`0` success; `2` a requirement is violated (`compress` then writes nothing);
`1` an argument, file or processing error. Report fields and file handling:
[docs/cli.md](docs/cli.md).

## Licences

QoIW adapts its expression engine and tuning from public QPET
([BSD 3-Clause](LICENSES/QPET.txt)) and bundles CLI11 v2.7.2
([licence](LICENSES/CLI11.txt)). SZ3 is a submodule under its own licence.
