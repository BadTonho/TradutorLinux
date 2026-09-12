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
    if (buffer == nullptr || value.size() + 1U > capacity ||
        !mapped_guest_range(buffer, capacity * sizeof(*buffer), true)) {
        error = abi::kErrorInsufficientBuffer;
        return false;
    }
    std::copy(value.begin(), value.end(), buffer);
    buffer[value.size()] = 0;
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
