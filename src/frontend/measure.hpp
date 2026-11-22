#pragma once

#include <map>

#include "frontend/prepare.hpp"

namespace qoiw::frontend {

struct CandidateStats {
    double bound = 0;
    std::size_t sample_values = 0, restored = 0;
    std::size_t core_bytes = 0, overhead_bytes = 0, lossless_bytes = 0;
    double input_sensitive_share = 0, sample_sensitive_share = 0, correction_weight = 1;
    bool rare_outlier_fallback = false;
    double correction_bytes_per_value = 0, bytes_per_value = 0;
};

struct CandidateMeasurement : CandidateStats {
    SampleBlocks decoded;  // Raw core reconstruction, before any correction.
};

// One context per search. The core and F0/F3 results must stay alive and
// unchanged. Call measure sequentially. Individual blocks run in parallel.
class PointMeasurer {
   public:
    PointMeasurer(const Core& core, const PointResolution& resolved, const PointSearch& search,
                  CoreParams params = {}, double rare_outlier_share = .1);

    // References remain valid for the lifetime of this context.
    const CandidateMeasurement& measure(double bound);

   private:
    const Core& core_;
    const ResolvedPointRequest& request_;
    const PointSearch& search_;
    CoreParams params_;
    CoreTraits traits_;
    int threads_;
    double rare_outlier_share_;
    std::vector<double> block_radii_;
    std::map<double, CandidateMeasurement> cache_;
};

}  // namespace qoiw::frontend
