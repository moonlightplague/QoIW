#pragma once

#include "frontend/resolve.hpp"
#include "qoiw/request.hpp"

namespace qoiw::pipeline {

frontend::PointRequest compile_point_request(const Request& request);

}  // namespace qoiw::pipeline
