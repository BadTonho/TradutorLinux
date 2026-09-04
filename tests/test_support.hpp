#pragma once

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>

namespace tradutorlinux::test_support {

// Procura em /proc/self/maps a linha que cobre `address` e retorna as
// permissões (por exemplo, "r-xp"). Retorna std::nullopt se não houver.
inline std::optional<std::string> maps_permissions_for(const std::uintptr_t address) {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        const std::string::size_type space = line.find(' ');
        if (space == std::string::npos) {
            continue;
        }
        const std::string range = line.substr(0, space);
        const std::string::size_type dash = range.find('-');
        if (dash == std::string::npos) {
            continue;
        }
        const std::uintptr_t start = static_cast<std::uintptr_t>(
            std::stoull(range.substr(0, dash), nullptr, 16));
        const std::uintptr_t end = static_cast<std::uintptr_t>(
            std::stoull(range.substr(dash + 1), nullptr, 16));
        if (address >= start && address < end) {
            return line.substr(space + 1, 4);
        }
    }
    return std::nullopt;
}

}  // namespace tradutorlinux::test_support
