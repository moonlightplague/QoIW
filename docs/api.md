# C++ API

Include `<qoiw/qoiw.hpp>` and link `QOIW::qoiw`. A reader that only decodes
can include the same header and link `qoiw_decode`. All dimensions are in C
order (slowest-varying first), 1 to 4 of them, and every value is `float32`
or `float64`.

## Example

```cpp
#include <qoiw/qoiw.hpp>

std::vector<float> round_trip(const std::vector<float>& values, std::vector<std::uint64_t> dims) {
    qoiw::Request request;
    request.targets.push_back(qoiw::Target::pointwise("x^2").rel(1e-3));
    request.targets.push_back(qoiw::Target::pointwise("x > 273.15 ? 1 : 0").abs(0.5));
    request.data_bound = qoiw::Tol::rel(1e-2);
    request.exact = "x > 1e30";

    qoiw::Options options;
    options.threads = 4;
    qoiw::Report report;
    const auto blob = qoiw::compress(qoiw::ArrayView(values.data(), dims), request, options, &report);

    const qoiw::StreamInfo info = qoiw::inspect(blob);
    std::vector<float> decoded(info.num);
    qoiw::decompress(blob, decoded.data(), decoded.size(), options.threads);
    return decoded;
}
```

## Types (`types.hpp`)

| Type | Meaning |
| --- | --- |
| `ArrayView` | Non-owning pointer, `DType` and dimensions. Constructed from `const float*` or `const double*`. The buffer must hold the product of the dimensions. |
| `DType` | `Float32` (1) or `Float64` (2); the numbers are the container codes. |
| `Bytes` | Non-owning byte range; converts from `std::vector<std::uint8_t>`. |
| `Tol` | `Tol::abs(v)`, `Tol::rel(v)`, `Tol::none()`. Values must be finite and nonnegative. |
| `Error` | `std::runtime_error` with `code()`; see [Errors](architecture.md#errors). |

## Request (`request.hpp`)

```cpp
struct Target  { std::string expression; Tol tolerance; };   // Target::pointwise("x^2").rel(1e-3)
struct Request {
    std::vector<Target> targets;        // all must hold; point-wise, one field
    Tol data_bound = Tol::none();       // |x - x'| bound; none by default
    std::optional<std::string> exact;   // predicate; non-zero means keep bit-exact
};
```

- A relative target tolerance is a fraction of the range of `f` over the
  finite, unmasked inputs where `f` is finite. A relative data bound is a
  fraction of the range of the finite, unmasked input. If a range is empty or
  constant, or the fraction is 0, the value is used as an absolute tolerance.
  Details: [F0](frontend.md#f0-resolve-tolerances).
- Every target needs a tolerance other than `none`.
- `exact` and every target are [expressions](expressions.md) of `x`.
- A request with no targets and no data bound still keeps non-finite values
  and exact-predicate values exact; every other value is accepted as the core
  returned it.

## Options (`options.hpp`)

| Field | Default | Meaning |
| --- | --- | --- |
| `core` | `"sz3"` | Registered core name. |
| `core_options` | empty | Passed to the core. SZ3 accepts only `algo`; see [cores](cores.md#sz3). |
| `threads` | 1 | QoIW passes and the full-field core call. 0 uses the OpenMP default. |
| `allow_stream_changing_parallelism` | `false` | Let the core use a parallel mode whose stream depends on the thread count (SZ3's chunked OpenMP mode). |
| `fixed_bound` | unset | Skip tuning and give the core this bound. The backend still enforces every constraint. |
| `frontend.prior.point_samples` | 0 (= 2^20) | Values the prior is computed for. |
| `frontend.prior.quantiles` | empty (= `0, .0025, .005, .01, .02, .05, .1, .2, .5, 1`) | Initial candidates as quantiles of the prior. |
| `frontend.sampling.sample_rate` | 0.01 | Share of the input in the sample, in (0, 1]. |
| `frontend.sampling.block_edge`, `min_edge`, `min_blocks`, `max_blocks` | 0 (rank defaults) | See [F3](frontend.md#f3-sample-blocks). |
| `frontend.search.refine_rounds` | 4 | Geometric refinement rounds. |
| `frontend.search.tie_bytes` | 0.025 | Near-tie band, relative to the cheapest cost. |
| `frontend.rare_outlier_share` | 0.1 | In [0, 1]; see [F4](frontend.md#f4-measure-candidates). |
| `verify_patch` | `false` | Decode and apply the full-field lossless patch during compression. With the default, the encoder copies the original bits directly. |

## Functions (`qoiw.hpp`)

```cpp
std::vector<std::uint8_t> compress(const ArrayView& input, const Request& = {}, const Options& = {},
                                   Report* report = nullptr);
StreamInfo inspect(Bytes stream);
void decompress(Bytes stream, float* out, std::size_t capacity, int threads = 1,
                DecompressionTimes* times = nullptr);
void decompress(Bytes stream, double* out, std::size_t capacity, int threads = 1,
                DecompressionTimes* times = nullptr);
Verification verify(const ArrayView& original, const ArrayView& reconstructed, const Request& = {},
                    int threads = 1);
```

- **`compress`** runs the [pipeline](architecture.md#stages) and returns the
  container. `*report` is assigned only on success. Without a report the
  extra verification pass is skipped.
- **`inspect`** parses the header and section table without decoding and
  without needing the stream's core.
- **`decompress`** checks the storage type against the stream and the
  capacity (in elements) against the field before writing. Use the overload
  for `info.dtype`. The output is valid only after the call returns. If
  `times` is not null it is assigned the stage times on success and left
  unchanged on failure.
- **`verify`** checks two fields of the same shape and type. It resolves
  relative tolerances on `original` exactly as `compress` does and runs the
  backend check in diagnostic mode. It needs no core.

## Results (`report.hpp`)

### `Verification`

| Field | Meaning |
| --- | --- |
| `values` | Values checked. |
| `violating_values` | Values that fail at least one check, counted once. `ok()` is `violating_values == 0`. |
| `exact_violations` | Changed values where the exact predicate is non-zero on either side. |
| `data_bound_abs` | The absolute data bound, or empty. |
| `data` | `max_abs_error` over changed finite pairs; `violations`; `nonfinite_violations` (a changed value with a non-finite side, counted even without a data bound). |
| `targets[i]` | Per target, in request order: the resolved tolerance, `max_abs_error` over pairs where both QoI values are finite, `violations`, `nonfinite_violations` (either QoI value non-finite). |

Bit-identical pairs pass every check. A subtraction that overflows is reported
as infinity. Exact semantics: [backend](backend.md#checks).

### `Plan`

| Field | Meaning |
| --- | --- |
| `bound` | One bound per field (one entry in this version). |
| `reference_bound` | Same as `bound[0]` while only lossless corrections exist. |
| `correction_mode` | Always `Lossless`. |
| `binding` | `DataBound`, `Qoi`, `Tie` or empty; see [F6](frontend.md#f6-plan). |
| `data_bound_abs`, `targets`, `exact_condition` | The resolved request. |
| `predicted` | For measured searches: core, correction and total bytes per value, outlier share, sample PSNR, indicative ratio. Empty for shortcuts. |
| `trace` | Every distinct measured bound, in measurement order (`CandidateStats`). Empty for shortcuts. |
| `flags` | `whole_input_sampled`, `prior_free` (always false), `rare_outlier_fallback`, `shortcut_taken`. |
| `shortcut_reason` | `FixedBound`, `ZeroRadii`, `SingleCandidate` or empty. |

`plan.to_json()` serializes it. Infinite numbers are written as `"+inf"` or
`"-inf"`, absent values as `null`, enums as `"QOI"`, `"DATA_BOUND"`, `"TIE"`,
`"lossless"`, `"fixed_bound"`, `"zero_radii"`, `"single_candidate"`.

### `CandidateStats`

One measured bound: `bound`, `sample_values`, `restored`, raw `core_bytes`
(stream plus decode parameters), `overhead_bytes`, `lossless_bytes`,
`input_sensitive_share`, `sample_sensitive_share`, `correction_weight`,
`rare_outlier_fallback`, `outlier_share`, `correction_bytes_per_value`,
`bytes_per_value` (the cost), `sample_psnr_db`, and
`core_bytes_per_value()`. Definitions: [F4](frontend.md#f4-measure-candidates).

### `Report`

`plan`; `stream` (`StreamInfo` of the result); `nonfinite_inputs`; `restored`
(values in the LOSSLESS layer); `seconds` (`CompressionTimes`, below); and
`verification` of the corrected reconstruction against the resolved request.
`to_json()` serializes all of it.

### `CompressionTimes` and `DecompressionTimes`

Wall-clock seconds measured with `std::chrono::steady_clock`.

| Field | Stage |
| --- | --- |
| `CompressionTimes::planning` | Frontend, F0 to F6. |
| `core_encode`, `core_decode` | The full-field core compression and decompression. |
| `correction` | Backend check, LOSSLESS encoding, and either direct restoration or patch readback with `verify_patch`. |
| `container` | Assembling the in-memory container. |
| `verification` | The report's check of the corrected reconstruction (only with a report). |
| `total` | The whole `compress` call. At least the sum of the stages; the difference is buffer allocation and bookkeeping. |
| `DecompressionTimes::parse` | Parsing and validating the container header and sections. |
| `core_decode` | The core's decompression. |
| `correction` | Validating and applying the LOSSLESS layer. |
| `total` | The whole `decompress` call. |

`DecompressionTimes::to_json()` writes `{"parse", "core_decode",
"correction", "total"}`; `Report::to_json()` writes `seconds` with every
`CompressionTimes` field.

### `StreamInfo`

`dtype`, `dims`, `num` (values per field), `stream_bytes`, `wrapper_version`,
`core_name`, `core_version`, `core_format_version`, `targets`,
`exact_condition`, and per field `name`, `bound`, `data_bound_abs`,
`decode_params_bytes`, `core_bytes`, `lossless_bytes`.

## Expressions and cores

`qoiw::Expr` (`expr.hpp`) is public; see [expressions](expressions.md).
`qoiw::get_core`, `available_cores` and the `Core` interface (`core.hpp`) are
public and live in `qoiw_decode`; see [cores](cores.md).
