#include <SZ3/api/sz.hpp>

#include <cmath>
#include <stdexcept>

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
void check_layout(Bytes stream, DType dtype, const std::vector<std::uint64_t>& dims) {
    common::ByteReader reader(stream.data, stream.size);
    if (reader.u32() != SZ3_MAGIC_NUMBER) throw common::CorruptStream("invalid SZ3 magic");
    const auto version = reader.u32();
    if (version < versionInt(SZ3_DATA_VER_OLDEST) || version > versionInt(SZ3_DATA_VER))
        throw std::invalid_argument("unsupported SZ3 stream version");
    reader.take(reader.u64());
    auto pos = reader.data();
    auto remaining = reader.remaining();
    if (!remaining || *pos != remaining) throw common::CorruptStream("invalid SZ3 config length");
    SZ3::Config stored, expected;
    stored.load(pos, remaining);
    expected.setDims(dims.begin(), dims.end());  // SZ3 elides singleton axes.
    if (stored.dims != expected.dims || stored.num != expected.num ||
        stored.dataType != (dtype == DType::Float32 ? SZ_FLOAT : SZ_DOUBLE))
        throw common::CorruptStream("SZ3 stream layout disagrees with the field");
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
        return dtype == DType::Float32 ? compress_array<float>(conf, data) : compress_array<double>(conf, data);
    }

    void decompress(Bytes stream, Bytes decode_params, DType dtype, const std::vector<std::uint64_t>& dims,
                    int threads, void* out, std::size_t out_bytes) const override {
        const auto bytes = common::array_bytes(dtype, dims);
        if (!bytes || !out || out_bytes < bytes || decode_params.size || threads < 0)
            throw std::invalid_argument("invalid SZ3 decompression arguments");
        check_layout(stream, dtype, dims);
        SZ3::Config conf;
        const auto* src = reinterpret_cast<const char*>(stream.data);
        if (dtype == DType::Float32) {
            auto* dst = static_cast<float*>(out);
            SZ_decompress(conf, src, stream.size, dst);
        } else {
            auto* dst = static_cast<double*>(out);
            SZ_decompress(conf, src, stream.size, dst);
        }
    }
};
}  // namespace

const Core& sz3_core() {
    static const Sz3Core instance;
    return instance;
}

}  // namespace qoiw
