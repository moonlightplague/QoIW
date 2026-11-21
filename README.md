# QoIW

QoIW is a C++17 wrapper under development for preserving quantities of interest
when compressing fixed-grid scientific arrays. The first M0 components are the
build, SZ3 dependency integration, and the binary I/O primitives for the
container. The compression pipeline and CLI are planned for M1. [Development plan](PLAN.md)

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
| `QOIW_CORE_SZ3=OFF` | Build and test the common library without SZ3. |
| `QOIW_SZ3_SOURCE_DIR=/path/to/SZ3` | Use a compatible SZ3 source checkout. |
| `QOIW_SZ3_USE_INSTALLED=ON` | Find the SZ3 3.4 CMake package. Use `CMAKE_PREFIX_PATH` for a custom install prefix. |
| `QOIW_BUILD_TESTS=OFF` | Build only the libraries. |
| `QOIW_WARNINGS_AS_ERRORS=ON` | Treat QoIW warnings as errors under GCC and Clang. |
| `QOIW_SANITIZE=address`, `undefined`, or `thread` | Instrument a build with one sanitizer. The default is `none`. |

The common tests use known byte sequences and the CRC32C check value for
`123456789`. With SZ3 enabled, a second test builds against its public C++ API
and calls its compressed-size bound function to check linkage.
