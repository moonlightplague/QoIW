# Testing

Every CTest suite is registered in `tests/CMakeLists.txt`. Run them with

```sh
ctest --test-dir build --output-on-failure
ctest --test-dir build -R '^frontend\.' --output-on-failure    # one group
```

Python suites need Python 3; those that need NumPy exit with code 77 and are
reported as skipped when it is missing.

## Suites

| Test | Source | Checks |
| --- | --- | --- |
| `common.byte_io` | `unit/common/byte_io.cpp` | Little-endian integers and floats, LEB128, truncation, CRC32C of `123456789`. |
| `expr.reference`, `expr.edges` | `unit/expr/` | QPET's reference values and derivatives; parser limits, special values, batch evaluation, concurrent use. |
| `container.reader`, `container.writer` | `unit/container/` | Wire fixtures, every rejected header and section form, truncations, multi-field round trips, target records. |
| `backend.lossless`, `backend.pointwise` | `unit/backend/` | Layer encoding and every validation failure; the check rules of [backend](backend.md#checks). |
| `frontend.resolve`, `.prior`, `.prepare`, `.sample`, `.measure`, `.search`, `.plan`, `.plan_json` | `unit/frontend/` | F0 to F6 on synthetic inputs, including QPET's Hurricane block layout and JSON output. |
| `core.mock_roundtrip` | `cores/mock_roundtrip.cpp` | Mock cores, the quantizer golden stream, the LOSSLESS golden stream. |
| `core.pointwise_roundtrip` | `cores/pointwise_roundtrip.cpp` | Encoder and decoder see the same bits after correction for SZ3 and every mock core. |
| `core.sz3_roundtrip`, `core.sz3_build` | `cores/sz3_*.cpp` | Both types, ranks 1 to 4, options, chunked streams with 1, 2 and 7 threads, the chunked golden stream; that SZ3's headers, generated configuration and zstd linkage work on their own. |
| `pipeline.integrated`, `pipeline.verify` | `cores/integrated.cpp`, `cores/verify.cpp` | Public `compress`, `decompress`, `inspect`, `verify`, error codes, reports. |
| `property.guarantees` | `property/guarantees.py`, `property/roundtrip.cpp` | The guarantee matrix below. |
| `reference.numpy`, `reference.numpy_cli` | `reference/` | The NumPy checker itself, and fresh CLI decodes checked with it. |
| `cli.pointwise` | `cli/pointwise.py` | Every command, both types, both shape forms, exact values, malformed arguments, exit statuses, safe output. |
| `benchmark.regret`, `benchmark.performance` | `unit/bench/` | Oracle refinement, gates, journal recovery; timing pairs and budgets. |

## Guarantee matrix

`property/guarantees.py` drives `qoiw_matrix_probe` (`property/roundtrip.cpp`).
For each case the probe compresses through the internal pipeline with the
chosen core, once serially and once with several threads, and requires
identical containers and identical checked reconstructions. It then decodes
the saved container and requires the bits the encoder checked, and writes the
decoded file. The driver checks that file with the NumPy checker against the
request and compares the tolerances stored in the container with its own.

Cases cross both storage types, SZ3 and the six mock
cores, nine field families (smooth, noisy, sparse with zeros, heavy-tailed,
constant, non-finite, `1e36` fills, denormals, threshold-crowded) in ranks 1
to 4, and 32 point-wise expressions in eight groups (smooth, oscillating,
singular, other functions, piecewise, steps, comparisons, curved). The small
profile runs in every CTest run; `-DQOIW_TEST_FULL_MATRIX=ON` selects the
expanded cross product, which CI runs nightly in Release.

## Independent NumPy checker

`tests/reference/qoi_numpy.py` shares no code with the library. It reads the
original and decoded raw files, resolves relative tolerances from the
original, evaluates each target with a NumPy formula supplied by the caller,
checks exact values by their stored bits, and allows a relative slack of
`1e-9` on numerical bounds (`relative_slack=0` for strict comparisons).
Division uses NumPy, and `c_round` rounds halves away from zero as the
library does.

```python
import numpy as np
from qoi_numpy import Target, Tolerance, check_files

targets = [Target("x^2", np.square, Tolerance("REL", 1e-3))]
report = check_files("U.f32", "U.out.f32", "<f4", (100, 500, 500), targets)
assert report["ok"], report
```

## Continuous integration

`.github/workflows/build.yml` runs on pushes and pull requests to `dev` and
`main`, and nightly against `dev`, on Ubuntu 24.04 with NumPy installed:

| Compiler | Build | Sanitizer |
| --- | --- | --- |
| g++ | Release | none (nightly: full matrix) |
| clang++ | Debug | address |
| clang++ | Debug | undefined |
| clang++-18 | Debug | thread, with LLVM Archer |

All jobs build with `-DQOIW_WARNINGS_AS_ERRORS=ON`.
