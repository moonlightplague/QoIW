#include <SZ3/api/sz.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "common/byte_io.hpp"
#include "common/shape.hpp"
#include "core/adapters/sz3.hpp"

namespace qoiw {
namespace {
SZ3::ALGO algorithm(const CoreParams& params) {
    if (params.options.empty()) return SZ3::ALGO_INTERP_LORENZO;
    const auto& [key, value] = *params.options.begin();
    if (params.options.size() != 1 || key != "algo") throw std::invalid_argument("unknown SZ3 option");
    if (value == "interp_lorenzo") return SZ3::ALGO_INTERP_LORENZO;
    if (value == "interp") return SZ3::ALGO_INTERP;
    if (value == "lorenzo_reg") return SZ3::ALGO_LORENZO_REG;
    if (value == "nopred") return SZ3::ALGO_NOPRED;
    throw std::invalid_argument("unknown SZ3 algorithm: " + value);
}

template <class T>
CoreOutput compress_array(const SZ3::Config& conf, const void* data) {
    CoreOutput result;
    result.stream.resize(SZ3::SZ_compress_size_bound<T>(conf));
    const auto size = SZ_compress(conf, static_cast<const T*>(data), reinterpret_cast<char*>(result.stream.data()),
                                  result.stream.size());
    result.stream.resize(size);
    return result;
}

// SZ_decompress uses the stream's dimensions even when passed a caller-owned
// buffer. Read the public header and Config before allowing that write.
int checked_chunk_count(Bytes stream, DType dtype, const std::vector<std::uint64_t>& dims) {
    common::ByteReader reader(stream.data, stream.size);
    if (reader.u32() != SZ3_MAGIC_NUMBER) throw common::CorruptStream("invalid SZ3 magic");
    const auto version = reader.u32();
    if (version < versionInt(SZ3_DATA_VER_OLDEST) || version > versionInt(SZ3_DATA_VER))
        throw std::invalid_argument("unsupported SZ3 stream version");
    auto payload = reader.take(reader.u64());
    auto pos = reader.data();
    auto remaining = reader.remaining();
    if (!remaining || *pos != remaining) throw common::CorruptStream("invalid SZ3 config length");
    SZ3::Config stored, expected;
    stored.load(pos, remaining);
    expected.setDims(dims.begin(), dims.end());  // SZ3 elides singleton axes.
    if (stored.dims != expected.dims || stored.num != expected.num ||
        stored.dataType != (dtype == DType::Float32 ? SZ_FLOAT : SZ_DOUBLE))
        throw common::CorruptStream("SZ3 stream layout disagrees with the field");
    if (!stored.openmp) return 1;
    const auto chunks = payload.u32();
    if (!chunks || chunks > expected.dims.front() || chunks > static_cast<unsigned>(std::numeric_limits<int>::max()))
        throw common::CorruptStream("invalid SZ3 chunk count");
    return static_cast<int>(chunks);
}

int worker_limit(int requested) {
#ifdef _OPENMP
    return requested ? requested : omp_get_max_threads();
#else
    (void)requested;
    return 1;
#endif
}

// SZ3 reads the OpenMP task's thread settings. An undeferred task runs
// synchronously and discards its settings on return, including on failure.
template <class Work>
void with_sz3_threads(int workers, Work work) {
#ifdef _OPENMP
    std::exception_ptr failure;
#pragma omp task if(0) shared(work, failure)
    {
        if (workers == 1) omp_set_max_active_levels(omp_get_active_level());
        else omp_set_num_threads(workers);
        try {
            work();
        } catch (...) {
            failure = std::current_exception();
        }
    }
    if (failure) std::rethrow_exception(failure);
#else
    (void)workers;
    work();
#endif
}

class Sz3Core final : public Core {
   public:
    std::string_view name() const override { return "sz3"; }
    std::string_view version() const override { return SZ3_VER; }
    std::string_view format_version() const override { return SZ3_DATA_VER; }
    bool supports(DType dtype, const std::vector<std::uint64_t>& dims) const override {
        return common::array_bytes(dtype, dims) != 0;
    }
    CoreTraits traits(DType, std::size_t) const override { return {BoundKind::StrictAbs}; }

    CoreOutput compress(const void* data, DType dtype, const std::vector<std::uint64_t>& dims,
                        const CoreParams& params) const override {
        if (!data || !supports(dtype, dims) || params.threads < 0 || !std::isfinite(params.bound) || params.bound < 0)
            throw std::invalid_argument("invalid SZ3 compression arguments");
        SZ3::Config conf;
        conf.setDims(dims.begin(), dims.end());
        conf.dataType = dtype == DType::Float32 ? SZ_FLOAT : SZ_DOUBLE;
        conf.errorBoundMode = SZ3::EB_ABS;
        conf.absErrorBound = params.bound;
        conf.cmprAlgo = algorithm(params);
        const auto encode = [&] {
            return dtype == DType::Float32 ? compress_array<float>(conf, data) : compress_array<double>(conf, data);
        };
        if (params.allow_stream_changing_parallelism) {
            const int workers = static_cast<int>(std::min<std::size_t>(worker_limit(params.threads), conf.dims.front()));
            if (workers > 1) {
                conf.openmp = true;
                CoreOutput result;
                with_sz3_threads(workers, [&] { result = encode(); });
                return result;
            }
        }
        return encode();
    }

    void decompress(Bytes stream, Bytes decode_params, DType dtype, const std::vector<std::uint64_t>& dims,
                    int threads, void* out, std::size_t out_bytes) const override {
        const auto bytes = common::array_bytes(dtype, dims);
        if (!bytes || !out || out_bytes < bytes || decode_params.size || threads < 0)
            throw std::invalid_argument("invalid SZ3 decompression arguments");
        const auto chunks = checked_chunk_count(stream, dtype, dims);
        const auto decode = [&] {
            SZ3::Config conf;
            const auto* src = reinterpret_cast<const char*>(stream.data);
            if (dtype == DType::Float32) {
                auto* dst = static_cast<float*>(out);
                SZ_decompress(conf, src, stream.size, dst);
            } else {
                auto* dst = static_cast<double*>(out);
                SZ_decompress(conf, src, stream.size, dst);
            }
        };
        // SZ3 requests one worker per stored chunk explicitly. If the caller
        // allows fewer, use its serial chunk walk to stay inside that budget.
        if (chunks > worker_limit(threads)) with_sz3_threads(1, decode);
        else decode();
    }
};
}  // namespace

const Core& sz3_core() {
    static const Sz3Core instance;
    return instance;
}

}  // namespace qoiw
