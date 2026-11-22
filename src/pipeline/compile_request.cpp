#include "pipeline/compile_request.hpp"

namespace qoiw::pipeline {

frontend::PointRequest compile_point_request(const Request& request) {
    if (request.targets.size() > UINT8_MAX) throw std::invalid_argument("too many point-wise targets");
    auto expression = [](const std::string& text) {
        if (text.size() > UINT16_MAX) throw std::invalid_argument("expression exceeds the container string limit");
        return Expr(text);
    };
    frontend::PointRequest compiled;
    compiled.targets.reserve(request.targets.size());
    for (const auto& target : request.targets)
        compiled.targets.push_back({expression(target.expression), target.tolerance});
    if (request.data_bound.mode != Tol::Mode::None) compiled.data_bound = request.data_bound;
    if (request.exact) compiled.exact = expression(*request.exact);
    return compiled;
}

}  // namespace qoiw::pipeline
