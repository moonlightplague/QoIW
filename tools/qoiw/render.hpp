#pragma once

#include <optional>
#include <string>

#include "arguments.hpp"
#include "console.hpp"
#include "qoiw/report.hpp"

namespace qoiw::cli {

// Wall-clock seconds the compress command spent outside qoiw::compress, and in total.
struct CompressionRun {
    double read_input = 0, write_stream = 0, total = 0;
    bool written = false;
    // The -a check: decoding the finished container, then verifying it.
    std::optional<DecompressionTimes> check_decode;
    double check_verify = 0;
};

struct DecompressionRun {
    double read_stream = 0, write_output = 0, total = 0;
    DecompressionTimes decode;
};

struct VerificationRun {
    double read_inputs = 0, verify = 0, total = 0;
};

void print_compression(Console& console, const Arguments& args, const Report& report,
                       const std::optional<Verification>& post_decode, const CompressionRun& run);
std::string compression_json(const Report& report, const std::optional<Verification>& post_decode,
                             const CompressionRun& run);

void print_decompression(Console& console, const Arguments& args, const StreamInfo& info,
                         const DecompressionRun& run);
std::string decompression_json(const StreamInfo& info, const DecompressionRun& run);

void print_inspection(Console& console, const Arguments& args, const StreamInfo& info);

void print_verification(Console& console, const Arguments& args, const Verification& result,
                        const VerificationRun& run);

void print_cores(Console& console);

}  // namespace qoiw::cli
