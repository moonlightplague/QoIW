#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "qoiw/types.hpp"

namespace qoiw {

struct PriorOptions {
    // Zero or an empty list selects the corresponding default from PLAN.md.
    std::size_t point_samples = 0;
    std::vector<double> quantiles;
};

struct BlockSamplingOptions {
    double sample_rate = .01;
    // Zero selects the rank-specific defaults.
    std::size_t block_edge = 0, min_edge = 0, min_blocks = 0, max_blocks = 0;
};

struct SearchOptions {
    int refine_rounds = 4;
    double tie_bytes = .025;  // Relative to the cheapest eligible trial.
};

struct FrontendOptions {
    PriorOptions prior;
    BlockSamplingOptions sampling;
    SearchOptions search;
    double rare_outlier_share = .1;
};

struct Options {
    std::string core = "sz3";
    std::map<std::string, std::string> core_options;
    int threads = 1;  // Zero uses the runtime default.
    bool allow_stream_changing_parallelism = false;
    std::optional<double> fixed_bound;
    FrontendOptions frontend;
};

}  // namespace qoiw
