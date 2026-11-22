#pragma once

#include "qoiw/core.hpp"
#include "qoiw/qoiw.hpp"

namespace qoiw::pipeline {

// Supply a core directly for conformance tests. The optional byte snapshot
// holds the corrected reconstruction in native storage order on success.
std::vector<std::uint8_t> compress_field(const ArrayView& input, const Request& request, const Options& options,
                                        Report* report, const Core* supplied_core = nullptr,
                                        std::vector<std::uint8_t>* verified = nullptr);

}  // namespace qoiw::pipeline
