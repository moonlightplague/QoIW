# Architecture

## Stages

`qoiw::compress` runs three stages on one field.

```
input field, Request, Options
        |
        v
FRONTEND  src/frontend/
  F0 resolve   ranges, absolute tolerances, exact-value mask        resolve.cpp
  F1 prior     per-value radii on ~2^20 strided points, candidates  prior.cpp
  F2 shortcut  fixed bound | all radii zero | single affine cand.   prepare.cpp
  F3 sample    Halton-placed blocks copied from the input           sample.cpp
  F4 measure   core round trip + backend on every block, cost       measure.cpp
  F5 search    candidates, geometric refinement, tie rule           search.cpp
  F6 plan      bound, binding, prediction, trace                    plan.cpp
        | Plan::bound[0]
        v
CORE      src/core/adapters/sz3.cpp
  compress the whole field once, decompress it once
        | raw reconstruction
        v
BACKEND   src/backend/
  check every value, record indices to restore                      pointwise.cpp
  encode the LOSSLESS layer, apply it to the reconstruction         lossless_*.cpp
  optional: verify the corrected reconstruction (report only)
        |
        v
CONTAINER src/container/writer.cpp
  header + CORE section + optional LOSSLESS section + END
```

`qoiw::decompress` parses the container, looks the core up by its stored
name, runs the core's decompression into the caller's buffer, and applies
the LOSSLESS layer (`src/pipeline/decode_field.cpp`). It needs no request
and no expression evaluation.

The frontend calls the same backend function (`backend::correct_pointwise`)
on each sample block that the pipeline later calls on the whole field, and
prices the result with the same codec. Candidate costs therefore include the
real size of the corrections.

## Guarantee

For every value of the field, after decoding:

| Constraint | Holds as |
| --- | --- |
| Point-wise target `f` with absolute tolerance `tau` | `\|f(x) - f(x')\| <= tau`, evaluated in double precision |
| Data bound `eps` | `\|x - x'\| <= eps` |
| Exact predicate `g` | if `g(x)` or `g(x')` is non-zero, `x'` has the bits of `x` |
| Non-finite input, or non-finite QoI on either side | `x'` has the bits of `x` |

The guarantee rests on four properties of the code, which a change must keep.

1. The backend checks the reconstruction that the core actually produced,
   in the storage type, and restores every failing value from the original
   bits. The prior, the sample and the search never decide correctness.
2. The encoder computes the final values by decoding its own LOSSLESS
   payload into the reconstruction (`apply_lossless`), the same function the
   decoder runs. What it checks is what a reader gets.
3. A restored value is a copy of the original bits, so the correction pass
   cannot fail to terminate or create new violations.
4. The core's decompression is deterministic for a given stream. The SZ3
   adapter keeps the stream independent of the thread count unless
   `allow_stream_changing_parallelism` is set; the decoder then honours the
   stored chunk layout. The planned fingerprint check is not implemented yet.

Comparisons are written as `!(error <= tol)` so a NaN error fails.

## Libraries

| Target | Sources | Use |
| --- | --- | --- |
| `qoiw_decode` (static) | `common/byte_reader`, `core/builtin`, `core/adapters/sz3`, `container/reader`, `backend/lossless_apply`, `pipeline/decompress`, `pipeline/decode_field` | Readers: `inspect`, `decompress`, the core registry. No expression engine, no frontend. |
| `qoiw` (static, alias `QOIW::qoiw`) | everything else; links `qoiw_decode` publicly | Writers: `compress`, `verify`, `Expr`. |
| `qoiw_cli` (executable `qoiw`) | `tools/qoiw/` | Command line. |
| `qoiw_mock_cores` (tests only) | `src/core/adapters/mock.cpp` | Adversarial cores for tests. |

## Source layout

```
include/qoiw/       public headers
  qoiw.hpp          compress, decompress, inspect, verify
  types.hpp         Bytes, DType, ArrayView, Tol, Error, ErrorCode
  request.hpp       Target, Request
  options.hpp       Options, FrontendOptions and its parts
  report.hpp        Plan, CandidateStats, Report, StreamInfo, Verification
  core.hpp          Core, CoreParams, CoreOutput, CoreTraits, registry
  expr.hpp          Expr, Jet
src/
  common/           little-endian byte I/O, LEB128, CRC32C, shapes, parallel_for
  expr/             compiler (parser to bytecode), evaluate, jet (forward AD)
  qoi/              ResolvedPointRequest: compiled targets with absolute tolerances
  frontend/         F0 to F6 and the JSON report serializer
  core/             built-in registry; adapters/sz3.cpp, adapters/mock.cpp
  backend/          point-wise check, lossless encode and apply
  container/        reader, writer, section constants
  pipeline/         compress, decompress, verify, request compilation
tools/qoiw/         CLI: arguments, file staging, commands (main), text reports (render, console)
tests/              see testing.md
bench/              benchmark scripts and case lists; runbooks in docs/benchmarks
third_party/CLI11/  vendored CLI11 v2.7.2
```

## Threading

`Options::threads` (0 means the OpenMP default) is used for QoIW's own
passes through `common::parallel_for`, a static OpenMP loop that rethrows the
first exception on the calling thread. Every iteration writes only its own
output, and results are merged in index order, so streams and reconstructions
do not depend on the thread count. Without OpenMP the loops run serially.

The parallel passes are F0 resolution (chunks of 4096 values), the F1 prior,
block copying in F3, the per-block round trips in F4 (one core thread per
block; serial if the core is not `thread_safe`), and the backend check
(chunks of 2048 values). The full-field core call receives
`Options::threads`.

## Errors

The public functions throw `qoiw::Error` with an `ErrorCode`:

| Code | Raised for |
| --- | --- |
| `InvalidArgument` | null or empty input, shape and size mismatch, negative threads, invalid tolerance or fixed bound, an expression that does not parse, a resolved tolerance that overflows, an undersized or mistyped decode buffer |
| `Unsupported` | unknown core name, a shape or type the core rejects, an unsupported container version, section or target kind, a multi-field stream |
| `CorruptStream` | bad magic, CRC, framing, sizes or indices in the container or the LOSSLESS layer, a core stream that disagrees with the header |
| `CoreFailure` | any other exception from a core during compression |
| `Internal` | any other unexpected exception |

`std::bad_alloc` propagates unchanged.
