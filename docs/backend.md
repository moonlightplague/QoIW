# Backend: checking and correcting the reconstruction

The backend (`src/backend/`) receives the original field, the core's raw
reconstruction of the same type and size, and the resolved request. It does
not depend on the frontend or on any core, so the frontend can run it on
sample blocks and `verify` can run it on any two arrays.

## Checks

`correct_pointwise` and `verify_pointwise` share one kernel, `check_chunk`,
which works on chunks of 2048 values in parallel. A chunk that is
bit-identical to the original (`memcmp`) is skipped. Within a chunk, a value
whose bits are unchanged passes every check. For a changed value, in double
precision:

| Check | Restore if |
| --- | --- |
| Non-finite | the original or the reconstruction is NaN or infinite |
| Data bound `eps` (if set) | `!(abs(x - x') <= eps)` |
| Exact predicate `g` (if set) | `g(x) != 0` or `g(x') != 0` |
| Each target `f` with `tau` | `!(abs(f(x) - f(x')) <= tau)`, which includes a non-finite `f` on either side |

Changed finite pairs that still need expression checks are batched per chunk
and evaluated with `Expr::eval` on arrays, once for the originals and once for
the reconstructions per expression.

## Correction

`correct_pointwise` collects the flagged indices in increasing order, encodes
the LOSSLESS layer from the original bits (`encode_lossless`), and copies
those bits directly into the reconstruction. The encoder and decoder restore
the same original bits at the same indices. `restored` is the number of values
written.

`--verify-patch` (`Options::verify_patch` in C++) makes the encoder apply
the full-field LOSSLESS layer using `apply_lossless`, the same function a
reader uses. Both paths emit the same encoded bytes. Sample measurements
price the encoded patches and score the directly corrected values.

Only lossless corrections exist in this version. Quantised residuals (plan
section 9.2) and the region passes are later milestones.

## LOSSLESS layer

Payload of section type `0x11`, per field, omitted when nothing is restored:

```
u64  count                 number of restored values, 1 .. values in the field
u64  index_frame_bytes     size of the next zstd frame
...  index frame           zstd frame of LEB128 gaps
...  value frame           zstd frame of the original bit patterns, the rest of the payload
```

- Gaps: unsigned LEB128, the first gap is the absolute first index, then
  differences of strictly increasing flat C-order indices.
- Values: the original `float32` (`u32`) or `float64` (`u64`) bits,
  little-endian, in index order. Signed zeros and NaN payloads are kept.
- Both frames are compressed with zstd level 3 and record their content size.

`apply_lossless` validates the whole layer before writing anything: frame
content sizes (indices at most 10 bytes per value, values exactly
`count × width`), that each frame fills its byte range exactly, that gaps are
positive after the first and stay inside the field, and that no bytes trail.
Output buffers grow only as zstd produces data, so a frame that advertises a
large size cannot force a large allocation. Any failure raises
`CorruptStream`.

## Verification counters

`verify_pointwise` runs the same kernel in diagnostic mode and does not
modify anything. It fills [`Verification`](api.md#verification): per-check
violation counts, non-finite counts, the exact-predicate count, maximum
absolute errors over pairs with finite operands, and the number of distinct
violating values. The pipeline calls it on the corrected reconstruction when
a report is requested, and `qoiw::verify` calls it on any two arrays after
resolving tolerances with F0.
