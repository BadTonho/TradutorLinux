#pragma once

#include "tradutorlinux/runtime/winapi.hpp"
#include "../../core/runtime_context.hpp"
#include "tradutorlinux/loader/import_resolver.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/module_graph.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/runtime/error_map.hpp"
#include "tradutorlinux/runtime/environment.hpp"
#include "tradutorlinux/runtime/msvcrt.hpp"
#include "tradutorlinux/runtime/ntdll.hpp"
#include "tradutorlinux/runtime/security.hpp"
#include "tradutorlinux/runtime/unwind.hpp"
#include "tradutorlinux/util/basics.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <malloc.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tradutorlinux {

using runtime::errno_to_win32;

// Definições auxiliares compartilhadas
constexpr std::uint32_t kFileAttributeReadOnly = 0x00000001;
constexpr std::uint32_t kFileAttributeDirectory = 0x00000010;
constexpr std::uint32_t kFileAttributeArchive = 0x00000020;

constexpr std::uint32_t kFileBegin = 0;
constexpr std::uint32_t kFileCurrent = 1;
constexpr std::uint32_t kFileEnd = 2;

inline void* const kInvalidHandleValue = reinterpret_cast<void*>(~static_cast<std::uintptr_t>(0));

struct GuestFileTime {
    std::uint32_t low{};
    std::uint32_t high{};
};
static_assert(sizeof(GuestFileTime) == 8);

struct GuestFileAttributeData {
    std::uint32_t attributes{};
    GuestFileTime creation{};
    GuestFileTime last_access{};
    GuestFileTime last_write{};
    std::uint32_t size_high{};
    std::uint32_t size_low{};
};
static_assert(sizeof(GuestFileAttributeData) == 36);

struct GuestByHandleFileInformation {
    std::uint32_t attributes{};
    GuestFileTime creation{};
    GuestFileTime last_access{};
    GuestFileTime last_write{};
    std::uint32_t volume_serial_number{};
    std::uint32_t size_high{};
    std::uint32_t size_low{};
    std::uint32_t number_of_links{};
    std::uint32_t file_index_high{};
    std::uint32_t file_index_low{};
};
static_assert(sizeof(GuestByHandleFileInformation) == 52);

inline FileMappingSlot* find_file_mapping_slot_locked(const void* handle) noexcept {
    if (handle == nullptr) {
        return nullptr;
    }
    const auto addr = std::bit_cast<std::uintptr_t>(handle);
    const auto begin = std::bit_cast<std::uintptr_t>(g_mappings.data());
    const auto end = begin + g_mappings.size() * sizeof(FileMappingSlot);
    if (addr < begin || addr >= end || (addr - begin) % sizeof(FileMappingSlot) != 0) {
        return nullptr;
    }
    auto* slot = static_cast<FileMappingSlot*>(const_cast<void*>(handle));
    return slot->used ? slot : nullptr;
}

inline std::int64_t unix_time_to_filetime(const std::time_t seconds) noexcept {
    constexpr std::int64_t kEpochDifference = 11644473600LL;
    constexpr std::int64_t kTicksPerSecond = 10000000LL;
    return (static_cast<std::int64_t>(seconds) + kEpochDifference) * kTicksPerSecond;
}

inline std::time_t filetime_to_unix_time(const GuestFileTime& ft) noexcept {
    constexpr std::int64_t kEpochDifference = 11644473600LL;
    constexpr std::int64_t kTicksPerSecond = 10000000LL;
    const std::uint64_t ticks = (static_cast<std::uint64_t>(ft.high) << 32) | ft.low;
    return static_cast<std::time_t>(ticks / kTicksPerSecond - kEpochDifference);
}

inline void filetime_from_unix(const std::time_t source, GuestFileTime& target) noexcept {
    const std::int64_t ticks = unix_time_to_filetime(source);
    const auto raw = static_cast<std::uint64_t>(std::max<std::int64_t>(ticks, 0));
    target.low = static_cast<std::uint32_t>(raw & 0xFFFFFFFFU);
    target.high = static_cast<std::uint32_t>(raw >> 32U);
}

inline void trace_filesystem(const char* const operation, const char* const status,
                      const std::string& detail) noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", operation},
        diagnostics::TraceField{"status", status},
        diagnostics::TraceField{"detail", detail},
        diagnostics::TraceField{"scope", "prefix"},
    };
    runtime_trace("filesystem", fields, 4);
}

inline bool ascii_case_equal(const char left, const char right) noexcept {
    return std::tolower(static_cast<unsigned char>(left)) ==
           std::tolower(static_cast<unsigned char>(right));
}

inline bool win32_wildcard_match(std::string_view pattern, const std::string_view name) noexcept {
    if (pattern == "*.*") {
        pattern = "*";
    }
    std::size_t pattern_pos = 0;
    std::size_t name_pos = 0;
    std::size_t star_pos = std::string_view::npos;
    std::size_t star_name_pos = 0;
    while (name_pos < name.size()) {
        if (pattern_pos < pattern.size() &&
            (pattern[pattern_pos] == '?' ||
             ascii_case_equal(pattern[pattern_pos], name[name_pos]))) {
            ++pattern_pos;
            ++name_pos;
        } else if (pattern_pos < pattern.size() && pattern[pattern_pos] == '*') {
            star_pos = pattern_pos++;
            star_name_pos = name_pos;
        } else if (star_pos != std::string_view::npos) {
            pattern_pos = star_pos + 1;
            name_pos = ++star_name_pos;
        } else {
            return false;
        }
    }
    while (pattern_pos < pattern.size() && pattern[pattern_pos] == '*') {
        ++pattern_pos;
    }
    return pattern_pos == pattern.size();
}

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
