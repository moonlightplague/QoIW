#pragma once

#include "container/container.hpp"
#include "qoiw/report.hpp"

namespace qoiw::pipeline {

StreamInfo describe_stream(const container::Stream& stream, std::size_t stream_bytes);

}  // namespace qoiw::pipeline
