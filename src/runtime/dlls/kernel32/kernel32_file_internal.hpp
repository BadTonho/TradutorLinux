#pragma once

#include "kernel32_common.hpp"

#include "tradutorlinux/runtime/security.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

#include <sys/statvfs.h>

namespace tradutorlinux {

constexpr std::uint32_t kFileAttributeReadOnly = 0x00000001;
constexpr std::uint32_t kFileAttributeDirectory = 0x00000010;
constexpr std::uint32_t kFileAttributeArchive = 0x00000020;

constexpr std::uint32_t kFileBegin = 0;
constexpr std::uint32_t kFileCurrent = 1;
constexpr std::uint32_t kFileEnd = 2;

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

}  // namespace tradutorlinux
