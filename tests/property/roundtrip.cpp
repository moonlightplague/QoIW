#include <cstring>
#include <iomanip>
#include <iostream>

#include "common/shape.hpp"
#include "core/adapters/mock.hpp"
#include "files.hpp"
#include "pipeline/compress_field.hpp"
#include "pipeline/decode_field.hpp"

namespace {
using namespace qoiw;

struct Case {
    DType dtype;
    std::vector<std::uint64_t> dims;
    Request request;
    Options options;
};

Tol read_tolerance(std::istream& input) {
    std::string mode;
    double value;
    if (!(input >> mode >> value)) throw std::invalid_argument("invalid test tolerance record");
    if (mode == "ABS") return Tol::abs(value);
    if (mode == "REL") return Tol::rel(value);
    if (mode == "NONE") return Tol::none();
    throw std::invalid_argument("invalid test tolerance mode");
}

Case read_case(std::istream& input) {
    Case test;
    std::size_t rank;
    std::string dtype, bound;
    if (!(input >> dtype >> test.options.core >> test.options.threads >> bound >> rank) || rank < 1 || rank > 4)
        throw std::invalid_argument("invalid test case header");
    if (dtype == "f32") test.dtype = DType::Float32;
    else if (dtype == "f64") test.dtype = DType::Float64;
    else throw std::invalid_argument("invalid test storage type");
    if (bound != "auto") test.options.fixed_bound = std::stod(bound);
    test.dims.resize(rank);
    for (auto& dim : test.dims)
        if (!(input >> dim)) throw std::invalid_argument("invalid test shape");
    test.request.data_bound = read_tolerance(input);
    std::string exact;
    std::size_t targets;
    if (!(input >> std::quoted(exact) >> targets) || targets > UINT8_MAX)
        throw std::invalid_argument("invalid test request");
    if (!exact.empty()) test.request.exact = std::move(exact);
    for (std::size_t i = 0; i < targets; ++i) {
        std::string expression;
        if (!(input >> std::quoted(expression))) throw std::invalid_argument("missing test expression");
        test.request.targets.push_back({std::move(expression), read_tolerance(input)});
    }
    if (test.options.core == "mock:noise") test.options.core_options["factor"] = "3";
    return test;
}

template <class T>
void round_trip(const Case& test, const char* original_path, const char* stream_path, const char* decoded_path) {
    const auto bytes = common::array_bytes(test.dtype, test.dims);
    if (!bytes) throw std::invalid_argument("invalid test field shape");
    const auto original = cli::read_raw<T>(original_path, bytes);
    const ArrayView input(original.data(), test.dims);
    const bool mock = test.options.core.rfind("mock:", 0) == 0;
    const Core* core = mock ? &qoiw::test::mock_core(test.options.core) : nullptr;
    auto serial = test.options;
    serial.threads = 1;
    Report report;
    std::vector<std::uint8_t> verified, parallel_verified;
    const auto encoded = pipeline::compress_field(input, test.request, serial, &report, core, &verified);
    const auto parallel = pipeline::compress_field(input, test.request, test.options, nullptr, core, &parallel_verified);
    if (encoded != parallel || verified != parallel_verified)
        throw std::runtime_error("encoding depends on the requested thread count");

    cli::write_file(stream_path, encoded.data(), encoded.size());
    const auto persisted = cli::read_container(stream_path);
    const auto parsed = container::read(persisted);
    std::vector<T> decoded(original.size());
    for (int threads : {1, test.options.threads}) {
        // Poison the output before each decode so a skipped write cannot
        // retain a value from an earlier successful reconstruction.
        std::memset(decoded.data(), 0xA5, bytes);
        if (core) {
            const auto restored = pipeline::decode_field(parsed, 0, *core, threads, decoded.data(), bytes);
            if (restored != report.restored) throw std::runtime_error("lossless count changed during decode");
        } else {
            decompress(persisted, decoded.data(), decoded.size(), threads);
        }
        if (verified.size() != bytes || std::memcmp(verified.data(), decoded.data(), bytes) != 0)
            throw std::runtime_error("decoded bits differ from the encoder's checked reconstruction");
    }
    cli::convert_byte_order(decoded);
    cli::write_file(decoded_path, decoded.data(), bytes);
    std::cout << report.restored << '\n' << inspect(persisted).to_json() << '\n';
}
}  // namespace

int main(int argc, char** argv) try {
    if (argc != 4) throw std::invalid_argument("expected original, stream and decoded paths");
    const auto test = read_case(std::cin);
    if (test.dtype == qoiw::DType::Float32) round_trip<float>(test, argv[1], argv[2], argv[3]);
    else round_trip<double>(test, argv[1], argv[2], argv[3]);
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
