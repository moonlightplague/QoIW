#include <chrono>
#include <iostream>
#include <type_traits>
#include <utility>

#include "arguments.hpp"
#include "common/shape.hpp"
#include "files.hpp"
#include "qoiw/core.hpp"
#include "qoiw/qoiw.hpp"
#include "render.hpp"

namespace qoiw::cli {
namespace {

constexpr int violations_exit = 2;
using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// Run work and add its wall-clock seconds to elapsed.
template <class Work>
auto timed(double& elapsed, Work work) {
    const auto start = Clock::now();
    if constexpr (std::is_void_v<decltype(work())>) {
        work();
        elapsed += seconds_since(start);
    } else {
        auto result = work();
        elapsed += seconds_since(start);
        return result;
    }
}

template <class Work>
int with_storage_type(DType dtype, Work work) {
    if (dtype == DType::Float32) return work(float{});
    return work(double{});
}

template <class T>
int compress_field(const Arguments& args, Console& console) {
    CompressionRun run;
    check_distinct_files(args.input, args.stream);
    const auto bytes = common::array_bytes(*args.dtype, args.dims);
    const auto original = timed(run.read_input, [&] { return read_raw<T>(args.input, bytes); });
    const ArrayView input(original.data(), args.dims);
    Report report;
    const auto stream = compress(input, args.request, args.options, &report);
    std::optional<Verification> post_decode;
    if (args.check_decode) {
        std::vector<T> reconstructed(original.size());
        DecompressionTimes times;
        decompress(stream, reconstructed.data(), reconstructed.size(), args.options.threads, &times);
        run.check_decode = times;
        post_decode = timed(run.check_verify, [&] {
            return verify(input, ArrayView(reconstructed.data(), args.dims), args.request, args.options.threads);
        });
    }
    const bool ok = report.verification.ok() && (!post_decode || post_decode->ok());
    if (ok && !args.stream.empty()) {
        timed(run.write_stream, [&] { write_file(args.stream, stream.data(), stream.size()); });
        run.written = true;
    }
    if (args.json) std::cout << compression_json(report, post_decode, run) << '\n';
    else print_compression(console, args, report, post_decode, run);
    return ok ? 0 : violations_exit;
}

template <class T>
int verify_fields(const Arguments& args, Console& console) {
    const auto start = Clock::now();
    VerificationRun run;
    const auto bytes = common::array_bytes(*args.dtype, args.dims);
    const auto fields = timed(run.read_inputs, [&] {
        return std::pair(read_raw<T>(args.input, bytes), read_raw<T>(args.output, bytes));
    });
    const auto result = timed(run.verify, [&] {
        return verify(ArrayView(fields.first.data(), args.dims), ArrayView(fields.second.data(), args.dims),
                      args.request, args.options.threads);
    });
    run.total = seconds_since(start);
    if (args.json) std::cout << result.to_json() << '\n';
    else print_verification(console, args, result, run);
    return result.ok() ? 0 : violations_exit;
}

int decompress_file(const Arguments& args, Console& console) {
    DecompressionRun run;
    check_distinct_files(args.stream, args.output);
    const auto stream = timed(run.read_stream, [&] { return read_container(args.stream); });
    const auto info = inspect(stream);
    if (info.fields.size() != 1) throw std::invalid_argument("expected a single-field stream");
    if (!get_core(info.core_name).supports(info.dtype, info.dims))
        throw std::invalid_argument("core does not support the stream's shape and type");
    with_storage_type(info.dtype, [&](auto value) {
        using T = decltype(value);
        std::vector<T> decoded(info.num);
        decompress(stream, decoded.data(), decoded.size(), args.options.threads, &run.decode);
        timed(run.write_output, [&] {
            convert_byte_order(decoded);
            write_file(args.output, decoded.data(), decoded.size() * sizeof(T));
        });
        return 0;
    });
    if (args.json) std::cout << decompression_json(info, run) << '\n';
    else print_decompression(console, args, info, run);
    return 0;
}

int run(const Arguments& args) {
    Console console(std::cout, !args.json && use_color(args.color));
    switch (args.command) {
        case Command::Compress:
            return with_storage_type(*args.dtype,
                                     [&](auto value) { return compress_field<decltype(value)>(args, console); });
        case Command::Decompress:
            return decompress_file(args, console);
        case Command::Inspect: {
            const auto info = inspect(read_container(args.stream));
            if (args.json) std::cout << info.to_json() << '\n';
            else print_inspection(console, args, info);
            return 0;
        }
        case Command::Verify:
            return with_storage_type(*args.dtype,
                                     [&](auto value) { return verify_fields<decltype(value)>(args, console); });
        case Command::Cores:
            print_cores(console);
            return 0;
    }
    throw std::logic_error("unhandled command");
}

}  // namespace
}  // namespace qoiw::cli

int main(int argc, char** argv) {
    try {
        const auto args = qoiw::cli::parse_arguments(argc, argv);
        return args ? qoiw::cli::run(*args) : 0;
    } catch (const std::exception& error) {
        std::cerr << "qoiw error: " << error.what() << '\n';
        return 1;
    }
}
