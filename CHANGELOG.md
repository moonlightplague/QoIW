# Changelog

## Unreleased

### M1 point-wise tolerance resolution

- Store each point-wise expression and its resolved absolute tolerance in the checksummed request header. Read these records as metadata and preserve the wire bytes of streams with zero targets.
- Resolve absolute and relative tolerances in one parallel pass over a field. Report finite data and QoI ranges, count non-finite inputs, and identify original values that require exact preservation.

### M1 point-wise correction pass

- Check the corrected float32 and float64 fields from all six mock cores against independent scalar expectations and fresh container decodes across thread counts.
- Run the parallel backend under Clang ThreadSanitizer with LLVM Archer in CI so the race detector can observe OpenMP barriers.
- Compare the core's decoded field against resolved data and QoI tolerances and an optional exact condition. Encode violations in increasing index order and apply the resulting lossless layer to the reconstruction.

### M1 lossless corrections

- Store optional per-field LOSSLESS sections and restore their values after core decompression in the decode library. Preserve the existing core-only wire fixtures.
- Encode increasing indices as ULEB128 gaps and original IEEE bits in separate zstd frames. Validate both frames and all indices before restoring the decoded field.

### M0 reference baseline

- Check benchmark field sizes and readability, and prepare and bit-check the three Miranda float32 inputs for the M1 regret cases.
- Add a pinned QPET/SZ3 build recipe and a Hurricane anchor that checks the saved stream with an independent NumPy decoder-output comparison.

### Earlier M0 commits

| Commit | Change |
| --- | --- |
| `3df370a` | Added C++17 build targets, SZ3 integration, and bounded binary I/O primitives. |
| `c253d85` | Added CI checks and documented the build. |
| `f833714` | Added checksummed container headers and per-field core sections for empty requests. |
| `b39a59c` | Reserved the complete stream size and tested reordered field sections. |
| `b8eece3` | Added deterministic mock cores and empty-request round trips. |
| `22ab3c7` | Validated mock quantizer payloads before writing decoded output. |
| `1374629` | Added bytecode compilation, scalar and batch evaluation, and second-order differentiation for single-variable QoIs. |
| `c22e4d1` | Checked expression boundaries, derivatives against finite differences, and in-place batch evaluation. |
