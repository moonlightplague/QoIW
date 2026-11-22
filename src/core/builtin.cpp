#include "qoiw/core.hpp"

#include <stdexcept>

#ifdef QOIW_CORE_SZ3
#include "core/adapters/sz3.hpp"
#endif

namespace qoiw {

const Core& get_core(std::string_view name) {
#ifdef QOIW_CORE_SZ3
    if (name == "sz3") return sz3_core();
#endif
    throw std::invalid_argument("unavailable core: " + std::string(name));
}

std::vector<std::string> available_cores() {
#ifdef QOIW_CORE_SZ3
    return {"sz3"};
#else
    return {};
#endif
}

}  // namespace qoiw
