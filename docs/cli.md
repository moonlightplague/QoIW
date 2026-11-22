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
| `decompress` | Inspects the container, checks that the core is available and supports the shape, decodes, writes the raw output, prints the stream metadata. |
| `inspect` | Prints the stream metadata without decoding. |
| `verify` | Reads two raw files and calls `qoiw::verify`. |
| `cores` | Lists each built-in core with version, format version, and the traits for every supported type and rank. |

## Text reports

`compress` prints the stream metadata (core, type, shape, values, stream
bytes, per-field bound, data bound and section sizes, targets, exact
predicate), then

```
Compression ratio=<raw bytes / stream bytes> restored=<n> nonfinite_inputs=<n>
Verification passed|failed values=... violating_values=... exact_violations=...
Data bound_abs=<v|none> max_abs_error=... violations=... nonfinite_violations=...
QoI "<expr>" tolerance_abs=... max_abs_error=... violations=... nonfinite_violations=...
```

The first verification block is the encoder's check of the corrected
reconstruction. With `-a`, a second block follows under
`Finished container check`: the result of decoding the finished container and
verifying it with `qoiw::verify`. With `-v`, one `Trial` line per measured
bound shows every `CandidateStats` field, or `Bound chosen by shortcut`.
Numbers are printed with 17 significant digits.

## JSON reports

With `--json`:

| Command | Output |
| --- | --- |
| `compress` | `{"report": Report, "post_decode": Verification or null}` |
| `decompress`, `inspect` | `StreamInfo` |
| `verify` | `Verification` |

The objects are the serializations described in the [C++ API](api.md#results-reporthpp).
`report.plan` includes the full trace regardless of `-v`.

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
