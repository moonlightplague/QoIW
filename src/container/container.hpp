#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "qoiw/types.hpp"

namespace qoiw::container {

using qoiw::Bytes;

struct Field {
    std::string_view name;
    double bound = 0;
    std::optional<double> data_bound_abs;
    Bytes decode_params;
    std::uint64_t fingerprint = 0;
    Bytes core_stream;
    Bytes lossless{};  // Absent when no indices need correction.
};

// Strings and byte ranges borrow their storage. For read(), keep the source
// buffer alive and unmodified while using the returned view.
struct Stream {
    DType dtype = DType::Float32;
    std::vector<std::uint64_t> dims;
    std::string_view wrapper_version;
    std::string_view core_name;
    std::string_view core_version;
    std::string_view core_format_version;
    std::vector<Field> fields;
    std::string_view exact_condition;
};

class UnsupportedFormat : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

Stream read(Bytes input);
std::vector<std::uint8_t> write(const Stream& stream);

}  // namespace qoiw::container
