#pragma once

#include "console.hpp"
#include "qoiw/options.hpp"
#include "qoiw/request.hpp"

namespace qoiw::cli {

enum class Command { Compress, Decompress, Inspect, Verify, Cores };

struct Arguments {
    Command command = Command::Compress;
    Request request;
    Options options;
    std::vector<std::uint64_t> dims;
    std::optional<DType> dtype;
    std::string input, output, stream;
    bool check_decode = false, verbose = false, json = false;
    ColorMode color = ColorMode::Auto;
};

// An empty result means CLI11 handled a help request.
std::optional<Arguments> parse_arguments(int argc, char** argv);

}  // namespace qoiw::cli
