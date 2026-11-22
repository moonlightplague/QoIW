#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "qoiw/types.hpp"

namespace qoiw {

enum class Binding { Qoi, DataBound, Tie };
enum class CorrectionMode { Lossless, Residual };
enum class ShortcutReason { FixedBound, ZeroRadii, SingleCandidate };

// One measured bound. Byte counts include decode parameters and encoded
// lossless corrections before the cost model's weighting and deductions.
struct CandidateStats {
    double bound = 0;
    std::size_t sample_values = 0, restored = 0;
    std::size_t core_bytes = 0, overhead_bytes = 0, lossless_bytes = 0;
    double input_sensitive_share = 0, sample_sensitive_share = 0, correction_weight = 1;
    bool rare_outlier_fallback = false;
    double outlier_share = 0;  // Estimated fraction of full-field values requiring correction.
    double correction_bytes_per_value = 0, bytes_per_value = 0;
    std::optional<double> sample_psnr_db;

    double core_bytes_per_value() const {
        return static_cast<double>(core_bytes - overhead_bytes) / sample_values;
    }
};

struct PlanPrediction {
    double core_bytes_per_value = 0, correction_bytes_per_value = 0, bytes_per_value = 0;
    double outlier_share = 0;
    std::optional<double> sample_psnr_db;
    std::optional<double> indicative_compression_ratio;
};

struct PlanFlags {
    bool whole_input_sampled = false, prior_free = false;
    bool rare_outlier_fallback = false, shortcut_taken = false;
};

struct TargetTolerance {
    std::string expression;
    double tolerance_abs = 0;
};

struct ErrorStats {
    // Maximum over pairs with finite operands. Subtraction can overflow to infinity.
    double max_abs_error = 0;
    std::size_t violations = 0;
    std::size_t nonfinite_violations = 0;  // Subset of violations.
};

struct TargetVerification {
    TargetTolerance target;
    ErrorStats errors;
};

struct Verification {
    std::size_t values = 0;
    std::size_t violating_values = 0;  // Counts a value once even if several checks fail.
    std::size_t exact_violations = 0;
    std::optional<double> data_bound_abs;
    ErrorStats data;
    std::vector<TargetVerification> targets;  // Request order.

    bool ok() const noexcept { return violating_values == 0; }
};

struct Plan {
    std::vector<double> bound;
    double reference_bound = 0;
    CorrectionMode correction_mode = CorrectionMode::Lossless;
    std::optional<Binding> binding;
    std::optional<double> data_bound_abs;
    std::optional<std::string> exact_condition;
    std::vector<TargetTolerance> targets;
    std::optional<PlanPrediction> predicted;
    std::vector<CandidateStats> trace;  // Unique trials in measurement order.
    PlanFlags flags;
    std::optional<ShortcutReason> shortcut_reason;

    // Missing measurements are JSON null. Infinite PSNR is "+inf" or "-inf".
    std::string to_json() const;
};

struct FieldInfo {
    std::string name;
    double bound = 0;
    std::optional<double> data_bound_abs;
    std::size_t decode_params_bytes = 0;
    std::size_t core_bytes = 0;
    std::size_t lossless_bytes = 0;
};

struct StreamInfo {
    DType dtype = DType::Float32;
    std::vector<std::uint64_t> dims;
    std::size_t num = 0;  // Values per field.
    std::size_t stream_bytes = 0;
    std::string wrapper_version, core_name, core_version, core_format_version;
    std::vector<FieldInfo> fields;
    std::vector<TargetTolerance> targets;
    std::optional<std::string> exact_condition;
};

struct CompressionTimes {
    double planning = 0, core_encode = 0, core_decode = 0, correction = 0, container = 0;  // Seconds.
    double verification = 0;
};

struct Report {
    Plan plan;
    StreamInfo stream;
    std::size_t nonfinite_inputs = 0;
    std::size_t restored = 0;
    CompressionTimes seconds;
    Verification verification;  // On the corrected reconstruction.
};

}  // namespace qoiw
