#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>

#include "backend/pointwise.hpp"
#include "core/adapters/mock.hpp"
#include "frontend/prior.hpp"
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
    using Mode = qoiw::frontend::Tolerance::Mode;
    // The finite, unmasked field spans 3, and x^2 spans 361/64.
    const qoiw::frontend::PointRequest spec{{{Expr("ln(x)"), {Mode::Absolute, .5}},
                                              {Expr("x^2"), {Mode::Relative, 64. / 361}}},
                                             qoiw::frontend::Tolerance{Mode::Relative, 1. / 12}, Expr("x > 1e30")};
    const auto resolved = qoiw::frontend::resolve_pointwise(dtype, original.data(), bytes, spec, 2);
    const auto& request = resolved.request;
    const auto prior = qoiw::frontend::estimate_point_prior(dtype, original.data(), bytes, resolved, 2);
    if (prior.candidates.empty()) throw std::runtime_error("expected positive candidate bounds");

    for (auto name : {"mock:identity", "mock:quantizer", "mock:noise", "mock:garbage", "mock:signflip", "mock:nan"}) {
        const auto& core = qoiw::test::mock_core(name);
        qoiw::CoreParams params;
        params.bound = prior.candidates.back();
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
            for (const auto& target : request.targets)
                stream.targets.push_back({target.expression.text(), target.tolerance_abs});
            stream.fields = {{"x", params.bound, request.data_bound_abs, view(payload.decode_params), 0,
                              view(payload.stream), view(corrections.lossless)}};
            const auto blob = qoiw::container::write(stream);
            if (threads == 1) serial_stream = blob;
            if (blob != serial_stream) throw std::runtime_error(std::string(name) + " stream differs by thread count");
            const auto parsed = qoiw::container::read(view(blob));
            if (parsed.fields[0].data_bound_abs != .25 || parsed.targets.size() != 2 ||
                parsed.targets[0].expression != "ln(x)" || parsed.targets[0].tolerance_abs != .5 ||
                parsed.targets[1].expression != "x^2" || parsed.targets[1].tolerance_abs != 1.)
                throw std::runtime_error("stored tolerances differ from scalar reference");
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
