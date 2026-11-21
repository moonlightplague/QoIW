#include "core/adapters/mock.hpp"

#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

#include "common/byte_io.hpp"
#include "common/shape.hpp"

namespace qoiw::test {
namespace {
using common::ByteReader;
using common::ByteWriter;
using common::CorruptStream;

enum class Mode { Identity, Quantizer, Noise, Garbage, SignFlip, Nan };

double noise_factor(const std::map<std::string, std::string>& options) {
    if (options.empty()) return 2;
    const auto it = options.find("factor");
    if (options.size() != 1 || it == options.end()) throw std::invalid_argument("mock:noise expects option factor");
    const auto& text = it->second;
    double factor = 0;
    const auto end = text.data() + text.size();
    auto result = std::from_chars(text.data(), end, factor);
    if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(factor) || factor <= 1)
        throw std::invalid_argument("noise factor must be finite and greater than one");
    return factor;
}

template <class T>
void write_bits(ByteWriter& writer, const T& value) {
    using Word = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
    Word bits;
    std::memcpy(&bits, &value, sizeof(bits));
    if constexpr (sizeof(T) == 4) writer.u32(bits);
    else writer.u64(bits);
}

template <class T>
void read_bits(ByteReader& reader, T& value) {
    if constexpr (sizeof(T) == 4) {
        const auto bits = reader.u32();
        std::memcpy(&value, &bits, sizeof(bits));
    } else {
        const auto bits = reader.u64();
        std::memcpy(&value, &bits, sizeof(bits));
    }
}

// Check range before narrowing a double outside the finite range of float.
template <class T>
T storage_value(double value) {
    constexpr auto max = std::numeric_limits<T>::max();
    if (value > max) return std::numeric_limits<T>::infinity();
    if (value < -max) return -std::numeric_limits<T>::infinity();
    return static_cast<T>(value);
}

template <class T>
void write_quantized(ByteWriter& writer, const T& value, double step, double bound) {
    // Code 0 escapes to the original IEEE bits. Codes 1..65535 store indices -32767..32767.
    if (step > 0 && std::isfinite(value) && !(value == 0 && std::signbit(value))) {
        const double index = std::round(value / step);
        if (std::abs(index) <= 32767) {
            const T decoded = storage_value<T>(index * step);
            if (std::abs(static_cast<double>(value) - decoded) <= bound) {
                writer.u16(static_cast<std::uint16_t>(static_cast<int>(index) + 32768));
                return;
            }
        }
    }
    writer.u16(0);
    write_bits(writer, value);
}

template <class T>
CoreOutput encode(Mode mode, const T* values, std::size_t count, const CoreParams& params) {
    static_assert(std::numeric_limits<T>::is_iec559);
    CoreOutput output;
    ByteWriter writer(output.stream);
    const double bound = params.bound;
    double step = 0;
    double noise = 0;
    if (mode == Mode::Noise) {
        noise = bound * noise_factor(params.options);
    } else if (!params.options.empty()) {
        throw std::invalid_argument("unknown mock core option");
    }
    if (mode == Mode::Quantizer) {
        if (bound <= std::numeric_limits<double>::max() / 2) step = 2 * bound;
        ByteWriter(output.decode_params).f64(step);
    }

    for (std::size_t i = 0; i < count; ++i) {
        const T& value = values[i];
        switch (mode) {
            case Mode::Identity: write_bits(writer, value); break;
            case Mode::Quantizer: write_quantized(writer, value, step, bound); break;
            case Mode::Noise:
                write_bits(writer, storage_value<T>(value + ((i % 2) ? -noise : noise)));
                break;
            case Mode::Garbage: write_bits(writer, static_cast<T>((i % 2) ? -1 : 1)); break;
            case Mode::SignFlip:
                write_bits(writer, std::abs(value) <= bound ? -value : value);
                break;
            case Mode::Nan:
                write_bits(writer, (i % 2 == 0) ? std::numeric_limits<T>::quiet_NaN() : value);
                break;
        }
    }
    return output;
}

template <class T>
void decode_quantized(ByteReader reader, double step, std::size_t count, T* output) {
    if (count > reader.remaining() / 2) throw CorruptStream("truncated mock quantizer stream");
    // Validate the entire variable-length payload before committing the caller's output.
    std::vector<T> values(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto code = reader.u16();
        if (code == 0) {
            read_bits(reader, values[i]);
        } else {
            const auto value = storage_value<T>((static_cast<int>(code) - 32768) * step);
            if (step == 0 || !std::isfinite(value)) throw CorruptStream("invalid mock quantizer code");
            values[i] = value;
        }
    }
    if (reader.remaining()) throw CorruptStream("trailing mock quantizer bytes");
    std::memcpy(output, values.data(), count * sizeof(T));
}

class MockCore final : public Core {
   public:
    constexpr MockCore(std::string_view name, Mode mode) : name_(name), mode_(mode) {}

    std::string_view name() const override { return name_; }
    std::string_view version() const override { return "m0"; }
    std::string_view format_version() const override { return "1"; }
    bool supports(DType dtype, const std::vector<std::uint64_t>& dims) const override {
        return common::array_bytes(dtype, dims) != 0;
    }
    CoreTraits traits(DType, std::size_t) const override {
        if (mode_ == Mode::Identity || mode_ == Mode::Quantizer) return {BoundKind::StrictAbs};
        return {};
    }

    CoreOutput compress(const void* data, DType dtype, const std::vector<std::uint64_t>& dims,
                        const CoreParams& params) const override {
        const auto bytes = common::array_bytes(dtype, dims);
        if (!bytes || !data || params.threads < 0 || !std::isfinite(params.bound) || params.bound < 0)
            throw std::invalid_argument("invalid mock core input, threads or bound");
        if (dtype == DType::Float32) return encode(mode_, static_cast<const float*>(data), bytes / 4, params);
        return encode(mode_, static_cast<const double*>(data), bytes / 8, params);
    }

    void decompress(Bytes stream, Bytes params, DType dtype, const std::vector<std::uint64_t>& dims,
                    int threads, void* out, std::size_t out_bytes) const override {
        const auto bytes = common::array_bytes(dtype, dims);
        if (!bytes || !out || bytes > out_bytes || threads < 0) throw std::invalid_argument("invalid mock core output");
        ByteReader reader(stream.data, stream.size);
        if (mode_ == Mode::Quantizer) {
            if (params.size != 8) throw CorruptStream("mock quantizer requires a binary64 step");
            const auto step = ByteReader(params.data, params.size).f64();
            if (!std::isfinite(step) || step < 0) throw CorruptStream("invalid mock quantizer step");
            if (dtype == DType::Float32) decode_quantized(reader, step, bytes / 4, static_cast<float*>(out));
            else decode_quantized(reader, step, bytes / 8, static_cast<double*>(out));
        } else {
            if (params.size || stream.size != bytes) throw CorruptStream("invalid raw mock payload length or parameters");
            if (dtype == DType::Float32) {
                for (std::size_t i = 0; i < bytes / 4; ++i) read_bits(reader, static_cast<float*>(out)[i]);
            } else {
                for (std::size_t i = 0; i < bytes / 8; ++i) read_bits(reader, static_cast<double*>(out)[i]);
            }
        }
    }

   private:
    std::string_view name_;
    Mode mode_;
};
}  // namespace

const Core& mock_core(std::string_view name) {
    static const MockCore cores[]{
        {"mock:identity", Mode::Identity}, {"mock:quantizer", Mode::Quantizer}, {"mock:noise", Mode::Noise},
        {"mock:garbage", Mode::Garbage}, {"mock:signflip", Mode::SignFlip}, {"mock:nan", Mode::Nan},
    };
    for (const auto& core : cores) {
        if (core.name() == name) return core;
    }
    throw std::invalid_argument("unknown mock core");
}

}  // namespace qoiw::test
