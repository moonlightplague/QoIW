#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>

#include "backend/pointwise.hpp"
#include "core/adapters/mock.hpp"
#include "pipeline/decode_field.hpp"

namespace {
using qoiw::Bytes;
using qoiw::DType;
using qoiw::Expr;
using Buffer = std::vector<std::uint8_t>;

Bytes view(const Buffer& bytes) { return {bytes.data(), bytes.size()}; }

// Independent scalar reference for the request in round_trip. Check the actual
// storage values and retain the core value at every position where it is legal.
template <class T>
bool may_keep(const T& original, const T& candidate) {
    if (std::memcmp(&original, &candidate, sizeof(T)) == 0) return true;
    const double x = original, y = candidate;
    if (!std::isfinite(x) || !std::isfinite(y) || x > 1e30 || y > 1e30) return false;
    if (x <= 0 || y <= 0) return false;
    return std::abs(x - y) <= 0.25 && std::abs(std::log(x) - std::log(y)) <= 0.5 &&
           std::abs(x * x - y * y) <= 1.;
}

template <class T>
void round_trip() {
    const auto dtype = std::is_same_v<T, float> ? DType::Float32 : DType::Float64;
    const std::vector<std::uint64_t> dims{65, 65};
    std::vector<T> original(65 * 65);
    for (std::size_t i = 0; i < original.size(); ++i) original[i] = (static_cast<int>(i % 97) - 20) / 32.;
    original[42] = -0.;
    original[2047] = std::numeric_limits<T>::quiet_NaN();
    original[2048] = std::numeric_limits<T>::infinity();
    original[2049] = static_cast<T>(1e36);
    original.back() = std::numeric_limits<T>::denorm_min();
    const auto bytes = original.size() * sizeof(T);
    const qoiw::ResolvedPointRequest request{{{Expr("ln(x)"), 0.5}, {Expr("x^2"), 1.}}, 0.25, Expr("x > 1e30")};

    for (auto name : {"mock:identity", "mock:quantizer", "mock:noise", "mock:garbage", "mock:signflip", "mock:nan"}) {
        const auto& core = qoiw::test::mock_core(name);
        qoiw::CoreParams params;
        params.bound = 0.125;
        // Overruns the data bound while staying finite on ordinary inputs.
        if (core.name() == "mock:noise") params.options["factor"] = "3";
        const auto payload = core.compress(original.data(), dtype, dims, params);
        std::vector<T> raw(original.size());
        core.decompress(view(payload.stream), view(payload.decode_params), dtype, dims, 1, raw.data(), bytes);
        auto expected = raw;
        std::size_t restored = 0;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (!may_keep(original[i], raw[i])) {
                std::memcpy(&expected[i], &original[i], sizeof(T));
                ++restored;
            }
        }
        Buffer serial_stream;
        for (int threads : {1, 2, 7, 16}) {
            auto verified = raw;
            auto corrections = qoiw::backend::correct_pointwise(dtype, original.data(), verified.data(), bytes,
                                                                 request, threads);
            if (restored != corrections.restored || std::memcmp(expected.data(), verified.data(), bytes))
                throw std::runtime_error(std::string(name) + " violated reference selection");

            qoiw::container::Stream stream;
            stream.dtype = dtype;
            stream.dims = dims;
            stream.core_name = core.name();
            stream.core_version = core.version();
            stream.core_format_version = core.format_version();
            stream.exact_condition = request.exact->text();
            stream.fields = {{"x", params.bound, request.data_bound_abs, view(payload.decode_params), 0,
                              view(payload.stream), view(corrections.lossless)}};
            const auto blob = qoiw::container::write(stream);
            if (threads == 1) serial_stream = blob;
            if (blob != serial_stream) throw std::runtime_error(std::string(name) + " stream differs by thread count");
            const auto parsed = qoiw::container::read(view(blob));
            std::vector<T> output(original.size());
            const auto count = qoiw::pipeline::decode_field(parsed, 0, qoiw::test::mock_core(parsed.core_name),
                                                            threads, output.data(), bytes);
            if (count != restored || std::memcmp(output.data(), verified.data(), bytes))
                throw std::runtime_error(std::string(name) + " decode differs from verified field");
        }
    }
}
}  // namespace

int main() {
    try {
        round_trip<float>();
        round_trip<double>();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
