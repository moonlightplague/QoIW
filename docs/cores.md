# Cores

A core is an unmodified error-bounded compressor behind the `qoiw::Core`
interface (`include/qoiw/core.hpp`). QoIW gives it one absolute bound per
field and never assumes the bound holds: the backend checks every value.

## Interface

```cpp
struct CoreParams { double bound; int threads; bool allow_stream_changing_parallelism;
                    std::map<std::string, std::string> options; };
struct CoreOutput { std::vector<std::uint8_t> stream, decode_params; };
struct CoreTraits { BoundKind bound_kind = NominalAbs; bool fixed_call_overhead = false; bool thread_safe = true; };

class Core {
    virtual std::string_view name() const = 0;            // stored in every stream; registry key
    virtual std::string_view version() const = 0;         // stored in every stream
    virtual std::string_view format_version() const = 0;  // the core's stream format version
    virtual bool supports(DType, const std::vector<std::uint64_t>& dims) const = 0;
    virtual CoreTraits traits(DType, std::size_t ndims) const;
    virtual CoreOutput compress(const void* data, DType, const std::vector<std::uint64_t>& dims,
                                const CoreParams&) const = 0;
    virtual void decompress(Bytes stream, Bytes decode_params, DType, const std::vector<std::uint64_t>& dims,
                            int threads, void* out, std::size_t out_bytes) const = 0;
};
```

A core must:

1. accept the C-order dimensions it says it supports, without changing the
   input;
2. be safe to call concurrently on disjoint buffers if `thread_safe` is true
   (F4 compresses sample blocks in parallel);
3. decode the same stream to the same bits on every call, thread count and
   build;
4. put anything beyond the stream, type and shape that decoding needs into
   `decode_params` (stored in the field header);
5. throw on failure. During compression, exceptions other than
   `std::invalid_argument` and `std::bad_alloc` become `CoreFailure`; during
   decoding they become `CorruptStream`.

### Traits

| Trait | Effect |
| --- | --- |
| `bound_kind = StrictAbs` | F5 never proposes a bound above the data bound, and F2 may use the single-candidate shortcut. The default `NominalAbs` makes no promise. |
| `fixed_call_overhead` | F4 deducts the size of a constant block from each sample block. |
| `thread_safe = false` | F4 measures blocks serially. |

The richer traits of plan section 8.3 (power-of-two bounds, independent
cells, special-value handling) are not implemented.

### Registry

`get_core(name)` and `available_cores()` (`src/core/builtin.cpp`) return the
built-in cores compiled into `qoiw_decode`. CMake generates the factory list
from `QOIW_CORE_SOURCES`. The registry uses each adapter's `name()` as its
key, so each name must be unique and stable. SZ3 is included in every build.

## SZ3

`src/core/adapters/sz3.cpp`, built from the `SZ3/` submodule (SZ3 3.4.0
format).

| Aspect | Behaviour |
| --- | --- |
| Support | `float32` and `float64`, rank 1 to 4, any non-empty shape. Traits: `StrictAbs`, thread-safe. |
| Mode | `EB_ABS` with `absErrorBound = bound`. A bound of 0 makes SZ3 store the data losslessly. |
| Options | Only `algo`: `interp_lorenzo` (default), `interp`, `lorenzo_reg`, `nopred`. Any other key or value is `InvalidArgument`. |
| Buffers | Compresses into a buffer sized by `SZ_compress_size_bound` and shrinks it. Decodes into the caller's buffer. |
| `decode_params` | Empty. SZ3's stream stores its own configuration. |
| Threads | By default serial and byte-identical for any thread count. With `allow_stream_changing_parallelism` and more than one thread, SZ3's OpenMP mode splits the field into at most `dims[0]` chunks. |
| Decode check | Before letting SZ3 write, the adapter reads SZ3's header and stored config and checks magic, format version range, dimensions (SZ3 drops singleton axes), element count, type and chunk count against the QoIW field. |
| Decode threads | If a chunked stream has more chunks than the caller's thread budget, the adapter runs SZ3's serial chunk loop. Thread settings are scoped to an undeferred OpenMP task, so the caller's OpenMP state is unchanged. |

## Mock cores (tests only)

`src/core/adapters/mock.cpp`, library `qoiw_mock_cores`, reached through
`qoiw::test::mock_core(name)`. They exist to show that the guarantee does not
depend on the core.

| Core | Reconstruction | Traits |
| --- | --- | --- |
| `mock:identity` | The original bits. | `StrictAbs` |
| `mock:quantizer` | Nearest multiple of `2 × bound`, halves away from zero; exceptional values, out-of-range indices and values that would exceed the bound after rounding are stored exactly. Step in `decode_params`. | `StrictAbs` |
| `mock:noise` | Alternating `±factor × bound` offsets; option `factor` (> 1, default 2). | nominal |
| `mock:garbage` | Alternating +1 and −1. | nominal |
| `mock:signflip` | Sign flipped where `abs(x) <= bound`. | nominal |
| `mock:nan` | Quiet NaN at even indices. | nominal |

## Adding a core

Follow the [README steps](../README.md#add-your-own-core). Keep the
compressor's public headers in the adapter's `.cpp`, since headers from
different compressors can conflict in one translation unit. Use
`NominalAbs` if the compressor's bound is approximate. The
[SZ3 round-trip test](../tests/cores/sz3_roundtrip.cpp) shows how to check
the adapter across supported shapes, storage types and thread counts.
