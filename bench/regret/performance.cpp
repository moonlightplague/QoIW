#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

#include "common/shape.hpp"
#include "files.hpp"
#include "qoiw/core.hpp"
#include "qoiw/qoiw.hpp"

namespace {
using namespace qoiw;
using Clock = std::chrono::steady_clock;

double elapsed(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

long peak_rss_kib() {
    // Linux rusage can carry a larger peak forward from before exec.
    // VmHWM covers the address space used by this probe.
    std::ifstream status("/proc/self/status");
    constexpr std::string_view field = "VmHWM:";
    std::string line;
    while (std::getline(status, line)) {
        if (line.compare(0, field.size(), field) == 0) return std::stol(line.substr(field.size()));
    }
    throw std::runtime_error("cannot read VmHWM from /proc/self/status");
}

void wrapped(const ArrayView& input, std::size_t count, int threads, const Request& request) {
    Options options;
    options.threads = threads;
    Report report;
    auto start = Clock::now();
    const auto stream = compress(input, request, options, &report);
    const double encode = elapsed(start);
    const auto peak = peak_rss_kib();
    if (!report.verification.ok()) throw std::runtime_error("QoIW verification failed");

    std::vector<float> output(count);
    start = Clock::now();
    decompress(stream, output.data(), count, threads);
    const double decode = elapsed(start);
    std::cout << "{\"encode_seconds\":" << encode << ",\"decode_seconds\":" << decode
              << ",\"planning_seconds\":" << report.seconds.planning
              << ",\"verification_seconds\":" << report.seconds.verification
              << ",\"core_encode_seconds\":" << report.seconds.core_encode
              << ",\"core_decode_seconds\":" << report.seconds.core_decode
              << ",\"correction_seconds\":" << report.seconds.correction
              << ",\"container_seconds\":" << report.seconds.container
              << ",\"bound\":" << report.plan.bound.at(0) << ",\"stream_bytes\":" << stream.size()
              << ",\"core_bytes\":" << report.stream.fields.at(0).core_bytes
              << ",\"restored\":" << report.restored << ",\"compress_peak_rss_kib\":" << peak << "}\n";
}

void plain(const ArrayView& input, std::size_t count, int threads, double bound) {
    const auto& core = get_core("sz3");
    const CoreParams params{bound, threads, false, {}};
    auto start = Clock::now();
    const auto payload = core.compress(input.data, input.dtype, input.dims, params);
    const double encode = elapsed(start);
    const auto peak = peak_rss_kib();

    std::vector<float> output(count);
    start = Clock::now();
    core.decompress(payload.stream, payload.decode_params, input.dtype, input.dims, threads,
                    output.data(), count * sizeof(float));
    const double decode = elapsed(start);
    std::cout << "{\"encode_seconds\":" << encode << ",\"decode_seconds\":" << decode
              << ",\"bound\":" << bound << ",\"core_bytes\":" << payload.stream.size()
              << ",\"compress_peak_rss_kib\":" << peak << "}\n";
}
}  // namespace

int main(int argc, char** argv) try {
    if (argc < 2) throw std::invalid_argument("expected wrapped or plain benchmark mode");
    const std::string_view mode = argv[1];
    const int first_dim = mode == "wrapped" ? 7 : 5;
    if ((mode != "wrapped" && mode != "plain") || argc <= first_dim || argc > first_dim + 4)
        throw std::invalid_argument("expected mode, input, threads, tolerances and shape");
    const int threads = std::stoi(argv[3]);
    std::vector<std::uint64_t> dims;
    for (int i = first_dim; i < argc; ++i) dims.push_back(std::stoull(argv[i]));
    const auto bytes = common::array_bytes(DType::Float32, dims);
    if (!bytes || threads < 1) throw std::invalid_argument("invalid field shape or thread count");
    const auto values = cli::read_raw<float>(argv[2], bytes);
    const ArrayView input(values.data(), std::move(dims));
    std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
    if (mode == "wrapped") {
        const Request request{{Target::pointwise(argv[4]).abs(std::stod(argv[5]))}, Tol::abs(std::stod(argv[6])), {}};
        wrapped(input, values.size(), threads, request);
    } else {
        plain(input, values.size(), threads, std::stod(argv[4]));
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << "qoiw performance probe: " << error.what() << '\n';
    return 1;
}
