#pragma once

#include <map>

#include "frontend/prepare.hpp"

namespace qoiw::frontend {

using qoiw::CandidateStats;

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
    const PointResolution& resolved_;
    const PointSearch& search_;
    CoreParams params_;
    CoreTraits traits_;
    int threads_;
    double rare_outlier_share_;
    std::vector<double> block_radii_;
    std::size_t quality_values_ = 0;
    std::map<double, CandidateMeasurement> cache_;
};

}  // namespace qoiw::frontend
