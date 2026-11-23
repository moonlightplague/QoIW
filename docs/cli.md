# Command line: behaviour and reports

The inputs of each command are specified in the top-level
[README](../README.md#command-line). This page covers what the commands do,
what they print, and how they handle files. The implementation is in
`tools/qoiw/`; parsing uses the vendored CLI11 v2.7.2
([licence](../LICENSES/CLI11.txt)).

## Commands

| Command | Does |
| --- | --- |
| `compress` | Reads the raw file, calls `qoiw::compress` with a report, optionally decodes and re-verifies (`-a`), writes the container (`-z`) only if every check passed, prints the report. |
| `decompress` | Inspects the container, checks that the core is available and supports the shape, decodes, writes the raw output, prints the stream and its runtime. |
| `inspect` | Prints the stream metadata and compression ratio without decoding. |
| `verify` | Reads two raw files, calls `qoiw::verify`, prints the checks and runtime. |
| `cores` | Prints a table of the built-in cores: version, format, and the traits per type and range of ranks. |

## Text reports

Every report starts with the command and its files, continues with titled
sections, and, except for `cores`, ends with a rule and a highlighted summary
line starting with `==>`. Numbers are rounded for reading:
bounds and errors to six significant digits, byte and value counts with digit
grouping, times in milliseconds below one second. JSON output keeps full
precision.

A `compress -a` report for Hurricane `CLOUDf48.bin.f32` with `--shape 100,500,500 -q 'x^2' -m REL 1e-3 -t 128`:

```
qoiw compress  CLOUDf48.bin.f32  ->  CLOUDf48.qoiw

Input
  type            float32, 100 x 500 x 500  (25,000,000 values, 100 MB)
  core            sz3 3.4.0, stream format 3.4.0
  threads         128

Request
  target 1        x^2    REL 0.001  ->  4.19411e-09 absolute
  data bound      none

Plan
  bound           5.09716e-06 absolute, chosen by the QoI targets after 16 measured bounds
  predicted       0.0408 B/value  (core 0.0348, corrections 0.0061), ratio ~98.0
  sample          992,250 values in blocks

Stream
  Part                       bytes     share
  core stream              963,280    89.4 %
  lossless corrections     114,624    10.6 %   26,213 values restored (0.105 %)
  header and framing           153     0.0 %
  total                  1,078,057   100.0 %   from 100 MB

Verification
  Check   tolerance (abs)     max error   violations   non-finite
  data               none   5.09716e-06            0            0
  x^2         4.19411e-09   4.19406e-09            0            0

  on encode       PASSED  0 of 25,000,000 values violate a requirement
  on decode (-a)  PASSED  0 of 25,000,000 values violate a requirement

Runtime
  Stage                              ms     share
  frontend (tuning)              154.88    26.3 %
  core                           402.75    68.5 %
    compress                     272.85    46.4 %
    decompress                   129.90    22.1 %
  backend                         16.07     2.7 %
    check and correct             16.02     2.7 %
    assemble container             0.05     0.0 %
  encoder check                    4.19     0.7 %
  other (buffers, bookkeeping)      9.94     1.7 %
  total                          587.83   100.0 %
  read input                      49.45
  write stream                     0.81
  decode container (-a)          116.86
  verify decoded (-a)             16.82

------------------------------------------------------------------------
==>   Compression ratio 92.76   Total time 587.8 ms (170.1 MB/s)   PASSED
```

### Sections

| Section | Contents |
| --- | --- |
| Input | Type, shape in C order, values and raw size; core with its options; threads; non-finite inputs if any. |
| Request | Each target with the tolerance as given and, for `REL`, the resolved absolute value; the data bound likewise; the exact predicate. |
| Plan | The bound and how it was chosen (by the QoI targets, the data bound, a near-tie, or a shortcut); for measured searches the predicted bytes per value, the indicative ratio and the sample size. |
| Stream | Bytes and share of the core stream (with decode parameters), the LOSSLESS layer (with the number of restored values) and the container framing. |
| Verification | One row per check with tolerance, largest error, violations and non-finite violations; a row that fails is bold. The table shows the decoded container's check when `-a` is given, otherwise the encoder's. Below it the result of each check. |
| Measured bounds | With `-v` only: every measured bound in order with its cost, core and correction bytes per value, correction weight, restored count, sample PSNR and rare-outlier note; `*` marks the chosen one. |
| Runtime | Wall-clock time and share of each stage, see below. |

`decompress` prints the stream (type, core, field bound, section sizes,
targets, size) and its runtime, then the compression ratio and total time.
`inspect` prints the stream and the compression ratio. `verify` prints the
input, the request, the checks and its runtime, and ends with `PASSED` or
`FAILED`.

### Runtime stages

The compression and decompression totals measure the in-memory codec calls,
using the same boundaries as QPET's reported compression and decompression
times. File I/O and the optional `compress -a` decode and verification appear
below the total without a percentage share.

| Command | Stage | Measured as |
| --- | --- | --- |
| `compress` | read input | reading and byte-order conversion of the raw file |
| | frontend (tuning) | `CompressionTimes::planning`: F0 to F6 |
| | core: compress, decompress | `core_encode` and `core_decode`: the full-field core calls |
| | backend (check and correct, assemble container) | `correction` repairs the field. `container` assembles the stream in memory. |
| | encoder check | `verification`, the report's check of the corrected reconstruction |
| | decode container (-a), verify decoded (-a) | `qoiw::decompress` and `qoiw::verify` on the finished container |
| | write stream | staging and renaming the container, when it is written |
| | total | `CompressionTimes::total` covers the `qoiw::compress` call. |
| `decompress` | read stream, write output | reading the compressed file and writing the raw field, including output byte-order conversion |
| | parse container, core decompress, apply corrections | `DecompressionTimes` stages |
| | total | `DecompressionTimes::total` covers the `qoiw::decompress` call, timed after inspection and output buffer allocation. |
| `verify` | read inputs, verify, total | reading both raw files, verifying the fields, and the full command up to report printing |
| all | other (buffers, bookkeeping) | the total minus the stages counted towards it |

Throughput in the summary is the raw field size divided by the total.

### Colour

`--color auto` (the default) styles headings, the summary and `PASSED` or
`FAILED` with ANSI escapes only when standard output is a terminal, `NO_COLOR`
is unset or empty, and `TERM` is not `dumb`. `always` and `never` force it.
Redirected output and JSON never contain escapes in `auto` mode.

## JSON reports

With `--json`:

| Command | Output |
| --- | --- |
| `compress` | `{"report": Report, "post_decode": Verification or null, "runtime": {...}}` |
| `decompress` | `{"stream": StreamInfo, "runtime": {...}}` |
| `inspect` | `StreamInfo` |
| `verify` | `Verification` |

The objects are the serializations described in the [C++ API](api.md#results-reporthpp).
`report.plan` includes the full trace regardless of `-v`, and
`report.seconds` holds the library's stage times. The `runtime` objects
report codec and ancillary timings in seconds.

| Command | `runtime` fields |
| --- | --- |
| `compress` | `read_input`, `compress` (= `report.seconds.total`), `post_decode` (`{"decompress": DecompressionTimes, "verify": s}` with `-a`, else `null`), `write_stream` (`null` when nothing was written), `total` (= `compress`) |
| `decompress` | `read_stream`, `decompress` (`DecompressionTimes`: `parse`, `core_decode`, `correction`, `total`), `write_output`, `total` (= `decompress.total`) |

Before this change `decompress --json` printed the bare `StreamInfo`; read it
from `stream` now.

## Exit status

| Status | Meaning |
| --- | --- |
| 0 | Success. |
| 2 | A verification found violations (`compress` or `verify`). `compress` then does not write the container. |
| 1 | Argument, file, or processing error; the message goes to standard error with the prefix `qoiw error:`. |

## Files

- Raw files are little-endian `float32` or `float64` in C order. The size
  must equal the product of the extents times the value size, and the file
  must not change while it is read.
- Outputs are written to a temporary file in a fresh hidden directory next to
  the destination and renamed into place, so a failed run leaves no partial
  file. An existing destination must be a regular file.
- `compress` refuses a `-z` path and `decompress` refuses a `-o` path that
  refers to the input file.
- Without `-z`, `compress` measures and reports but writes nothing.
