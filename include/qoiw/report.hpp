#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

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
    double correction_bytes_per_value = 0, bytes_per_value = 0;
    std::optional<double> sample_psnr_db;

    double core_bytes_per_value() const {
        return static_cast<double>(core_bytes - overhead_bytes) / sample_values;
    }
};

struct PlanPrediction {
    double core_bytes_per_value = 0, correction_bytes_per_value = 0, bytes_per_value = 0;
    double outlier_share = 0;  // Estimated fraction of full-field values requiring correction.
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

struct Plan {
    std::vector<double> bound;
    double reference_bound = 0;
    CorrectionMode correction_mode = CorrectionMode::Lossless;
    std::optional<Binding> binding;
    std::optional<double> data_bound_abs;
    std::vector<TargetTolerance> targets;
    std::optional<PlanPrediction> predicted;
    std::vector<CandidateStats> trace;  // Unique trials in measurement order.
    PlanFlags flags;
    std::optional<ShortcutReason> shortcut_reason;
};

}  // namespace qoiw
