#pragma once

#include "qoiw/core.hpp"

namespace qoiw::test {

// Available only in builds of the test support library.
const Core& mock_core(std::string_view name);

}  // namespace qoiw::test
