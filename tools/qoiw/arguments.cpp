#include "arguments.hpp"

#include <CLI11.hpp>

#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <string_view>

#include "common/shape.hpp"

namespace qoiw::cli {
namespace {

using Shape = std::vector<std::uint64_t>;

const CLI::Validator nonempty{[](std::string& value) {
    return value.empty() ? "a nonempty value is required" : "";
}, "NONEMPTY", "nonempty"};

using ToleranceValues = std::pair<std::string, std::optional<double>>;

Tol tolerance(const ToleranceValues& values, const char* option, bool allow_none = false) {
    const auto& [mode, value] = values;
    if (allow_none && (mode == "NONE" || mode == "none") && !value) return Tol::none();
    if (value && std::isfinite(*value) && *value >= 0) {
        if (mode == "ABS" || mode == "abs") return Tol::abs(*value);
        if (mode == "REL" || mode == "rel") return Tol::rel(*value);
    }
    throw CLI::ValidationError(option, allow_none ? "expected ABS or REL with a finite nonnegative value, or NONE"
                                                   : "expected ABS or REL with a finite nonnegative value");
}

Shape c_order_shape(std::string_view csv) {
    // The comma-delimited spelling is a single CLI11 option value. Check the
    // separators here so an empty extent cannot be dropped.
    Shape dims;
    for (;;) {
        const auto end = csv.find(',');
        const auto extent = csv.substr(0, end);
        std::uint64_t value{};
        const auto* last = extent.data() + extent.size();
        const auto parsed = std::from_chars(extent.data(), last, value);
        if (parsed.ec != std::errc{} || parsed.ptr != last || value == 0 || dims.size() == 4)
            throw CLI::ValidationError("--shape", "expected one to four positive comma-separated extents");
        dims.push_back(value);
        if (end == std::string_view::npos) return dims;
        csv.remove_prefix(end + 1);
    }
}

void add_raw_options(CLI::App& app, Arguments& args) {
    app.add_option("-i,--input", args.input, "Original raw file")->required()->check(nonempty)->take_last();

    auto* storage = app.add_option_group("Raw storage")->require_option(1);
    storage->add_flag_callback("-f,--float32", [&args] { args.dtype = DType::Float32; }, "Little-endian float32");
    storage->add_flag_callback("-d,--float64", [&args] { args.dtype = DType::Float64; }, "Little-endian float64");

    auto* shape = app.add_option_group("Raw shape")->require_option(1);
    shape->add_option_function<std::string>("--shape", [&args](const auto& csv) { args.dims = c_order_shape(csv); },
                                             "Comma-separated extents in C order")
        ->type_name("N[,N...]");
    for (int rank = 1; rank <= 4; ++rank) {
        shape->add_option_function<Shape>("-" + std::to_string(rank),
                                           [&args](const Shape& dims) { args.dims.assign(dims.rbegin(), dims.rend()); },
                                           "Extents in fastest-varying-first order")
            ->type_size(rank)
            ->expected(1)
            ->allow_extra_args(false)
            ->check(CLI::Range(std::uint64_t{1}, UINT64_MAX, "POSITIVE"));
    }

    app.add_option_function<std::string>("-q,--qoi", [&args](const auto& expression) {
        args.request.targets.push_back(Target::pointwise(expression));
    }, "Start a point-wise target. Repeat for multiple targets")->check(nonempty)->trigger_on_parse();

    // CLI11 invokes these callbacks in argument order so each tolerance is
    // associated with the target immediately preceding it.
    app.add_option_function<std::pair<std::string, double>>("-m,--tolerance", [&args](const auto& values) {
        auto& targets = args.request.targets;
        if (targets.empty() || targets.back().tolerance.mode != Tol::Mode::None)
            throw CLI::ValidationError("--tolerance", "specify one tolerance after each QoI");
        targets.back().tolerance = tolerance({values.first, values.second}, "--tolerance");
    }, "ABS or REL and a value for the preceding target")->type_name("MODE VALUE")->trigger_on_parse();

    app.add_option_function<ToleranceValues>("-M,--data-bound", [&args](const auto& values) {
        args.request.data_bound = tolerance(values, "--data-bound", true);
    }, "ABS or REL and a value, or NONE (default)")
        ->type_size(1, 2)->type_name("MODE [VALUE]")->trigger_on_parse();
    app.add_option("-X,--exact", args.request.exact, "Preserve values satisfying the predicate bit for bit")
        ->check(nonempty)->take_last();
}

void add_compression_options(CLI::App& app, Arguments& args) {
    app.add_option("-c,--core", args.options.core, "Compressor")->check(nonempty)->take_last()->capture_default_str();
    app.add_option_function<std::string>("--core-option", [&args](const auto& item) {
        const auto equals = item.find('=');
        if (equals == std::string::npos || equals == 0 || equals + 1 == item.size())
            throw CLI::ValidationError("--core-option", "expected NAME=VALUE");
        args.options.core_options[item.substr(0, equals)] = item.substr(equals + 1);
    }, "Set an adapter option such as algo=interp")->type_name("NAME=VALUE")->trigger_on_parse();
    app.add_option_function<double>("--fixed-bound", [&args](double value) {
        if (!std::isfinite(value) || value < 0)
            throw CLI::ValidationError("--fixed-bound", "expected a finite nonnegative bound");
        args.options.fixed_bound = value;
    }, "Supply a bound and bypass tuning")->take_last();
    app.add_flag("--allow-stream-changing-parallelism", args.options.allow_stream_changing_parallelism,
                 "Allow the compressor to vary its stream with the thread count");
    app.add_flag("--verify-patch", args.options.verify_patch, "Apply the encoded correction layer during compression");
    app.add_flag("-a,--verify", args.check_decode, "Decode and check the finished stream before writing");
    app.add_flag("-v,--verbose", args.verbose, "Include all search trials in the text report");
}

CLI::App& subcommand(CLI::App& root, const char* name, const char* description, Command command, Arguments& args) {
    CLI::App& app = *root.add_subcommand(name, description);
    app.callback([&args, command] { args.command = command; });
    if (command == Command::Compress || command == Command::Decompress || command == Command::Verify) {
        app.add_option("-t,--threads", args.options.threads, "Thread count (zero uses the runtime default)")
            ->check(CLI::Range(0, std::numeric_limits<int>::max(), "NONNEGATIVE"))->take_last()->capture_default_str();
    }
    if (command != Command::Cores) app.add_flag("--json", args.json, "Print a JSON report");
    const std::map<std::string, ColorMode> colors{
        {"auto", ColorMode::Auto}, {"always", ColorMode::Always}, {"never", ColorMode::Never}};
    app.add_option("--color", args.color, "Colour the text report: auto (terminal only), always or never")
        ->transform(CLI::CheckedTransformer(colors, CLI::ignore_case))->take_last()->type_name("WHEN");
    return app;
}

void validate_request(const Arguments& args) {
    if (args.command != Command::Compress && args.command != Command::Verify) return;
    if (!args.dtype) throw std::invalid_argument("missing storage type");
    if (!common::array_bytes(*args.dtype, args.dims)) throw std::invalid_argument("invalid or overflowing shape");
    for (const auto& target : args.request.targets) {
        if (target.tolerance.mode == Tol::Mode::None)
            throw std::invalid_argument("missing tolerance for " + target.expression);
    }
}

}  // namespace

std::optional<Arguments> parse_arguments(int argc, char** argv) {
    Arguments args;
    CLI::App app("Compress and verify fields with point-wise QoI tolerances", "qoiw");
    app.require_subcommand(1);
    app.footer("Exit status 0 on success, 2 on verification violations, 1 on other errors.");

    auto& compress = subcommand(app, "compress", "Compress a raw field", Command::Compress, args);
    add_raw_options(compress, args);
    add_compression_options(compress, args);
    compress.add_option("-z,--stream", args.stream, "Output QoIW container")->check(nonempty)->take_last();

    auto& decompress = subcommand(app, "decompress", "Decode a QoIW container", Command::Decompress, args);
    decompress.add_option("-z,--stream", args.stream, "Input QoIW container")->required()->check(nonempty)->take_last();
    decompress.add_option("-o,--output", args.output, "Output raw file")->required()->check(nonempty)->take_last();

    auto& inspect = subcommand(app, "inspect", "Read container metadata", Command::Inspect, args);
    inspect.add_option("-z,--stream", args.stream, "Input QoIW container")->required()->check(nonempty)->take_last();

    auto& verify = subcommand(app, "verify", "Check two raw fields", Command::Verify, args);
    add_raw_options(verify, args);
    verify.add_option("-o,--reconstructed", args.output, "Reconstructed raw field")->required()->check(nonempty)->take_last();

    subcommand(app, "cores", "List built-in compression cores", Command::Cores, args);
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        if (error.get_exit_code() != 0) throw std::invalid_argument(error.what());
        const auto extras = app.remaining(true);
        if (!extras.empty()) throw std::invalid_argument("unexpected argument " + extras.front());
        app.exit(error);
        return std::nullopt;
    }
    validate_request(args);
    return args;
}

}  // namespace qoiw::cli
