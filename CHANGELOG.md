# Changelog

## Unreleased

### M0 reference baseline

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
