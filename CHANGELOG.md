# Changelog

## Unreleased

### M1 point-wise block sampling

- Copy the selected float32 or float64 blocks into owned arrays in C order, preserving signed zeros and NaN payloads across thread counts.
- Select deterministic C-order sample blocks with QPET's Halton sequence and rank-specific defaults. Use the whole field when the planned sample reaches half the input.

### M1 point-wise bound shortcuts

- Verify fixed and zero-radius shortcuts through all six mock cores, lossless correction, and a fresh container decode for both storage types. Document the bounds used in each case.
- Select fixed bounds, a capped fallback for all-zero sampled radii, or a single candidate for strict cores with affine constraints. Preserve the sampled distribution and a positive candidate for requests that need measurement.
- Expose the core's absolute bound kind for frontend tuning. The identity and quantizer mocks declare a strict bound, and the four adversarial mocks use the nominal default.

### M1 point-wise prior

- Sample the F0 field at QPET's regular stride, combine all point-wise targets, and extract positive, distinct quantile candidates from the sorted estimates.
- Estimate a local data-error radius from an expression's derivatives, singularities, and breakpoints. Check both endpoints against the resolved tolerance, including non-finite evaluations.

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
