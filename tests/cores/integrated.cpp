#include "qoiw/qoiw.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

#include "container/container.hpp"

namespace {
using namespace qoiw;

void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

template <class Work>
void rejects(ErrorCode code, Work work) {
    try {
        work();
    } catch (const Error& error) {
        if (error.code() == code) return;
        throw;
    }
    throw std::runtime_error("invalid integrated API call succeeded");
}

template <class T>
void round_trip(bool fixed) {
    std::vector<T> data(65 * 67);
    for (std::size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<T>((static_cast<int>(i % 263) - 131) / 32.);
    data[1] = T(1e36);
    data[2] = -T(0);
    data[3] = std::numeric_limits<T>::quiet_NaN();
    data[4] = -std::numeric_limits<T>::infinity();
    const auto copy = data;

    Request request;
    request.targets = {Target::pointwise("x^2").rel(.001), Target::pointwise("sin(x)").abs(.08)};
    request.data_bound = Tol::rel(.015625);
    request.exact = "x > 1e30 or x == 0";
    const ArrayView input(data.data(), {1, 65, 67});
    Options options;
    options.core_options["algo"] = "nopred";
    options.frontend.sampling = {.01, 16, 16, 2, 4};
    if (fixed) options.fixed_bound = .5;
    Report report;
    const auto blob = compress(input, request, options, &report);
    options.threads = 7;
    check(blob == compress(input, request, options), "integrated stream depends on thread count");
    check(!std::memcmp(copy.data(), data.data(), data.size() * sizeof(T)), "compression modified the input");
    const auto info = inspect(blob);
    check(info.dtype == input.dtype && info.dims == input.dims && info.num == data.size() &&
          info.core_name == "sz3" && info.stream_bytes == blob.size() && info.fields.size() == 1 &&
          info.targets.size() == 2 && info.exact_condition == request.exact, "incorrect stream metadata");
    const auto& field = info.fields.at(0);
    const auto span = 262 / 32.;
    const auto square_range = (131. / 32) * (131. / 32) - (1. / 32) * (1. / 32);
    check(field.data_bound_abs == span * .015625 && field.bound == report.plan.bound.at(0) &&
          info.targets[0].expression == "x^2" && info.targets[0].tolerance_abs == square_range * .001 &&
          info.targets[1].expression == "sin(x)" && info.targets[1].tolerance_abs == .08 &&
          field.decode_params_bytes == 0 && field.core_bytes > 0, "incorrect stored bounds or section sizes");
    check(report.nonfinite_inputs == 2 && report.stream.stream_bytes == info.stream_bytes &&
          report.stream.fields[0].lossless_bytes == field.lossless_bytes && report.plan.flags.shortcut_taken == fixed &&
          report.plan.trace.empty() == fixed &&
          (fixed ? field.bound == *options.fixed_bound && field.lossless_bytes > 0 && report.restored > 0
                 : field.bound <= field.data_bound_abs.value()), "incorrect compression report");

    std::vector<T> out(info.num + 1, 123);
    decompress(blob, out.data(), out.size(), 0);
    check(out.back() == 123, "decoder touched capacity past the declared field");
    double max_data_error = 0, max_square_error = 0, max_sin_error = 0;
    for (std::size_t i = 0; i < data.size(); ++i) {
        const double x = data[i], y = out[i];
        if (!std::isfinite(x) || x > 1e30 || x == 0) {
            check(!std::memcmp(&data[i], &out[i], sizeof(T)), "decoder did not preserve exact bits");
        } else {
            check(y != 0 && y <= 1e30 && std::abs(x - y) <= field.data_bound_abs.value() &&
                  std::abs(x * x - y * y) <= info.targets[0].tolerance_abs &&
                  std::abs(std::sin(x) - std::sin(y)) <= info.targets[1].tolerance_abs,
                  "decoded value violates a constraint");
            max_data_error = std::max(max_data_error, std::abs(x - y));
            max_square_error = std::max(max_square_error, std::abs(x * x - y * y));
            max_sin_error = std::max(max_sin_error, std::abs(std::sin(x) - std::sin(y)));
        }
    }
    const auto checked = verify(input, ArrayView(out.data(), info.dims), request, 7);
    check(checked.ok() && checked.data.max_abs_error == max_data_error &&
          checked.targets.at(0).errors.max_abs_error == max_square_error &&
          checked.targets.at(1).errors.max_abs_error == max_sin_error, "verification differs from scalar checks");
    const auto& written = report.verification;
    check(written.ok() && written.values == checked.values && written.data_bound_abs == checked.data_bound_abs &&
          written.data.max_abs_error == max_data_error && written.targets.at(0).errors.max_abs_error == max_square_error &&
          written.targets.at(1).errors.max_abs_error == max_sin_error && report.seconds.verification >= 0,
          "report should describe the corrected reconstruction");
}

void bad_arguments() {
    const float data[]{1, 2, 3, 4};
    const ArrayView input(data, {4});
    Options options;
    options.core = "unavailable";
    rejects(ErrorCode::Unsupported, [&] { compress(input, {}, options); });
    rejects(ErrorCode::InvalidArgument, [&] { compress(ArrayView(data, {0})); });
    rejects(ErrorCode::CorruptStream, [] { inspect({}); });
    options.core = "sz3";
    options.fixed_bound = 0;
    Report report;
    const auto blob = compress(input, {}, options, &report);
    const auto info = inspect(blob);
    check(info.targets.empty() && !info.fields[0].data_bound_abs && !info.exact_condition &&
          report.restored == 0 && !info.fields[0].lossless_bytes, "unexpected empty-request metadata");
    float out[4]{};
    decompress(blob, out, 4);
    check(std::equal(data, data + 4, out), "zero bound should decode losslessly");

    Request request;
    request.targets = {Target::pointwise("x+").abs(.1)};
    rejects(ErrorCode::InvalidArgument, [&] { compress(input, request, options, &report); });
    request.targets = {Target::pointwise("x^2")};
    rejects(ErrorCode::InvalidArgument, [&] { compress(input, request, options, &report); });
    options.core_options["algo"] = "unknown";
    rejects(ErrorCode::InvalidArgument, [&] { compress(input, {}, options, &report); });
    check(report.stream.stream_bytes == blob.size() && report.plan.bound.at(0) == 0,
          "failed compression changed the caller's report");

    double wrong_type[4]{};
    rejects(ErrorCode::InvalidArgument, [&] { decompress(blob, wrong_type, 4); });
    rejects(ErrorCode::InvalidArgument, [&] { decompress(blob, out, 3); });
    rejects(ErrorCode::InvalidArgument, [&] { decompress(blob, out, 4, -1); });
    rejects(ErrorCode::CorruptStream, [&] { decompress({blob.data(), blob.size() - 1}, out, 4); });
    auto unavailable = container::read(blob);
    unavailable.core_name = "unavailable";
    const auto unavailable_blob = container::write(unavailable);
    check(inspect(unavailable_blob).core_name == "unavailable", "inspect should work without a matching core");
    rejects(ErrorCode::Unsupported, [&] { decompress(unavailable_blob, out, 4); });
    check(std::equal(data, data + 4, out) && wrong_type[0] == 0, "validation wrote to a rejected output");
}
}  // namespace

int main() {
    try {
        bad_arguments();
        round_trip<float>(true);
        round_trip<double>(false);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
