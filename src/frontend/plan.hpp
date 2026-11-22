#pragma once

#include "frontend/search.hpp"
#include "qoiw/report.hpp"

namespace qoiw::frontend {

// Complete F5/F6 for one prepared field. The returned plan owns its report data.
Plan plan_pointwise(const Core& core, const PointPreparation& prepared, CoreParams params = {},
                    const SearchOptions& options = {}, double rare_outlier_share = .1);

}  // namespace qoiw::frontend
