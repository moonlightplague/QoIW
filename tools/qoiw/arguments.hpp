#pragma once

#include <string_view>

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
    bool check_decode = false, verbose = false, json = false, help = false;
};

Arguments parse_arguments(int argc, char** argv);
bool is_help(std::string_view option);
std::string_view usage();

}  // namespace qoiw::cli
