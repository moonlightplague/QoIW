#include <iomanip>
#include <iostream>
#include <limits>

#include "arguments.hpp"
#include "common/shape.hpp"
#include "files.hpp"
#include "qoiw/core.hpp"
#include "qoiw/qoiw.hpp"

namespace qoiw::cli {
namespace {

constexpr int violations_exit = 2;

template <class Work>
int with_storage_type(DType dtype, Work work) {
    if (dtype == DType::Float32) return work(float{});
    return work(double{});
}

void print_stream(const StreamInfo& info, bool json) {
    if (json) {
        std::cout << info.to_json() << '\n';
        return;
    }
    std::cout << "Core " << std::quoted(info.core_name) << " version=" << std::quoted(info.core_version)
              << " format=" << std::quoted(info.core_format_version) << '\n'
              << (info.dtype == DType::Float32 ? "float32" : "float64") << " shape=";
    for (std::size_t i = 0; i < info.dims.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << info.dims[i];
    }
    std::cout << " values_per_field=" << info.num << " stream_bytes=" << info.stream_bytes << '\n';
    for (const auto& field : info.fields) {
        std::cout << "Field " << std::quoted(field.name) << " bound=" << field.bound << " data_bound_abs=";
        if (field.data_bound_abs) std::cout << *field.data_bound_abs;
        else std::cout << "none";
        std::cout << " core_bytes=" << field.core_bytes << " lossless_bytes=" << field.lossless_bytes
                  << " decode_params_bytes=" << field.decode_params_bytes << '\n';
    }
    for (const auto& target : info.targets)
        std::cout << "Target " << std::quoted(target.expression) << " tolerance_abs=" << target.tolerance_abs << '\n';
    if (info.exact_condition) std::cout << "Exact predicate " << std::quoted(*info.exact_condition) << '\n';
}

void print_errors(const ErrorStats& errors) {
    std::cout << " max_abs_error=" << errors.max_abs_error << " violations=" << errors.violations
              << " nonfinite_violations=" << errors.nonfinite_violations << '\n';
}

void print_verification(const Verification& result) {
    std::cout << (result.ok() ? "Verification passed" : "Verification failed")
              << " values=" << result.values << " violating_values=" << result.violating_values
              << " exact_violations=" << result.exact_violations << '\n';
    std::cout << "Data bound_abs=";
    if (result.data_bound_abs) std::cout << *result.data_bound_abs;
    else std::cout << "none";
    print_errors(result.data);
    for (const auto& target : result.targets) {
        std::cout << "QoI " << std::quoted(target.target.expression) << " tolerance_abs=" << target.target.tolerance_abs;
        print_errors(target.errors);
    }
}

void print_compression(const Report& report, const std::optional<Verification>& post_decode, std::size_t raw_bytes,
                       const Arguments& args) {
    if (args.json) {
        std::cout << "{\"report\":" << report.to_json()
                  << ",\"post_decode\":" << (post_decode ? post_decode->to_json() : "null") << "}\n";
        return;
    }
    print_stream(report.stream, false);
    std::cout << "Compression ratio=" << static_cast<double>(raw_bytes) / report.stream.stream_bytes
              << " restored=" << report.restored << " nonfinite_inputs=" << report.nonfinite_inputs << '\n';
    print_verification(report.verification);
    if (post_decode) {
        std::cout << "Finished container check\n";
        print_verification(*post_decode);
    }
    if (args.verbose) {
        if (report.plan.trace.empty()) std::cout << "Bound chosen by shortcut\n";
        for (const auto& trial : report.plan.trace) {
            std::cout << "Trial bound=" << trial.bound << " bytes_per_value=" << trial.bytes_per_value
                      << " sample_values=" << trial.sample_values << " core_bytes=" << trial.core_bytes
                      << " overhead_bytes=" << trial.overhead_bytes << " lossless_bytes=" << trial.lossless_bytes
                      << " correction_bytes_per_value=" << trial.correction_bytes_per_value
                      << " weight=" << trial.correction_weight << " restored=" << trial.restored
                      << " input_sensitive_share=" << trial.input_sensitive_share
                      << " sample_sensitive_share=" << trial.sample_sensitive_share
                      << " outlier_share=" << trial.outlier_share
                      << " rare_outlier_fallback=" << std::boolalpha << trial.rare_outlier_fallback;
            if (trial.sample_psnr_db) std::cout << " sample_psnr_db=" << *trial.sample_psnr_db;
            std::cout << '\n';
        }
    }
}

template <class T>
int compress_field(const Arguments& args) {
    check_distinct_files(args.input, args.stream);
    const auto bytes = common::array_bytes(*args.dtype, args.dims);
    const auto original = read_raw<T>(args.input, bytes);
    const ArrayView input(original.data(), args.dims);
    Report report;
    const auto stream = compress(input, args.request, args.options, &report);
    std::optional<Verification> post_decode;
    if (args.check_decode) {
        std::vector<T> reconstructed(original.size());
        decompress(stream, reconstructed.data(), reconstructed.size(), args.options.threads);
        post_decode = verify(input, ArrayView(reconstructed.data(), args.dims), args.request, args.options.threads);
    }
    const bool ok = report.verification.ok() && (!post_decode || post_decode->ok());
    if (ok && !args.stream.empty()) write_file(args.stream, stream.data(), stream.size());
    print_compression(report, post_decode, bytes, args);
    return ok ? 0 : violations_exit;
}

template <class T>
int verify_fields(const Arguments& args) {
    const auto bytes = common::array_bytes(*args.dtype, args.dims);
    const auto original = read_raw<T>(args.input, bytes);
    const auto reconstructed = read_raw<T>(args.output, bytes);
    const auto result = verify(ArrayView(original.data(), args.dims), ArrayView(reconstructed.data(), args.dims),
                               args.request, args.options.threads);
    if (args.json) std::cout << result.to_json() << '\n';
    else print_verification(result);
    return result.ok() ? 0 : violations_exit;
}

int decompress_file(const Arguments& args) {
    check_distinct_files(args.stream, args.output);
    const auto stream = read_container(args.stream);
    const auto info = inspect(stream);
    if (info.fields.size() != 1) throw std::invalid_argument("expected a single-field stream");
    if (!get_core(info.core_name).supports(info.dtype, info.dims))
        throw std::invalid_argument("core does not support the stream's shape and type");
    return with_storage_type(info.dtype, [&](auto value) {
        using T = decltype(value);
        std::vector<T> decoded(info.num);
        decompress(stream, decoded.data(), decoded.size(), args.options.threads);
        convert_byte_order(decoded);
        write_file(args.output, decoded.data(), decoded.size() * sizeof(T));
        print_stream(info, args.json);
        return 0;
    });
}

int run(const Arguments& args) {
    switch (args.command) {
        case Command::Compress:
            return with_storage_type(*args.dtype, [&](auto value) { return compress_field<decltype(value)>(args); });
        case Command::Decompress:
            return decompress_file(args);
        case Command::Inspect:
            print_stream(inspect(read_container(args.stream)), args.json);
            return 0;
        case Command::Verify:
            return with_storage_type(*args.dtype, [&](auto value) { return verify_fields<decltype(value)>(args); });
        case Command::Cores:
            for (const auto& name : available_cores()) {
                const auto& core = get_core(name);
                std::cout << name << " version=" << core.version() << " format=" << core.format_version() << '\n';
                for (const auto dtype : {DType::Float32, DType::Float64}) {
                    for (std::size_t rank = 1; rank <= 4; ++rank) {
                        if (!core.supports(dtype, std::vector<std::uint64_t>(rank, 2))) continue;
                        const auto traits = core.traits(dtype, rank);
                        std::cout << "  " << (dtype == DType::Float32 ? "float32" : "float64") << " rank=" << rank
                                  << " bound=" << (traits.bound_kind == BoundKind::StrictAbs ? "strict" : "nominal")
                                  << std::boolalpha << " thread_safe=" << traits.thread_safe
                                  << " fixed_call_overhead=" << traits.fixed_call_overhead << '\n';
                    }
                }
            }
            return 0;
    }
    throw std::logic_error("unhandled command");
}

}  // namespace
}  // namespace qoiw::cli

int main(int argc, char** argv) {
    try {
        std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
        const auto args = qoiw::cli::parse_arguments(argc, argv);
        return args ? qoiw::cli::run(*args) : 0;
    } catch (const std::exception& error) {
        std::cerr << "qoiw error: " << error.what() << '\n';
        return 1;
    }
}
