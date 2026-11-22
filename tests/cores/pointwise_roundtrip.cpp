#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>

#include "backend/lossless.hpp"
#include "backend/pointwise.hpp"
#include "core/adapters/mock.hpp"
#include "frontend/plan.hpp"
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

enum class BoundCase { Candidates, Fixed, ZeroSample };

void affine_shortcut() {
    using namespace qoiw::frontend;
    const auto& core = qoiw::test::mock_core("mock:quantizer");
    const double field[]{-1, 1};
    const PointRequest spec{{{Expr("2*x"), {Tolerance::Mode::Absolute, .25}}}, {}, {}};
    const auto prep = prepare_pointwise(DType::Float64, field, sizeof(field), {2}, spec,
                                        core.traits(DType::Float64, 1), 1);
    const auto selected = plan_pointwise(core, prep);
    if (selected.bound != std::vector<double>{.125} || !selected.trace.empty() ||
        selected.shortcut_reason != ShortcutReason::SingleCandidate || selected.binding != qoiw::Binding::Qoi)
        throw std::runtime_error("affine shortcut should bypass search");
}

template <class T>
void check_trials(const qoiw::Core& core, const qoiw::frontend::PointPreparation& prep,
                  const qoiw::Plan& selection, qoiw::CoreParams params) {
    using namespace qoiw::frontend;
    const auto& search = std::get<PointSearch>(prep.decision);
    const auto& blocks = std::get<std::vector<std::vector<T>>>(search.sample.blocks);
    const auto dtype = search.sample.dtype();
    const auto& dims = search.sample.layout.dims;
    params.threads = 4;
    PointMeasurer meter(core, prep.resolved, search, params);
    params.threads = 1;
    auto traced = [&](double bound) {
        return std::any_of(selection.trace.begin(), selection.trace.end(),
                           [=](const CandidateStats& s) { return s.bound == bound; });
    };
    for (double seed : search.prior.candidates) {
        if (!traced(seed)) throw std::runtime_error("unmeasured prior candidate");
    }
    if (!traced(selection.bound.at(0)) || !traced(*prep.resolved.request.data_bound_abs))
        throw std::runtime_error("chosen bound or data-bound probe missing from trace");
    for (const auto& stats : selection.trace) {
        const auto& trial = meter.measure(stats.bound);
        const auto& cached = std::get<std::vector<std::vector<T>>>(trial.decoded);
        params.bound = stats.bound;
        std::size_t core_bytes = 0, lossless_bytes = 0, restored = 0;
        double squared_error = 0;
        std::size_t quality_count = 0;
        for (std::size_t k = 0; k < blocks.size(); ++k) {
            const auto& block = blocks[k];
            const auto bytes = block.size() * sizeof(T);
            const auto payload = core.compress(block.data(), dtype, dims, params);
            core_bytes += payload.stream.size() + payload.decode_params.size();
            std::vector<T> raw(block.size());
            core.decompress(view(payload.stream), view(payload.decode_params), dtype, dims, 1, raw.data(), bytes);
            if (std::memcmp(raw.data(), cached.at(k).data(), bytes))
                throw std::runtime_error("cached sample differs from fresh decode");
            std::vector<std::size_t> indices;
            for (std::size_t i = 0; i < block.size(); ++i) {
                const bool keep = may_keep(block[i], raw[i]);
                if (!keep) indices.push_back(i);
                if (std::isfinite(block[i]) && block[i] <= 1e30) {
                    ++quality_count;
                    const double error = keep ? static_cast<double>(raw[i]) - block[i] : 0.;
                    squared_error += error * error;
                }
            }
            restored += indices.size();
            lossless_bytes += qoiw::backend::encode_lossless(dtype, block.data(), bytes, indices).size();
        }
        if (stats.core_bytes != core_bytes || stats.restored != restored || stats.lossless_bytes != lossless_bytes ||
            stats.bytes_per_value != trial.bytes_per_value)
            throw std::runtime_error("candidate bytes or correction count disagree with scalar reference");
        const double expected_psnr = squared_error == 0 ? std::numeric_limits<double>::infinity()
            : 20 * std::log10(3 / std::sqrt(squared_error / quality_count));
        const double psnr = stats.sample_psnr_db.value();
        if (!(psnr == expected_psnr || std::abs(psnr - expected_psnr) < 1e-10))
            throw std::runtime_error("reported sample PSNR disagrees with corrected scalar reference");
        if (stats.bound == selection.bound[0]) {
            const auto& p = selection.predicted.value();
            if (p.core_bytes_per_value != stats.core_bytes_per_value() ||
                p.correction_bytes_per_value != stats.correction_bytes_per_value || p.bytes_per_value != stats.bytes_per_value ||
                p.sample_psnr_db != psnr || p.indicative_compression_ratio != sizeof(T) / stats.bytes_per_value ||
                selection.flags.rare_outlier_fallback != stats.rare_outlier_fallback)
                throw std::runtime_error("predictions do not describe the chosen trial");
        }
    }
}

template <class T>
void round_trip(BoundCase selection) {
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
    using namespace qoiw::frontend;
    using Mode = Tolerance::Mode;
    // The finite, unmasked field spans 3, and x^2 spans 361/64.
    const PointRequest spec{{{Expr("ln(x)"), {Mode::Absolute, .5}}, {Expr("x^2"), {Mode::Relative, 64. / 361}}},
                            Tolerance{Mode::Relative, 1. / 12}, Expr("x > 1e30")};
    const auto fixed_bound = selection == BoundCase::Fixed ? std::optional<double>{1.} : std::nullopt;
    // With one sample, F1 sees only the negative value at index zero.
    const PriorOptions options{selection == BoundCase::ZeroSample ? 1u : 0u, {}};

    auto names = qoiw::available_cores();
    names.insert(names.end(), {"mock:identity", "mock:quantizer", "mock:noise", "mock:garbage", "mock:signflip", "mock:nan"});
    auto lookup = [](std::string_view name) -> const qoiw::Core& {
        return name.substr(0, 5) == "mock:" ? qoiw::test::mock_core(name) : qoiw::get_core(name);
    };
    for (const auto& name : names) {
        const auto& core = lookup(name);
        const auto prep = prepare_pointwise(dtype, original.data(), bytes, dims, spec, core.traits(dtype, dims.size()),
                                            2, fixed_bound, options, {.01, 16, 16, 2, 4});
        const auto& request = prep.resolved.request;
        qoiw::CoreParams params;
        // Deliberately overshoot the core's bound on ordinary inputs.
        if (core.name() == "mock:noise") params.options["factor"] = "3";
        const auto selected = plan_pointwise(core, prep, params);
        params.bound = selected.bound.at(0);
        if (selected.bound.size() != 1 || selected.reference_bound != params.bound ||
            selected.correction_mode != qoiw::CorrectionMode::Lossless || selected.data_bound_abs != .25 ||
            selected.targets.size() != 2 || selected.targets[0].expression != "ln(x)" ||
            selected.targets[0].tolerance_abs != .5 || selected.targets[1].expression != "x^2" ||
            selected.targets[1].tolerance_abs != 1 || selected.flags.prior_free || selected.flags.whole_input_sampled)
            throw std::runtime_error("plan differs from resolved request or sample");
        if (selection == BoundCase::Candidates) {
            const auto& search = std::get<PointSearch>(prep.decision);
            if (search.sample.layout.dims != std::vector<std::uint64_t>{13, 13} ||
                std::get<std::vector<std::vector<T>>>(search.sample.blocks).size() != 2)
                throw std::runtime_error("expected two 13 x 13 blocks from the original field");
            const auto& candidates = search.prior.candidates;
            if (candidates.empty()) throw std::runtime_error("expected positive candidate bounds");
            if (core.name() == "mock:identity" && (params.bound != .25 || selected.binding != qoiw::Binding::DataBound))
                throw std::runtime_error("equal-cost identity trials should select the clean data bound");
            if (selected.flags.shortcut_taken || selected.shortcut_reason)
                throw std::runtime_error("measured plan marked as a shortcut");
            check_trials<T>(core, prep, selected, params);
        } else {
            const auto reason = fixed_bound ? ShortcutReason::FixedBound : ShortcutReason::ZeroRadii;
            if (!selected.trace.empty() || selected.predicted || selected.binding || !selected.flags.shortcut_taken ||
                selected.shortcut_reason != reason || params.bound != fixed_bound.value_or(3 * 1e-9))
                throw std::runtime_error("unexpected compressor bound for shortcut");
        }
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
            if (parsed.fields[0].bound != params.bound || parsed.fields[0].data_bound_abs != .25 ||
                parsed.targets.size() != 2 ||
                parsed.targets[0].expression != "ln(x)" || parsed.targets[0].tolerance_abs != .5 ||
                parsed.targets[1].expression != "x^2" || parsed.targets[1].tolerance_abs != 1.)
                throw std::runtime_error("stored tolerances differ from scalar reference");
            std::vector<T> output(original.size());
            const auto count = qoiw::pipeline::decode_field(parsed, 0, lookup(parsed.core_name),
                                                            threads, output.data(), bytes);
            if (count != restored || std::memcmp(output.data(), verified.data(), bytes))
                throw std::runtime_error(std::string(name) + " decode differs from verified field");
        }
    }
}
}  // namespace

int main() {
    try {
        affine_shortcut();
        for (const auto selection : {BoundCase::Candidates, BoundCase::Fixed, BoundCase::ZeroSample}) {
            round_trip<float>(selection);
            round_trip<double>(selection);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
