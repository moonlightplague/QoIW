#pragma once

#include <optional>
#include <vector>

#include "qoiw/expr.hpp"

namespace qoiw {

// Encoder-side constraints for one field, after tolerance resolution.
struct PointTarget {
    Expr expression;
    double tolerance_abs;
};

struct ResolvedPointRequest {
    std::vector<PointTarget> targets;
    std::optional<double> data_bound_abs;
    std::optional<Expr> exact;
};

}  // namespace qoiw
