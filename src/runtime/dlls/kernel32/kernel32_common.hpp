#pragma once

#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/loader/module_graph.hpp"
#include "../../core/runtime_state_common.hpp"
#include "../../core/runtime_process_state.hpp"
#include "../../core/runtime_thread_state.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>

namespace tradutorlinux {

using runtime::errno_to_win32;

inline void* const kInvalidHandleValue = reinterpret_cast<void*>(~static_cast<std::uintptr_t>(0));

inline void trace_process_console(const char* const event, const char* const operation,
                                  const std::string& detail) noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", operation},
        diagnostics::TraceField{"detail", detail},
        diagnostics::TraceField{"thread", std::to_string(g_current_thread_id)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace(event, fields, 4);
}

inline bool copy_wide_string(const std::u16string& value, std::uint16_t* buffer,
                             const std::size_t capacity, std::uint32_t& error) noexcept {
    if (buffer == nullptr || value.size() + 1U > capacity) {
        error = abi::kErrorInsufficientBuffer;
        return false;
    }
    if (!value.empty() &&
        runtime::write_guest_memory(buffer, value.data(), value.size() * sizeof(*buffer)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
        error = abi::kErrorInvalidParameter;
        return false;
    }
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(buffer);
    if (value.size() > (std::numeric_limits<std::uintptr_t>::max() - base) / sizeof(*buffer) ||
        !write_guest_value(
            reinterpret_cast<std::uint16_t*>(base + value.size() * sizeof(*buffer)),
            std::uint16_t{0})) {
        error = abi::kErrorInvalidParameter;
        return false;
    }
    error = abi::kErrorSuccess;
    return true;
}

[[nodiscard]] inline bool is_guest_executable_address(const std::uintptr_t address) noexcept {
    if (runtime::guest_context().module_graph != nullptr &&
        runtime::guest_context().module_graph->is_guest_executable_address(address)) {
        return true;
    }
    if (g_guest_image_base == nullptr || address == 0) {
        return false;
    }
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(g_guest_image_base);
    if (address < base || address - base >= g_guest_image_size) {
        return false;
    }
    std::ifstream maps{"/proc/self/maps"};
    std::string line;
    while (std::getline(maps, line)) {
        unsigned long long start = 0;
        unsigned long long end = 0;
        char permissions[5]{};
        if (std::sscanf(line.c_str(), "%llx-%llx %4s", &start, &end, permissions) != 3) {
            continue;
        }
        if (address >= start && address < end) {
            return permissions[2] == 'x';
        }
    }
    return false;
}

}  // namespace tradutorlinux
