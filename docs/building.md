# Building

The short version is in the top-level [README](../README.md#build). This page
lists every option and explains the less common builds.

## Requirements

| Dependency | Version | Found by | Notes |
| --- | --- | --- | --- |
| CMake | 3.19 or newer | | SZ3's own minimum |
| C++ compiler | C++17 | | GCC or Clang for the warning and sanitizer options |
| zstd | 1.4 or newer | `find_package(zstd CONFIG)`, then pkg-config `libzstd` | Used by the LOSSLESS layer in every build. `QOIW_FETCH_DEPS=ON` fetches v1.5.6 instead. |
| SZ3 | 3.4 | the `SZ3/` submodule by default | Header-only. Not needed with `QOIW_CORE_SZ3=OFF`. |
| OpenMP | any | `find_package(OpenMP COMPONENTS CXX)`, optional | Parallel encoder passes. |
| Python 3 with NumPy | | `find_package(Python3)`, optional | Registers the Python-driven tests. Tests that need NumPy skip without it. |
| CLI11 | v2.7.2 | vendored in `third_party/CLI11` | CLI only. |

## CMake options

| Option | Default | Effect |
| --- | --- | --- |
| `QOIW_CORE_SZ3` | `ON` | Build the SZ3 adapter and register core `sz3`. With `OFF`, `available_cores()` is empty and the public `compress` reports `Unsupported`; the tests reach the pipeline with mock cores. |
| `QOIW_SZ3_SOURCE_DIR` | empty | Use another SZ3 checkout instead of `./SZ3`. |
| `QOIW_SZ3_USE_INSTALLED` | `OFF` | Use `find_package(SZ3 3.4 CONFIG)`. Set `CMAKE_PREFIX_PATH` for a custom prefix. |
| `QOIW_FETCH_DEPS` | `OFF` | Fetch zstd v1.5.6 when no system zstd is found. |
| `QOIW_BUILD_TOOLS` | `ON` | Build the `qoiw` executable. |
| `QOIW_BUILD_TESTS` | `ON` | Build and register the tests. |
| `QOIW_TEST_FULL_MATRIX` | `OFF` | Register the expanded guarantee matrix instead of the small one (only when Python is found). |
| `QOIW_BUILD_BENCHMARKS` | `OFF` | Build `qoiw_regret_prior` and, on Linux with SZ3 and OpenMP, `qoiw_performance_probe`. |
| `QOIW_WARNINGS_AS_ERRORS` | `OFF` | Add `-Werror` to QoIW targets. |
| `QOIW_SANITIZE` | `none` | `address`, `undefined` or `thread` for QoIW targets. |

SZ3 is added with `add_subdirectory(... EXCLUDE_FROM_ALL)` and its install,
tool, MDZ, ParaView and HDF5 options switched off, so it builds nothing of
its own. Its headers are marked as system headers and are outside QoIW's
warning policy. The submodule is never modified.

## Compiler flags

Every QoIW target is compiled with `-Wall -Wextra -Wpedantic -fno-fast-math
-ffp-contract=off` under GCC and Clang. The last two keep the encoder's
checks and the decoder's arithmetic from being rewritten or fused. Do not
override them.

## Serial build

```sh
cmake -S . -B build-serial -DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=ON
```

Streams and reconstructions are identical to an OpenMP build, and a serial
build decodes SZ3 streams that were written in SZ3's chunked OpenMP mode.

## Sanitizer builds

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DQOIW_SANITIZE=address
```

`undefined` also sets `-fno-sanitize-recover=undefined`. The sanitizer link
flag is public on each target, so programs that link the static libraries get
the runtime too.

ThreadSanitizer only understands OpenMP synchronization through LLVM Archer.
CI uses Clang 18 with `libomp-18-dev`, configures with
`-DCMAKE_REQUIRE_FIND_PACKAGE_OpenMP=ON`, and runs CTest as

```sh
OMP_TOOL_LIBRARIES=/usr/lib/llvm-18/lib/libarcher.so \
TSAN_OPTIONS=ignore_noninstrumented_modules=1 \
setarch x86_64 -R ctest --test-dir build-tsan --output-on-failure
```

[LLVM Archer](https://github.com/llvm/llvm-project/blob/release/18.x/openmp/tools/archer/README.md)

## Using QoIW from another CMake project

There are no install rules yet. Add the repository as a subdirectory and link
the target you need.

```cmake
set(QOIW_BUILD_TOOLS OFF)
set(QOIW_BUILD_TESTS OFF)
add_subdirectory(external/qoiw)
target_link_libraries(writer PRIVATE QOIW::qoiw)   # compress, verify, Expr
target_link_libraries(reader PRIVATE qoiw_decode)  # inspect, decompress only
```

`qoiw_decode` carries the SZ3 adapter and zstd but no expression engine or
frontend.
