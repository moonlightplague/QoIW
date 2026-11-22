#include "arguments.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <type_traits>

#include "common/shape.hpp"

namespace qoiw::cli {
namespace {
using std::string_view;
using std::invalid_argument;

class Tokens {
   public:
    Tokens(int argc, char** argv) : current_(argv), end_(argv + argc) {}

    bool empty() const { return current_ == end_; }
    string_view next(string_view flag) {
        if (empty()) throw invalid_argument("missing value for " + std::string(flag));
        const string_view token = *current_++;
        if (token.empty()) throw invalid_argument("empty value for " + std::string(flag));
        return token;
    }

   private:
    char** current_;
    char** end_;
};

Command parse_command(string_view name) {
    if (name == "compress") return Command::Compress;
    if (name == "decompress") return Command::Decompress;
    if (name == "inspect") return Command::Inspect;
    if (name == "verify") return Command::Verify;
    if (name == "cores") return Command::Cores;
    throw invalid_argument("unknown command " + std::string(name));
}

template <class T>
T nonnegative(string_view token, string_view flag) {
    T value{};
    const auto end = token.data() + token.size();
    const auto parsed = std::from_chars(token.data(), end, value);
    bool invalid = parsed.ec != std::errc{} || parsed.ptr != end;
    if constexpr (std::is_signed_v<T>) invalid = invalid || value < 0;
    if constexpr (std::is_floating_point_v<T>) invalid = invalid || !std::isfinite(value);
    if (invalid)
        throw invalid_argument("invalid numeric value for " + std::string(flag) + " (" + std::string(token) + ")");
    return value;
}

Tol parse_tolerance(Tokens& tokens, string_view flag, bool allow_none) {
    const auto mode = tokens.next(flag);
    if (mode == "ABS" || mode == "abs") return Tol::abs(nonnegative<double>(tokens.next(flag), flag));
    if (mode == "REL" || mode == "rel") return Tol::rel(nonnegative<double>(tokens.next(flag), flag));
    if (allow_none && (mode == "NONE" || mode == "none")) return Tol::none();
    throw invalid_argument(std::string(flag) + (allow_none ? " expects ABS, REL or NONE" : " expects ABS or REL"));
}

std::vector<std::uint64_t> parse_dimensions(string_view flag, Tokens& tokens) {
    std::vector<std::uint64_t> dims;
    if (flag == "--shape") {
        auto csv = tokens.next(flag);
        for (;;) {
            const auto comma = csv.find(',');
            dims.push_back(nonnegative<std::uint64_t>(csv.substr(0, comma), flag));
            if (comma == string_view::npos) break;
            if (dims.size() == 4) throw invalid_argument("shape must have one to four dimensions");
            csv.remove_prefix(comma + 1);
        }
    } else {
        const auto rank = flag[1] - '0';
        for (int i = 0; i < rank; ++i) dims.push_back(nonnegative<std::uint64_t>(tokens.next(flag), flag));
        std::reverse(dims.begin(), dims.end());
    }
    return dims;
}

void set_dtype(Arguments& args, DType dtype) {
    if (args.dtype && *args.dtype != dtype) throw invalid_argument("conflicting raw storage types");
    args.dtype = dtype;
}

bool raw_option(Arguments& args, string_view flag, Tokens& tokens) {
    if (flag == "-f" || flag == "--float32") {
        set_dtype(args, DType::Float32);
    } else if (flag == "-d" || flag == "--float64") {
        set_dtype(args, DType::Float64);
    } else if (flag == "--shape" || flag == "-1" || flag == "-2" || flag == "-3" || flag == "-4") {
        if (!args.dims.empty()) throw invalid_argument("specify the shape once using --shape or -1 through -4");
        args.dims = parse_dimensions(flag, tokens);
    } else if (flag == "-i" || flag == "--input") {
        args.input = tokens.next(flag);
    } else if (flag == "-q" || flag == "--qoi") {
        args.request.targets.push_back(Target::pointwise(std::string(tokens.next(flag))));
    } else if (flag == "-m" || flag == "--tolerance") {
        if (args.request.targets.empty() || args.request.targets.back().tolerance.mode != Tol::Mode::None)
            throw invalid_argument("specify one tolerance after each QoI");
        args.request.targets.back().tolerance = parse_tolerance(tokens, flag, false);
    } else if (flag == "-M" || flag == "--data-bound") {
        args.request.data_bound = parse_tolerance(tokens, flag, true);
    } else if (flag == "-X" || flag == "--exact") {
        args.request.exact = tokens.next(flag);
    } else {
        return false;
    }
    return true;
}

bool compression_option(Arguments& args, string_view flag, Tokens& tokens) {
    if (flag == "-c" || flag == "--core") {
        args.options.core = tokens.next(flag);
    } else if (flag == "--core-option") {
        const auto item = tokens.next(flag);
        const auto equals = item.find('=');
        if (equals == string_view::npos || equals == 0 || equals + 1 == item.size())
            throw invalid_argument("--core-option expects NAME=VALUE");
        args.options.core_options[std::string(item.substr(0, equals))] = item.substr(equals + 1);
    } else if (flag == "--fixed-bound") {
        args.options.fixed_bound = nonnegative<double>(tokens.next(flag), flag);
    } else if (flag == "--allow-stream-changing-parallelism") {
        args.options.allow_stream_changing_parallelism = true;
    } else if (flag == "-a" || flag == "--verify") {
        args.check_decode = true;
    } else if (flag == "-v" || flag == "--verbose") {
        args.verbose = true;
    } else {
        return false;
    }
    return true;
}
}  // namespace

bool is_help(std::string_view option) {
    return option == "-h" || option == "--help";
}

Arguments parse_arguments(int argc, char** argv) {
    Tokens tokens(argc, argv);
    Arguments args;
    args.command = parse_command(tokens.next("command"));
    if (argc == 2 && is_help(argv[1])) {
        args.help = true;
        return args;
    }
    const bool compressing = args.command == Command::Compress;
    const bool verifying = args.command == Command::Verify;
    const bool decompressing = args.command == Command::Decompress;
    while (!tokens.empty()) {
        const auto flag = tokens.next("option");
        if (flag == "--json" && args.command != Command::Cores) {
            args.json = true;
        } else if ((flag == "-t" || flag == "--threads") && (compressing || verifying || decompressing)) {
            args.options.threads = nonnegative<int>(tokens.next(flag), flag);
        } else if ((flag == "-z" || flag == "--stream") && (compressing || decompressing || args.command == Command::Inspect)) {
            args.stream = tokens.next(flag);
        } else if ((decompressing && (flag == "-o" || flag == "--output")) ||
                   (verifying && (flag == "-o" || flag == "--reconstructed"))) {
            args.output = tokens.next(flag);
        } else if (!((compressing || verifying) && raw_option(args, flag, tokens)) &&
                   !(compressing && compression_option(args, flag, tokens))) {
            throw invalid_argument("unknown option " + std::string(flag));
        }
    }
    if (compressing || verifying) {
        if (args.input.empty() || !args.dtype || args.dims.empty())
            throw invalid_argument("raw input requires --input, a storage type and a shape");
        if (!common::array_bytes(*args.dtype, args.dims)) throw invalid_argument("invalid or overflowing shape");
        for (const auto& target : args.request.targets)
            if (target.tolerance.mode == Tol::Mode::None) throw invalid_argument("missing tolerance for " + target.expression);
    }
    if ((decompressing || args.command == Command::Inspect) && args.stream.empty())
        throw invalid_argument("missing --stream");
    if ((verifying || decompressing) && args.output.empty())
        throw invalid_argument(verifying ? "missing --reconstructed" : "missing --output");
    return args;
}

std::string_view usage() {
    return R"(Usage
  qoiw compress -i RAW (-f|-d) --shape N[,N...] [-z STREAM] [request options] [compression options]
  qoiw decompress -z STREAM -o RAW [-t N] [--json]
  qoiw inspect -z STREAM [--json]
  qoiw verify -i RAW -o RECONSTRUCTED (-f|-d) --shape N[,N...] [request options] [-t N] [--json]
  qoiw cores

Raw fields
  -i, --input FILE          Original raw file
  -f, --float32             Little-endian float32
  -d, --float64             Little-endian float64
  --shape N[,N...]          C-order extents
  -1 through -4             Alternative shape flags with fastest-varying extent first
  -o, --output FILE         Decompression output
  -o, --reconstructed FILE  Field to check in verify
  -z, --stream FILE         QoIW container to write or read

Request options
  -q, --qoi EXPR            Start a point-wise target, repeat for multiple targets
  -m, --tolerance MODE V    Set ABS or REL tolerance for the preceding target
  -M, --data-bound MODE [V] Set ABS, REL or NONE data bound (default NONE)
  -X, --exact EXPR          Preserve values satisfying the predicate bit for bit

Compression options
  -c, --core NAME           Compressor (default sz3)
  --core-option NAME=VALUE  Set an adapter option such as algo=interp
  --fixed-bound V           Supply a bound and bypass tuning
  --allow-stream-changing-parallelism
  -a, --verify              Decode and check the finished stream before writing
  -v, --verbose             Include all search trials in the text report
  -t, --threads N           Set thread count (default 1, zero uses runtime default)
  --json                    Print a JSON report

Exit status 0 on success, 2 on verification violations, 1 on other errors.
)";
}

}  // namespace qoiw::cli
