# Container format, version 0.0

Provisional. The layout below is what `src/container/writer.cpp` writes and
`reader.cpp` accepts. Integers and floats are little-endian on every host.

```
magic         4 bytes   "QOIW"
major         u8        0
minor         u8        0
flags         u16       0
header_len    u32       length of the header body
header body   header_len bytes
header_crc    u32       CRC32C of the header body
sections      until END
```

## Header body

```
dtype           u8      1 float32, 2 float64
ndims           u8      1 .. 4
nfields         u8      1 .. 255 (the API writes and reads 1)
reserved        u8      0
dims            u64 × ndims, C order, all singleton axes kept
wrapper_version str     "0.0"
core_name       str     non-empty, e.g. "sz3"
core_version    str     e.g. "3.4.0"
core_format     str     the core's stream format version
per field:
  name            str     "x"
  bound           f64     bound given to the core; finite, >= 0
  data_bound      f64     absolute data bound, 0 when absent
  field_flags     u8      bit 0: data bound present
  decode_params   u32 length + bytes, opaque to QoIW (empty for SZ3)
  fingerprint     u64     reserved; written as 0, not checked
ntargets        u8      0 .. 255
per target:
  kind            u8      1 point-wise
  tolerance_abs   f64     finite, >= 0
  expression      str     non-empty
exact           str     the exact predicate, empty when absent
```

`str` is a `u16` byte length followed by the bytes. Targets and the exact
predicate are recorded for provenance; decoding does not evaluate them.

## Sections

```
type     u8
field    u8      field index
flags    u16     bit 0: skippable
length   u64
payload  length bytes
```

| Type | Name | Rules |
| --- | --- | --- |
| `0x01` | CORE | Required once per field, flags 0. The core's stream byte for byte. |
| `0x11` | LOSSLESS | At most once per field, non-empty, flags 0. See [backend](backend.md#lossless-layer). |
| `0x10`, `0x12`, `0x20` | RESIDUAL, STEPS, TRANSFORM | Reserved for later milestones; rejected as unsupported. |
| `0x7E` | NOTE | Ignored. |
| `0xFF` | END | Last; field 0, flags 0, empty, nothing may follow. Every field must have had a CORE section. |
| other | | Skipped if the skippable flag is set, otherwise rejected as unsupported. |

The writer emits, for each field, CORE then LOSSLESS if present, then END.

## Reader behaviour

- A different major version, unknown stream flags, a non-zero reserved byte,
  unknown field flags or an unknown target kind are `Unsupported`. A newer
  minor version is accepted.
- Bad magic, a CRC mismatch, an invalid rank or field count, a shape whose
  byte size is zero or overflows, an empty core name, an invalid bound or
  tolerance, trailing header bytes, and every truncation are `CorruptStream`.
- Strings and payloads are returned as views into the input buffer, which
  must stay alive while they are used.
- Every length is checked against the bytes that remain before it is used.

## Fixtures

`tests/golden/` holds stored streams that the tests decode and, where noted,
compare byte for byte with fresh output: `m0-core.qoiw` (core-only header),
`m0-mock-quantizer.qoiw`, `m1-lossless.qoiw` (LOSSLESS layer restoring
negative zero and a NaN payload), `m1-point-targets.qoiw` (target records),
and `m1-sz3-chunked.qoiw` (a 16 × 32 float32 SZ3 stream in two OpenMP chunks with a LOSSLESS layer, decoded to the same bits with 1, 2 and 7 threads and by serial builds).
