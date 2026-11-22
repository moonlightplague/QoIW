#include "qoiw/core.hpp"

#include <map>
#include <stdexcept>

#include "builtin_cores.hpp"

namespace qoiw {
namespace {

const auto& core_registry() {
    static const auto registry = [] {
        std::map<std::string_view, const Core*> cores;
        for (const auto factory : detail::builtin_cores) {
            const auto& core = factory();
            if (!cores.emplace(core.name(), &core).second)
                throw std::logic_error("duplicate core name: " + std::string(core.name()));
        }
        return cores;
    }();
    return registry;
}

}  // namespace

const Core& get_core(std::string_view name) {
    const auto& cores = core_registry();
    const auto entry = cores.find(name);
    if (entry != cores.end()) return *entry->second;
    throw std::invalid_argument("unavailable core: " + std::string(name));
}

std::vector<std::string> available_cores() {
    const auto& cores = core_registry();
    std::vector<std::string> names;
    names.reserve(cores.size());
    for (const auto& entry : cores) names.emplace_back(entry.first);
    return names;
}

}  // namespace qoiw
