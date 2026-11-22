#include "qoiw/report.hpp"

#include <charconv>
#include <cmath>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace qoiw {
namespace {
std::string number(double value) {
    if (std::isinf(value)) return value > 0 ? "\"+inf\"" : "\"-inf\"";
    if (std::isnan(value)) throw std::invalid_argument("unexpected NaN in report");
    char buffer[64];  // Shortest round-trippable decimal for a binary64.
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{}) throw std::logic_error("cannot format report number");
    return {buffer, result.ptr};
}

std::string number(const std::optional<double>& value) {
    return value ? number(*value) : "null";
}

std::string quote(std::string_view value) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        if (c < 0x20) {
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 15];
        } else {
            out += static_cast<char>(c);
        }
    }
    return out + '"';
}

const char* label(Binding binding) {
    switch (binding) {
        case Binding::Qoi: return "QOI";
        case Binding::DataBound: return "DATA_BOUND";
        case Binding::Tie: return "TIE";
    }
    throw std::invalid_argument("unknown plan binding");
}

const char* label(ShortcutReason reason) {
    switch (reason) {
        case ShortcutReason::FixedBound: return "fixed_bound";
        case ShortcutReason::ZeroRadii: return "zero_radii";
        case ShortcutReason::SingleCandidate: return "single_candidate";
    }
    throw std::invalid_argument("unknown plan shortcut");
}

const char* label(CorrectionMode mode) {
    switch (mode) {
        case CorrectionMode::Lossless: return "lossless";
        case CorrectionMode::Residual: return "residual";
    }
    throw std::invalid_argument("unknown correction mode");
}

void write_prediction(std::ostream& out, const PlanPrediction& p) {
    out << "{\"core_bytes_per_value\":" << number(p.core_bytes_per_value)
        << ",\"correction_bytes_per_value\":" << number(p.correction_bytes_per_value)
        << ",\"bytes_per_value\":" << number(p.bytes_per_value)
        << ",\"outlier_share\":" << number(p.outlier_share)
        << ",\"sample_psnr_db\":" << number(p.sample_psnr_db)
        << ",\"indicative_compression_ratio\":" << number(p.indicative_compression_ratio) << '}';
}

void write_trial(std::ostream& out, const CandidateStats& t) {
    out << "{\"bound\":" << number(t.bound) << ",\"sample_values\":" << t.sample_values
        << ",\"restored\":" << t.restored << ",\"core_bytes\":" << t.core_bytes
        << ",\"overhead_bytes\":" << t.overhead_bytes << ",\"lossless_bytes\":" << t.lossless_bytes
        << ",\"input_sensitive_share\":" << number(t.input_sensitive_share)
        << ",\"sample_sensitive_share\":" << number(t.sample_sensitive_share)
        << ",\"correction_weight\":" << number(t.correction_weight)
        << ",\"outlier_share\":" << number(t.outlier_share)
        << ",\"rare_outlier_fallback\":" << t.rare_outlier_fallback
        << ",\"correction_bytes_per_value\":" << number(t.correction_bytes_per_value)
        << ",\"bytes_per_value\":" << number(t.bytes_per_value)
        << ",\"sample_psnr_db\":" << number(t.sample_psnr_db) << '}';
}

void write_errors(std::ostream& out, const ErrorStats& stats) {
    out << "{\"max_abs_error\":" << number(stats.max_abs_error)
        << ",\"violations\":" << stats.violations
        << ",\"nonfinite_violations\":" << stats.nonfinite_violations << '}';
}

void write_tolerance(std::ostream& out, const TargetTolerance& target) {
    out << "\"expression\":" << quote(target.expression) << ",\"tolerance_abs\":" << number(target.tolerance_abs);
}
}  // namespace

std::string Plan::to_json() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::boolalpha << "{\"bound\":[";
    for (std::size_t i = 0; i < bound.size(); ++i) {
        if (i) out << ',';
        out << number(bound[i]);
    }
    out << "],\"reference_bound\":" << number(reference_bound) << ",\"correction_mode\":" << quote(label(correction_mode))
        << ",\"binding\":" << (binding ? quote(label(*binding)) : "null")
        << ",\"data_bound_abs\":" << number(data_bound_abs)
        << ",\"exact_condition\":" << (exact_condition ? quote(*exact_condition) : "null") << ",\"targets\":[";
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (i) out << ',';
        out << '{';
        write_tolerance(out, targets[i]);
        out << '}';
    }
    out << "],\"predicted\":";
    if (predicted) write_prediction(out, *predicted);
    else out << "null";
    out << ",\"trace\":[";
    for (std::size_t i = 0; i < trace.size(); ++i) {
        if (i) out << ',';
        write_trial(out, trace[i]);
    }
    out << "],\"flags\":{\"whole_input_sampled\":" << flags.whole_input_sampled
        << ",\"prior_free\":" << flags.prior_free << ",\"rare_outlier_fallback\":" << flags.rare_outlier_fallback
        << ",\"shortcut_taken\":" << flags.shortcut_taken << '}'
        << ",\"shortcut_reason\":" << (shortcut_reason ? quote(label(*shortcut_reason)) : "null") << '}';
    return out.str();
}

std::string Verification::to_json() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::boolalpha << "{\"ok\":" << ok() << ",\"values\":" << values
        << ",\"violating_values\":" << violating_values << ",\"exact_violations\":" << exact_violations
        << ",\"data_bound_abs\":" << number(data_bound_abs) << ",\"data\":";
    write_errors(out, data);
    out << ",\"targets\":[";
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (i) out << ',';
        out << '{';
        write_tolerance(out, targets[i].target);
        out << ",\"errors\":";
        write_errors(out, targets[i].errors);
        out << '}';
    }
    out << "]}";
    return out.str();
}

std::string StreamInfo::to_json() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"dtype\":" << quote(dtype == DType::Float32 ? "float32" : "float64") << ",\"shape\":[";
    for (std::size_t i = 0; i < dims.size(); ++i) {
        if (i) out << ',';
        out << dims[i];
    }
    out << "],\"num\":" << num << ",\"stream_bytes\":" << stream_bytes << ",\"wrapper_version\":" << quote(wrapper_version)
        << ",\"core_name\":" << quote(core_name) << ",\"core_version\":" << quote(core_version)
        << ",\"core_format_version\":" << quote(core_format_version) << ",\"fields\":[";
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i) out << ',';
        const auto& field = fields[i];
        out << "{\"name\":" << quote(field.name) << ",\"bound\":" << number(field.bound)
            << ",\"data_bound_abs\":" << number(field.data_bound_abs) << ",\"decode_params_bytes\":" << field.decode_params_bytes
            << ",\"core_bytes\":" << field.core_bytes << ",\"lossless_bytes\":" << field.lossless_bytes << '}';
    }
    out << "],\"targets\":[";
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (i) out << ',';
        out << '{';
        write_tolerance(out, targets[i]);
        out << '}';
    }
    out << "],\"exact_condition\":" << (exact_condition ? quote(*exact_condition) : "null") << '}';
    return out.str();
}

std::string Report::to_json() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"plan\":" << plan.to_json() << ",\"stream\":" << stream.to_json()
        << ",\"nonfinite_inputs\":" << nonfinite_inputs << ",\"restored\":" << restored
        << ",\"verification\":" << verification.to_json() << ",\"seconds\":{\"planning\":" << number(seconds.planning)
        << ",\"core_encode\":" << number(seconds.core_encode) << ",\"core_decode\":" << number(seconds.core_decode)
        << ",\"correction\":" << number(seconds.correction) << ",\"verification\":" << number(seconds.verification)
        << ",\"container\":" << number(seconds.container) << "}}";
    return out.str();
}

}  // namespace qoiw
