#pragma once

#include <optional>
#include <string>

#include "qoiw/types.hpp"

namespace qoiw {

struct Target {
    std::string expression;
    Tol tolerance = Tol::none();

    static Target pointwise(std::string expression) { return {std::move(expression)}; }
    Target abs(double value) const { return {expression, Tol::abs(value)}; }
    Target rel(double value) const { return {expression, Tol::rel(value)}; }
};

struct Request {
    std::vector<Target> targets;
    Tol data_bound = Tol::none();
    std::optional<std::string> exact;
};

}  // namespace qoiw
