#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

#include "backend/lossless.hpp"
#include "core/adapters/mock.hpp"
#include "frontend/measure.hpp"

namespace {
using namespace qoiw;
using namespace qoiw::frontend;
using Mode = Tolerance::Mode;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <class T>
void whole_field() {
    const auto dtype = sizeof(T) == 4 ? DType::Float32 : DType::Float64;
    const auto& core = test::mock_core("mock:quantizer");
    const T field[]{.25, 1.25, -.25, -1.25};
    const PointRequest spec{{{Expr("x^2"), {Mode::Absolute, .1}}}, {}, {}};
    const auto prep = prepare_pointwise(dtype, field, sizeof(field), {4}, spec, core.traits(dtype, 1), 1);
    const auto& search = std::get<PointSearch>(prep.decision);
    PointMeasurer measure(core, prep.resolved, search);
    const auto& trial = measure.measure(.5);
    // Four 16-bit quantizer codes and one binary64 decode parameter.
    const auto lossless = backend::encode_lossless(dtype, field, sizeof(field), {1, 3});
    check(trial.core_bytes == 16 && trial.overhead_bytes == 0 && trial.lossless_bytes == lossless.size(),
          "whole-field payload bytes");
    check(trial.sample_values == 4 && trial.restored == 2 && trial.correction_weight == 1 &&
          !trial.rare_outlier_fallback && trial.bytes_per_value == (16 + lossless.size()) / 4., "whole-field cost");
    check(std::get<std::vector<std::vector<T>>>(trial.decoded) == std::vector<std::vector<T>>{{0, 1, 0, -1}},
          "cache must retain the uncorrected core values");
    const double expected_db = 20 * std::log10(2.5 / std::sqrt(2 * .25 * .25 / 4));
    check(std::abs(trial.sample_psnr_db.value() - expected_db) < 1e-12, "quality must use corrected values");
    const auto& zero = measure.measure(0);
    check(zero.sample_psnr_db == std::numeric_limits<double>::infinity(), "exact decode has infinite PSNR");
    check(zero.restored == 0 && zero.lossless_bytes == 0 && &trial == &measure.measure(.5), "repeated bound");
    for (double bad : {-1., std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        try {
            measure.measure(bad);
        } catch (const std::invalid_argument&) {
            continue;
        }
        throw std::runtime_error("invalid bound accepted");
    }
}

void masked_sample_quality() {
    const auto& core = test::mock_core("mock:quantizer");
    std::vector<double> field(1000, .25);
    field[0] = -8;
    field[1] = 8;
    field[480] = 1e36;
    field[481] = std::numeric_limits<double>::quiet_NaN();
    const PointRequest spec{{}, {}, Expr("x > 9")};
    auto quality = [&] {
        const auto prep = prepare_pointwise(DType::Float64, field.data(), field.size() * sizeof(double),
                                            {1000}, spec, core.traits(DType::Float64, 1), 2, {}, {},
                                            {.01, 40, 40, 1, 1});
        const auto& search = std::get<PointSearch>(prep.decision);
        check(search.sample.layout.origins == std::vector<std::size_t>{480}, "quality sample origin");
        PointMeasurer meter(core, prep.resolved, search);
        return meter.measure(.5).sample_psnr_db;
    };
    check(std::abs(quality().value() - 20 * std::log10(16 / .25)) < 1e-12, "full range and eligible denominator");
    std::fill(field.begin() + 482, field.begin() + 520, 1e36);
    check(!quality(), "sample with no eligible values has no PSNR");
}

void extreme_quality() {
    const auto& core = test::mock_core("mock:signflip");
    auto quality = [&](std::vector<double> field) {
        const auto prep = prepare_pointwise(DType::Float64, field.data(), 2 * sizeof(double), {2}, {}, {}, 1);
        PointMeasurer meter(core, prep.resolved, std::get<PointSearch>(prep.decision));
        return meter.measure(std::numeric_limits<double>::max()).sample_psnr_db.value();
    };
    const auto huge = std::numeric_limits<double>::max();
    check(std::abs(quality({-huge, huge})) < 1e-12, "overflow in span or squared error");
    check(std::abs(quality({0., std::numeric_limits<double>::denorm_min()}) + 10 * std::log10(2.)) < 1e-12,
          "underflow in squared error");
}

void sensitive_shares(std::size_t in_field, bool one_in_sample, bool fallback) {
    // A single 40-value tile starts at 480. At bound 1, exact negative
    // values are sensitive and ordinary points are exactly at the prior cap.
    std::vector<float> field(1000);
    for (std::size_t i = 0; i < field.size(); ++i) field[i] = i % 2 ? 4 : 2;
    if (one_in_sample) field[480] = -1;
    std::size_t remaining = in_field - static_cast<std::size_t>(one_in_sample);
    for (std::size_t i = 0; remaining; ++i) {
        if (i >= 480 && i < 520) continue;
        field[i] = -1;
        --remaining;
    }
    const PointRequest spec{{}, Tolerance{Mode::Absolute, 1}, Expr("x < 0")};
    const auto& core = test::mock_core("mock:noise");
    const auto prep = prepare_pointwise(DType::Float32, field.data(), field.size() * sizeof(float), {field.size()},
                                        spec, {}, 2, {}, {}, {.01, 40, 40, 1, 1});
    const auto& search = std::get<PointSearch>(prep.decision);
    check(search.sample.layout.origins == std::vector<std::size_t>{480}, "unexpected sensitivity fixture");
    CoreParams params;
    params.options["factor"] = "3";
    PointMeasurer measure(core, prep.resolved, search, params, .25);
    const auto& trial = measure.measure(1);
    const double input_share = in_field / 1000., block_share = one_in_sample ? 1. / 40 : 0.;
    check(trial.input_sensitive_share == input_share && trial.sample_sensitive_share == block_share &&
          trial.rare_outlier_fallback == fallback, "sensitive shares or fallback threshold");
    check(trial.sample_values == 40 && trial.restored == 40 && trial.lossless_bytes > 0, "raw measured corrections");
    const double weight = one_in_sample && !fallback ? input_share / block_share : 1.;
    const double correction = fallback ? .25 * input_share * 5 : weight * trial.lossless_bytes / 40;
    check(trial.correction_weight == weight && trial.correction_bytes_per_value == correction &&
          trial.bytes_per_value == sizeof(float) + correction, "weighted correction cost");
    check(std::get<std::vector<std::vector<float>>>(trial.decoded)[0][0] == field[480] + 3, "core options lost");
}

// Instrument the real quantizer with a fixed call cost and serial-only calls.
class CountingCore final : public Core {
   public:
    std::string_view name() const override { return "counting"; }
    std::string_view version() const override { return "1"; }
    std::string_view format_version() const override { return "1"; }
    bool supports(DType type, const std::vector<std::uint64_t>& dims) const override {
        return quantizer.supports(type, dims);
    }
    CoreTraits traits(DType, std::size_t) const override { return {BoundKind::StrictAbs, true, false}; }
    CoreOutput compress(const void* data, DType type, const std::vector<std::uint64_t>& dims,
                        const CoreParams& params) const override {
        check(std::this_thread::get_id() == owner && params.threads == 1, "serial-only sample compression");
        ++encodes;
        return quantizer.compress(data, type, dims, params);
    }
    void decompress(Bytes stream, Bytes params, DType type, const std::vector<std::uint64_t>& dims,
                    int threads, void* out, std::size_t bytes) const override {
        check(std::this_thread::get_id() == owner && threads == 1, "serial-only sample decode");
        if (++decodes == 2) throw std::runtime_error("injected decode failure");
        quantizer.decompress(stream, params, type, dims, threads, out, bytes);
    }
    mutable int encodes = 0, decodes = 0;
   private:
    const Core& quantizer = test::mock_core("mock:quantizer");
    const std::thread::id owner = std::this_thread::get_id();
};

void overhead_and_retry() {
    CountingCore core;
    std::vector<float> field(24, 1);
    field[12] = field[13] = 1e6f;
    const PointRequest spec{{{Expr("x^2"), {Mode::Absolute, 1}}}, {}, {}};
    const auto prep = prepare_pointwise(DType::Float32, field.data(), field.size() * sizeof(float), {24},
                                        spec, core.traits(DType::Float32, 1), 4, {}, {}, {.01, 2, 2, 2, 2});
    CoreParams params;
    params.threads = 4;
    PointMeasurer measure(core, prep.resolved, std::get<PointSearch>(prep.decision), params);
    try {
        measure.measure(.5);
        throw std::runtime_error("missing decode failure");
    } catch (const std::runtime_error& e) {
        check(std::string_view(e.what()) == "injected decode failure", "unexpected trial failure");
    }
    const auto& trial = measure.measure(.5);
    check(core.encodes == 5 && core.decodes == 4, "failed candidate was cached or calibration was repeated");
    // The first block and the flat probe need escapes. The second block
    // costs only 12 bytes, so its deduction must be capped at its own size.
    check(trial.core_bytes == 32 && trial.overhead_bytes == 32 && trial.bytes_per_value == 0, "overhead deduction");
    const auto& other = measure.measure(1.);
    check(other.bound == 1 && &trial == &measure.measure(.5) && core.encodes == 8 && core.decodes == 6,
          "cache hit performed another core call");
}
}  // namespace

int main() {
    try {
        whole_field<float>();
        whole_field<double>();
        masked_sample_quality();
        extreme_quality();
        sensitive_shares(0, false, false);
        sensitive_shares(1, false, true);
        sensitive_shares(1, true, false);
        sensitive_shares(500, true, false);
        sensitive_shares(501, true, true);
        overhead_and_retry();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
