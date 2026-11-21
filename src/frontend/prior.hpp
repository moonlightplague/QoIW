#pragma once

#include "qoi/pointwise.hpp"

namespace qoiw::frontend {

// F1 estimate at one storage value. The cap and resolved tolerance must be
// finite and nonnegative. The correction pass checks actual reconstructions.
double pointwise_radius(const PointTarget& target, double x, double cap);

}  // namespace qoiw::frontend
