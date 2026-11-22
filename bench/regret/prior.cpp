#include <algorithm>
#include <iomanip>
#include <iostream>
#include <limits>

#include "common/shape.hpp"
#include "files.hpp"
#include "frontend/prior.hpp"

int main(int argc, char** argv) try {
    using namespace qoiw;
    if (argc < 7 || argc > 10)
        throw std::invalid_argument("expected input, expression, absolute QoI tolerance, absolute data bound, threads and shape");
    const frontend::PointRequest request{{{Expr(argv[2]), Tol::abs(std::stod(argv[3]))}},
                                         Tol::abs(std::stod(argv[4])), {}};
    const int threads = std::stoi(argv[5]);
    std::vector<std::uint64_t> shape;
    for (int i = 6; i < argc; ++i) shape.push_back(std::stoull(argv[i]));
    constexpr auto dtype = DType::Float32;
    const auto bytes = common::array_bytes(dtype, shape);
    if (!bytes || threads < 1) throw std::invalid_argument("invalid field shape or thread count");
    const auto input = cli::read_raw<float>(argv[1], bytes);
    const auto resolved = frontend::resolve_pointwise(dtype, input.data(), bytes, request, threads);
    const double span = resolved.data_range ? resolved.data_range->max - resolved.data_range->min : 0.;
    const double cap = std::min(span, *resolved.request.data_bound_abs);
    double minimum = 0.;
    if (cap > 0) {
        const auto prior = frontend::estimate_point_prior(dtype, input.data(), bytes, resolved, threads);
        const auto first = std::upper_bound(prior.sorted_radii.begin(), prior.sorted_radii.end(), 0.);
        minimum = first == prior.sorted_radii.end() ? cap * 1e-9 : std::max(cap * 1e-9, *first);
    }
    std::cout << std::setprecision(std::numeric_limits<double>::max_digits10) << minimum << ' ' << cap << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "qoiw regret prior: " << error.what() << '\n';
    return 1;
}
