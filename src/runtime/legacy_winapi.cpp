#include "tradutorlinux/runtime/winapi.hpp"
#include "runtime_context.hpp"

#include "tradutorlinux/gui/x11.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/runtime/error_map.hpp"
#include "tradutorlinux/runtime/ntdll.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cerrno>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace tradutorlinux {

using runtime::errno_to_win32;

namespace {

struct [[maybe_unused]] MapsRegion {
    std::uintptr_t start{};
    std::uintptr_t end{};
    char permissions[5]{};
    bool has_path{false};
};

[[maybe_unused]] bool find_maps_region(const void* address, MapsRegion& result) noexcept {
    const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(address);
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        const std::size_t dash = line.find('-');
        const std::size_t separator = line.find(' ');
        if (dash == std::string::npos || separator == std::string::npos || dash > separator) {
            continue;
        }
        std::uintptr_t start = 0;
        std::uintptr_t end = 0;
        const auto start_result = std::from_chars(line.data(), line.data() + dash, start, 16);
        const auto end_result = std::from_chars(line.data() + dash + 1, line.data() + separator, end, 16);
        if (start_result.ec != std::errc{} || end_result.ec != std::errc{} ||
            target < start || target >= end) {
            continue;
        }
        const std::size_t permissions_offset = separator + 1U;
        if (line.size() < permissions_offset + 4U) {
            return false;
        }
        result.start = start;
        result.end = end;
        std::memcpy(result.permissions, line.data() + permissions_offset, 4U);
        result.permissions[4] = '\0';
        result.has_path = line.find('/', permissions_offset + 4U) != std::string::npos;
        return true;
    }
    return false;
}

[[maybe_unused]] std::uint32_t win32_protection(const char permissions[4]) noexcept {
    const bool readable = permissions[0] == 'r';
    const bool writable = permissions[1] == 'w';
    const bool executable = permissions[2] == 'x';
    if (!readable && !writable && !executable) {
        return abi::kPageNoAccess;
    }
    if (executable) {
        return writable ? abi::kPageExecuteReadWrite : abi::kPageExecuteRead;
    }
    return writable ? abi::kPageReadWrite : abi::kPageReadOnly;
}

[[maybe_unused]] int host_protection(const std::uint32_t protection) noexcept {
    switch (protection & 0xFFU) {
        case abi::kPageNoAccess:
            return PROT_NONE;
        case abi::kPageReadOnly:
            return PROT_READ;
        case abi::kPageReadWrite:
        case abi::kPageWriteCopy:
            return PROT_READ | PROT_WRITE;
        case abi::kPageExecute:
            return PROT_EXEC;
        case abi::kPageExecuteRead:
            return PROT_READ | PROT_EXEC;
        case abi::kPageExecuteReadWrite:
        case abi::kPageExecuteWriteCopy:
            return PROT_READ | PROT_WRITE | PROT_EXEC;
        default:
            return -1;
    }
}

bool normalize_wide_path(const std::uint16_t* path, std::string& result) noexcept {
    char normalized[4096]{};
    if (!normalized_wide_path(path, normalized)) {
        return false;
    }
    result = normalized;
    return true;
}

std::int64_t filetime_ticks(const timespec& value) noexcept {
    constexpr std::int64_t kEpochDifference = 11644473600LL;
    return (static_cast<std::int64_t>(value.tv_sec) + kEpochDifference) * 10000000LL +
           static_cast<std::int64_t>(value.tv_nsec) / 100LL;
}

struct LegacyFileTime {
    std::uint32_t low{};
    std::uint32_t high{};
};
static_assert(sizeof(LegacyFileTime) == 8);

void write_filetime(const timespec& source, LegacyFileTime& target) noexcept {
    const auto ticks = static_cast<std::uint64_t>(std::max<std::int64_t>(filetime_ticks(source), 0));
    target.low = static_cast<std::uint32_t>(ticks & 0xFFFFFFFFU);
    target.high = static_cast<std::uint32_t>(ticks >> 32U);
}

bool filetime_to_timespec(const LegacyFileTime& value, timespec& result) noexcept {
    constexpr std::uint64_t kEpochDifference = 11644473600ULL;
    constexpr std::uint64_t kTicksPerSecond = 10000000ULL;
    const std::uint64_t ticks = (static_cast<std::uint64_t>(value.high) << 32U) | value.low;
    if (ticks < kEpochDifference * kTicksPerSecond) {
        return false;
    }
    const std::uint64_t unix_ticks = ticks - kEpochDifference * kTicksPerSecond;
    result.tv_sec = static_cast<time_t>(unix_ticks / kTicksPerSecond);
    result.tv_nsec = static_cast<long>((unix_ticks % kTicksPerSecond) * 100ULL);
    return true;
}

struct LegacyFileAttributeData {
    std::uint32_t attributes{};
    LegacyFileTime creation{};
    LegacyFileTime last_access{};
    LegacyFileTime last_write{};
    std::uint32_t size_high{};
    std::uint32_t size_low{};
};
static_assert(sizeof(LegacyFileAttributeData) == 36);

struct LegacyByHandleFileInformation {
    std::uint32_t attributes{};
    LegacyFileTime creation{};
    LegacyFileTime last_access{};
    LegacyFileTime last_write{};
    std::uint32_t volume_serial_number{};
    std::uint32_t size_high{};
    std::uint32_t size_low{};
    std::uint32_t number_of_links{};
    std::uint32_t file_index_high{};
    std::uint32_t file_index_low{};
};
static_assert(sizeof(LegacyByHandleFileInformation) == 52);

struct LegacyBasicFileInformation {
    std::int64_t creation_time{};
    std::int64_t last_access_time{};
    std::int64_t last_write_time{};
    std::int64_t change_time{};
    std::uint32_t attributes{};
    std::uint32_t reserved{};
};
static_assert(sizeof(LegacyBasicFileInformation) == 40);

struct LegacyFindDataW {
    std::uint32_t attributes{};
    std::uint32_t creation_low{};
    std::uint32_t creation_high{};
    std::uint32_t access_low{};
    std::uint32_t access_high{};
    std::uint32_t write_low{};
    std::uint32_t write_high{};
    std::uint32_t size_high{};
    std::uint32_t size_low{};
    std::uint32_t reserved0{};
    std::uint32_t reserved1{};
    std::uint16_t file_name[260]{};
    std::uint16_t alternate_file_name[14]{};
};
static_assert(sizeof(LegacyFindDataW) == 592);

void convert_find_data(const Win32FindDataA& source, LegacyFindDataW& target) noexcept {
    target = {};
    target.attributes = source.dw_file_attributes;
    target.creation_low = source.ft_creation_time_lo;
    target.creation_high = source.ft_creation_time_hi;
    target.access_low = source.ft_last_access_time_lo;
    target.access_high = source.ft_last_access_time_hi;
    target.write_low = source.ft_last_write_time_lo;
    target.write_high = source.ft_last_write_time_hi;
    target.size_high = source.n_file_size_high;
    target.size_low = source.n_file_size_low;
    target.reserved0 = source.dw_reserved0;
    target.reserved1 = source.dw_reserved1;
    const std::u16string name = util::utf8_to_wide(source.c_file_name);
    const std::size_t name_length = std::min(name.size(), std::size(target.file_name) - 1U);
    std::copy(name.begin(), name.begin() + static_cast<std::ptrdiff_t>(name_length), target.file_name);
    target.file_name[name_length] = 0;
}

bool copy_wide_string(const std::u16string& value, std::uint16_t* buffer,
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

std::u16string final_windows_path(const std::string& path) {
    return util::utf8_to_wide(
        prefix::to_windows_path(std::filesystem::path(path), guest_prefix_root()));
}

[[nodiscard]] bool supported_locale_lcid(const std::uint32_t locale) noexcept {
    return locale == abi::kLocaleEnglishUnitedStates || locale == abi::kLocaleUserDefault ||
           locale == abi::kLocaleSystemDefault;
}

[[nodiscard]] bool supported_code_page(const std::uint32_t code_page) noexcept {
    return code_page == abi::kCpAcp || code_page == abi::kCp1252 ||
           code_page == abi::kCpOem || code_page == abi::kCp437 ||
           code_page == abi::kCpUtf8;
}

[[nodiscard]] bool guest_executable_callback(const std::uintptr_t address) noexcept {
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

void trace_locale_extension(const char* const operation, const std::string& detail) noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", operation},
        diagnostics::TraceField{"locale", "en-US"},
        diagnostics::TraceField{"detail", detail},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("locale", fields, 4);
}

void trace_filesystem(const char* const operation, const char* const status,
                      const std::string& detail) noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", operation},
        diagnostics::TraceField{"status", status},
        diagnostics::TraceField{"detail", detail},
        diagnostics::TraceField{"scope", "prefix"},
    };
    runtime_trace("filesystem", fields, 4);
}

[[nodiscard]] bool valid_system_time(const abi::GuestSystemTime& value) noexcept {
    if (value.year < 1601U || value.month < 1U || value.month > 12U || value.day < 1U ||
        value.hour > 23U || value.minute > 59U || value.second > 59U ||
        value.milliseconds > 999U || value.day_of_week > 6U) {
        return false;
    }
    static constexpr std::uint16_t kDaysByMonth[]{31, 28, 31, 30, 31, 30,
                                                   31, 31, 30, 31, 30, 31};
    std::uint16_t max_day = kDaysByMonth[value.month - 1U];
    const bool leap = value.year % 4U == 0U &&
                      (value.year % 100U != 0U || value.year % 400U == 0U);
    if (value.month == 2U && leap) {
        ++max_day;
    }
    return value.day <= max_day;
}

void append_decimal(std::u16string& target, const std::uint16_t value,
                    const std::size_t minimum_digits) {
    char narrow[8]{};
    const auto converted = std::to_chars(narrow, narrow + sizeof(narrow), value);
    const std::size_t digits = static_cast<std::size_t>(converted.ptr - narrow);
    for (std::size_t index = digits; index < minimum_digits; ++index) {
        target.push_back(u'0');
    }
    for (std::size_t index = 0; index < digits; ++index) {
        target.push_back(static_cast<char16_t>(narrow[index]));
    }
}

[[nodiscard]] std::uint16_t ctype1(const std::uint16_t unit) noexcept {
    std::uint16_t result = 0;
    const bool upper = (unit >= 'A' && unit <= 'Z') ||
                       (unit >= 0x00C0U && unit <= 0x00D6U && unit != 0x00D7U) ||
                       (unit >= 0x00D8U && unit <= 0x00DEU);
    const bool lower = (unit >= 'a' && unit <= 'z') ||
                       (unit >= 0x00E0U && unit <= 0x00F6U && unit != 0x00F7U) ||
                       (unit >= 0x00F8U && unit <= 0x00FFU);
    if (upper) result |= abi::kC1Upper | abi::kC1Alpha;
    if (lower) result |= abi::kC1Lower | abi::kC1Alpha;
    if (unit >= '0' && unit <= '9') result |= abi::kC1Digit;
    if ((unit >= '0' && unit <= '9') || (unit >= 'A' && unit <= 'F') ||
        (unit >= 'a' && unit <= 'f')) result |= abi::kC1Xdigit;
    if (unit == ' ') result |= abi::kC1Space | abi::kC1Blank;
    if (unit == '\t' || unit == '\n' || unit == '\r' || unit == '\v' || unit == '\f') {
        result |= abi::kC1Space;
    }
    if (unit < 0x20U || unit == 0x7FU) result |= abi::kC1Cntrl;
    if (result == 0 && unit >= 0x21U && unit <= 0x00BFU) result |= abi::kC1Punct;
    return result;
}

int write_locale_value(const std::u16string& value, std::uint16_t* const data,
                       const int data_count) noexcept {
    const int needed = static_cast<int>(value.size() + 1U);
    if (data == nullptr || data_count == 0) {
        set_last_error(abi::kErrorSuccess);
        return needed;
    }
    if (data_count < needed ||
        !mapped_guest_range(data, static_cast<std::size_t>(data_count) * sizeof(*data), true)) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    std::copy(value.begin(), value.end(), data);
    data[value.size()] = 0;
    set_last_error(abi::kErrorSuccess);
    return needed;
}

[[nodiscard]] std::optional<std::u16string> locale_string(const std::uint32_t locale_type) {
    switch (locale_type & ~abi::kLocaleReturnNumber) {
        case abi::kLocaleILanguage: return u"0409";
        case abi::kLocaleSLanguage: return u"English (United States)";
        case abi::kLocaleSEngLanguage: return u"English";
        case abi::kLocaleSISO639LangName: return u"en";
        case abi::kLocaleSCountry: return u"United States";
        case abi::kLocaleSEngCountry: return u"United States";
        case abi::kLocaleSISO3166CtryName: return u"US";
        case abi::kLocaleSDecimal: return u".";
        case abi::kLocaleSThousand: return u",";
        case abi::kLocaleSCurrency: return u"$";
        case abi::kLocaleS1159: return u"AM";
        case abi::kLocaleS2359: return u"PM";
        case abi::kLocaleIDefaultCodePage: return u"1252";
        default: return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::uint32_t> locale_number(const std::uint32_t locale_type) noexcept {
    switch (locale_type & ~abi::kLocaleReturnNumber) {
        case abi::kLocaleILanguage: return abi::kLocaleEnglishUnitedStates;
        case abi::kLocaleIDefaultCodePage: return abi::kCp1252;
        default: return std::nullopt;
    }
}

[[nodiscard]] bool locale_name_is_en_us(const std::uint16_t* const name) noexcept {
    if (name == nullptr || !mapped_guest_wstring(name)) {
        return false;
    }
    static constexpr std::uint16_t kEnUs[] = {'e', 'n', '-', 'U', 'S', 0};
    for (std::size_t index = 0; index < std::size(kEnUs); ++index) {
        const std::uint16_t left = name[index];
        const std::uint16_t right = kEnUs[index];
        const std::uint16_t normalized =
            left >= 'A' && left <= 'Z' ? static_cast<std::uint16_t>(left + ('a' - 'A')) : left;
        const std::uint16_t expected =
            right >= 'A' && right <= 'Z' ? static_cast<std::uint16_t>(right + ('a' - 'A')) : right;
        if (normalized != expected) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::uint16_t locale_case_map(const std::uint16_t unit,
                                             const std::uint32_t flags) noexcept {
    if ((flags & abi::kLcmapsUppercase) != 0U) {
        if (unit >= 'a' && unit <= 'z') return static_cast<std::uint16_t>(unit - ('a' - 'A'));
        if (unit >= 0x00E0U && unit <= 0x00F6U && unit != 0x00F7U) return static_cast<std::uint16_t>(unit - 0x20U);
        if (unit >= 0x00F8U && unit <= 0x00FEU) return static_cast<std::uint16_t>(unit - 0x20U);
        return unit;
    }
    if (unit >= 'A' && unit <= 'Z') return static_cast<std::uint16_t>(unit + ('a' - 'A'));
    if (unit >= 0x00C0U && unit <= 0x00D6U && unit != 0x00D7U) return static_cast<std::uint16_t>(unit + 0x20U);
    if (unit >= 0x00D8U && unit <= 0x00DEU) return static_cast<std::uint16_t>(unit + 0x20U);
    return unit;
}

int map_locale_string(const std::uint32_t flags, const std::uint16_t* const source,
                      const int source_count, std::uint16_t* const destination,
                      const int destination_count) noexcept {
    if ((flags != abi::kLcmapsUppercase && flags != abi::kLcmapsLowercase) || source == nullptr ||
        source_count == 0 || source_count < -1 ||
        (destination_count != 0 && destination == nullptr)) {
        set_last_error(flags == abi::kLcmapsUppercase || flags == abi::kLcmapsLowercase
                           ? abi::kErrorInvalidParameter : abi::kErrorInvalidFlags);
        return 0;
    }
    std::size_t units = 0;
    if (source_count == -1) {
        if (!mapped_guest_wstring(source)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        do {
            ++units;
        } while (source[units - 1U] != 0);
    } else {
        units = static_cast<std::size_t>(source_count);
        if (!mapped_guest_range(source, units * sizeof(*source), false)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    if (destination == nullptr || destination_count == 0) {
        set_last_error(abi::kErrorSuccess);
        return static_cast<int>(units);
    }
    if (destination_count < 0 || static_cast<std::size_t>(destination_count) < units ||
        !mapped_guest_range(destination, units * sizeof(*destination), true)) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    for (std::size_t index = 0; index < units; ++index) {
        destination[index] = locale_case_map(source[index], flags);
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(units);
}

}  // namespace

TL_MSABI int tl_GetConsoleMode(const void* handle, std::uint32_t* mode) noexcept {
    if (mode == nullptr || !mapped_guest_range(mode, sizeof(*mode), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const int fd = handle_fd(handle);
    if (fd < 0 || ::isatty(fd) == 0) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    *mode = 0x3U;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetConsoleMode(const void* handle, std::uint32_t /*mode*/) noexcept {
    if (handle_fd(handle) < 0) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_IsDBCSLeadByteEx(std::uint32_t /*code_page*/, std::uint8_t /*test_char*/) noexcept {
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI int tl_VirtualProtect(void* address, std::uintptr_t size,
                               std::uint32_t new_protection,
                               std::uint32_t* old_protection) noexcept {
    if (old_protection == nullptr ||
        !mapped_guest_range(old_protection, sizeof(*old_protection), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::size_t region = static_cast<std::size_t>(size);
    std::uint32_t old = 0;
    const ntdll::NtStatus st = ntdll::NtProtectVirtualMemory(address, &region, new_protection, &old);
    if (st != ntdll::NtStatus::Success) {
        const std::uint32_t err = ntdll::NtStatusToDosError(st);
        // Wine mapeia InvalidParameter/AccessDenied para ERROR_INVALID_ADDRESS quando fora da VAD.
        if (st == ntdll::NtStatus::InvalidParameter || st == ntdll::NtStatus::AccessDenied) {
            // Tenta distinguir: se não achou região, retorna InvalidAddress como antes.
            set_last_error(abi::kErrorInvalidAddress);
        } else {
            set_last_error(err);
        }
        return 0;
    }
    *old_protection = old;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uintptr_t tl_VirtualQuery(const void* address, void* memory_information,
                                        std::uintptr_t length) noexcept {
    if (memory_information == nullptr || length < sizeof(abi::GuestMemoryBasicInformation) ||
        !mapped_guest_range(memory_information, sizeof(abi::GuestMemoryBasicInformation), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    ntdll::NtMemoryInformation info{};
    const ntdll::NtStatus st = ntdll::NtQueryVirtualMemory(address, &info);
    if (st != ntdll::NtStatus::Success) {
        set_last_error(abi::kErrorInvalidAddress);
        return 0;
    }
    auto* out = static_cast<abi::GuestMemoryBasicInformation*>(memory_information);
    *out = {};
    out->base_address = info.BaseAddress;
    out->allocation_base = info.AllocationBase;
    out->allocation_protect = info.AllocationProtect;
    out->region_size = info.RegionSize;
    out->state = info.State;
    out->protect = info.Protect;
    out->type = info.Type;
    set_last_error(abi::kErrorSuccess);
    return sizeof(*out);
}

TL_MSABI int tl_MultiByteToWideChar(std::uint32_t code_page, std::uint32_t flags,
                                    const char* mb_str, int mb_count,
                                    std::uint16_t* wide_str, int wide_count) noexcept {
    const bool supported_page = code_page == abi::kCpAcp || code_page == abi::kCp1252 ||
                                code_page == abi::kCpOem || code_page == abi::kCp437 ||
                                code_page == abi::kCpUtf8;
    if (mb_str == nullptr || mb_count == 0 || mb_count < -1 || !supported_page ||
        (flags & ~(abi::kMbPrecomposed | abi::kMbErrInvalidChars)) != 0U ||
        (wide_count != 0 && wide_str == nullptr)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const bool null_terminated = mb_count == -1;
    if (null_terminated && !mapped_guest_cstring(mb_str)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::size_t byte_count = null_terminated ? std::strlen(mb_str) : static_cast<std::size_t>(mb_count);
    if (!mapped_guest_range(mb_str, byte_count, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(mb_str);
    std::size_t index = 0;
    std::size_t needed = null_terminated ? 1U : 0U;
    while (index < byte_count) {
        std::uint32_t codepoint = decode_multibyte(code_page, bytes, byte_count, index);
        if (codepoint > 0x10FFFFU) {
            if ((flags & abi::kMbErrInvalidChars) != 0U) {
                set_last_error(abi::kErrorNoUnicodeTranslation);
                return 0;
            }
            codepoint = '?';
        }
        std::uint16_t units[2]{};
        needed += util::utf16_units_for(codepoint, units);
    }
    if (needed > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (wide_str == nullptr) {
        set_last_error(abi::kErrorSuccess);
        return static_cast<int>(needed);
    }
    if (wide_count < 0 || static_cast<std::size_t>(wide_count) < needed ||
        !mapped_guest_range(wide_str, needed * sizeof(*wide_str), true)) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    index = 0;
    std::size_t written = 0;
    while (index < byte_count) {
        std::uint32_t codepoint = decode_multibyte(code_page, bytes, byte_count, index);
        if (codepoint > 0x10FFFFU) {
            codepoint = '?';
        }
        std::uint16_t units[2]{};
        const std::size_t count = util::utf16_units_for(codepoint, units);
        std::copy(units, units + static_cast<std::ptrdiff_t>(count), wide_str + written);
        written += count;
    }
    if (null_terminated) {
        wide_str[written++] = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(written);
}

TL_MSABI int tl_WideCharToMultiByte(std::uint32_t code_page, std::uint32_t flags,
                                    const std::uint16_t* wide_str, int wide_count,
                                    char* mb_str, int mb_count, const char* default_char,
                                    int* used_default_char) noexcept {
    const bool supported_page = code_page == abi::kCpAcp || code_page == abi::kCp1252 ||
                                code_page == abi::kCpOem || code_page == abi::kCp437 ||
                                code_page == abi::kCpUtf8;
    if (wide_str == nullptr || wide_count == 0 || wide_count < -1 || !supported_page ||
        (flags & ~(abi::kWcCompositeCheck | abi::kWcNoBestFitChars)) != 0U ||
        (mb_count != 0 && mb_str == nullptr) ||
        (default_char != nullptr && !mapped_guest_range(default_char, sizeof(*default_char), false)) ||
        (used_default_char != nullptr &&
         !mapped_guest_range(used_default_char, sizeof(*used_default_char), true))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const bool null_terminated = wide_count == -1;
    std::size_t unit_count = 0;
    if (null_terminated) {
        if (!mapped_guest_wstring(wide_str)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        while (wide_str[unit_count] != 0) {
            ++unit_count;
        }
    } else {
        unit_count = static_cast<std::size_t>(wide_count);
        if (!mapped_guest_range(wide_str, unit_count * sizeof(*wide_str), false)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    const bool utf8 = code_page == abi::kCpUtf8;
    const bool cp437 = code_page == abi::kCpOem || code_page == abi::kCp437;
    std::size_t index = 0;
    std::size_t needed = null_terminated ? 1U : 0U;
    while (index < unit_count) {
        const std::uint32_t codepoint = util::decode_utf16(wide_str, unit_count, index);
        if (utf8) {
            char bytes[4]{};
            needed += util::utf8_bytes_for(codepoint > 0x10FFFFU ? '?' : codepoint, bytes);
        } else {
            needed += 1U;
        }
    }
    if (needed > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (mb_str == nullptr) {
        set_last_error(abi::kErrorSuccess);
        return static_cast<int>(needed);
    }
    if (mb_count < 0 || static_cast<std::size_t>(mb_count) < needed ||
        !mapped_guest_range(mb_str, needed, true)) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    const char fallback = default_char != nullptr ? *default_char : '?';
    bool used_default = false;
    index = 0;
    std::size_t written = 0;
    while (index < unit_count) {
        std::uint32_t codepoint = util::decode_utf16(wide_str, unit_count, index);
        if (codepoint > 0x10FFFFU) {
            codepoint = '?';
        }
        if (utf8) {
            char bytes[4]{};
            const std::size_t count = util::utf8_bytes_for(codepoint, bytes);
            std::copy(bytes, bytes + static_cast<std::ptrdiff_t>(count), mb_str + written);
            written += count;
        } else {
            std::uint8_t byte = 0;
            if ((cp437 ? util::unicode_to_cp437(codepoint, byte)
                       : util::unicode_to_cp1252(codepoint, byte))) {
                mb_str[written++] = static_cast<char>(byte);
            } else {
                mb_str[written++] = fallback;
                used_default = true;
            }
        }
    }
    if (null_terminated) {
        mb_str[written++] = '\0';
    }
    if (used_default_char != nullptr) {
        *used_default_char = used_default ? 1 : 0;
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(written);
}

TL_MSABI std::uint32_t tl_GetACP() noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", "acp"},
        diagnostics::TraceField{"code-page", "1252"},
        diagnostics::TraceField{"locale", "en-US"},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("locale", fields, 4);
    set_last_error(abi::kErrorSuccess);
    return abi::kCp1252;
}

TL_MSABI std::uint32_t tl_GetOEMCP() noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", "oemcp"},
        diagnostics::TraceField{"code-page", "437"},
        diagnostics::TraceField{"locale", "en-US"},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("locale", fields, 4);
    set_last_error(abi::kErrorSuccess);
    return abi::kCp437;
}

TL_MSABI int tl_GetCPInfo(const std::uint32_t code_page, abi::GuestCpInfo* const info) noexcept {
    if (info == nullptr || !mapped_guest_range(info, sizeof(*info), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const bool single_byte = code_page == abi::kCpAcp || code_page == abi::kCp1252 ||
                             code_page == abi::kCpOem || code_page == abi::kCp437;
    if (!single_byte && code_page != abi::kCpUtf8) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    *info = {};
    info->max_char_size = code_page == abi::kCpUtf8 ? 4U : 1U;
    info->default_char[0] = '?';
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", "cpinfo"},
        diagnostics::TraceField{"code-page", std::to_string(code_page)},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"max-char-size", std::to_string(info->max_char_size)},
    };
    runtime_trace("locale", fields, 4);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetLocaleInfoW(const std::uint32_t locale, const std::uint32_t locale_type,
                               std::uint16_t* const data, const int data_count) noexcept {
    if (!supported_locale_lcid(locale) || data_count < 0 ||
        (data_count != 0 && data == nullptr)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if ((locale_type & abi::kLocaleReturnNumber) != 0U) {
        const std::optional<std::uint32_t> value = locale_number(locale_type);
        if (!value.has_value()) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        constexpr int kNumberUnits = static_cast<int>(sizeof(std::uint32_t) / sizeof(std::uint16_t));
        if (data == nullptr || data_count == 0) {
            set_last_error(abi::kErrorSuccess);
            return kNumberUnits;
        }
        if (data_count < kNumberUnits ||
            !mapped_guest_range(data, sizeof(std::uint32_t), true)) {
            set_last_error(abi::kErrorInsufficientBuffer);
            return 0;
        }
        std::memcpy(data, &*value, sizeof(*value));
        set_last_error(abi::kErrorSuccess);
        return kNumberUnits;
    }
    const std::optional<std::u16string> value = locale_string(locale_type);
    if (!value.has_value()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const int needed = static_cast<int>(value->size() + 1U);
    if (data == nullptr || data_count == 0) {
        set_last_error(abi::kErrorSuccess);
        return needed;
    }
    if (data_count < needed ||
        !mapped_guest_range(data, static_cast<std::size_t>(data_count) * sizeof(*data), true)) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    std::copy(value->begin(), value->end(), data);
    data[value->size()] = 0;
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", "info"},
        diagnostics::TraceField{"locale", "en-US"},
        diagnostics::TraceField{"type", std::to_string(locale_type)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("locale", fields, 4);
    set_last_error(abi::kErrorSuccess);
    return needed;
}

TL_MSABI int tl_GetLocaleInfoEx(const std::uint16_t* const locale_name,
                                const std::uint32_t locale_type,
                                std::uint16_t* const data,
                                const int data_count) noexcept {
    if (locale_name != nullptr && !locale_name_is_en_us(locale_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const int result = tl_GetLocaleInfoW(abi::kLocaleEnglishUnitedStates, locale_type,
                                         data, data_count);
    if (result != 0) {
        trace_locale_extension("info-ex", "GetLocaleInfoEx");
    }
    return result;
}

TL_MSABI int tl_IsValidLocale(const std::uint32_t locale, const std::uint32_t flags) noexcept {
    if ((flags & ~(abi::kLcidInstalled | abi::kLcidSupported)) != 0U || flags == 0U) {
        set_last_error(abi::kErrorInvalidFlags);
        return 0;
    }
    const int result = supported_locale_lcid(locale) ? 1 : 0;
    set_last_error(result != 0 ? abi::kErrorSuccess : abi::kErrorInvalidParameter);
    if (result != 0) {
        trace_locale_extension("valid-locale", "0409");
    }
    return result;
}

TL_MSABI int tl_IsValidCodePage(const std::uint32_t code_page) noexcept {
    const int result = supported_code_page(code_page) ? 1 : 0;
    set_last_error(result != 0 ? abi::kErrorSuccess : abi::kErrorInvalidParameter);
    if (result != 0) {
        trace_locale_extension("valid-code-page", std::to_string(code_page));
    }
    return result;
}

TL_MSABI int tl_EnumSystemLocalesW(const std::uintptr_t callback,
                                   const std::uint32_t flags) noexcept {
    if (flags == 0U || (flags & ~(abi::kLcidInstalled | abi::kLcidSupported)) != 0U) {
        set_last_error(abi::kErrorInvalidFlags);
        return 0;
    }
    if (callback == 0 || !guest_executable_callback(callback)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    static std::uint16_t kEnglishUnitedStates[] = {'0', '4', '0', '9', 0};
    using LocaleEnumProc = int (TL_MSABI *)(std::uint16_t*);
    const auto procedure = reinterpret_cast<LocaleEnumProc>(callback);
    if (procedure(kEnglishUnitedStates) == 0) {
        set_last_error(abi::kErrorSuccess);
        return 0;
    }
    trace_locale_extension("enumerate", "1");
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetStringTypeW(const std::uint32_t info_type,
                               const std::uint16_t* const source,
                               const int source_count,
                               std::uint16_t* const char_type) noexcept {
    if (info_type != abi::kCType1 || source == nullptr || source_count == 0 || source_count < -1 ||
        char_type == nullptr) {
        set_last_error(info_type == abi::kCType1 ? abi::kErrorInvalidParameter
                                                  : abi::kErrorInvalidFlags);
        return 0;
    }
    std::size_t units = 0;
    if (source_count == -1) {
        if (!mapped_guest_wstring(source)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        do {
            ++units;
        } while (source[units - 1U] != 0);
    } else {
        units = static_cast<std::size_t>(source_count);
        if (!mapped_guest_range(source, units * sizeof(*source), false)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    if (!mapped_guest_range(char_type, units * sizeof(*char_type), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    for (std::size_t index = 0; index < units; ++index) {
        char_type[index] = ctype1(source[index]);
    }
    trace_locale_extension("string-type", std::to_string(units));
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetDateFormatW(const std::uint32_t locale, const std::uint32_t flags,
                               const abi::GuestSystemTime* const date,
                               const std::uint16_t* const format,
                               std::uint16_t* const data, const int data_count) noexcept {
    if (!supported_locale_lcid(locale) || date == nullptr ||
        !mapped_guest_range(date, sizeof(*date), false) || format != nullptr || data_count < 0 ||
        (flags != 0U && flags != abi::kDateShortDate && flags != abi::kDateLongDate) ||
        !valid_system_time(*date)) {
        set_last_error(flags != 0U && flags != abi::kDateShortDate && flags != abi::kDateLongDate
                           ? abi::kErrorInvalidFlags
                           : abi::kErrorInvalidParameter);
        return 0;
    }
    std::u16string value;
    if (flags == abi::kDateLongDate) {
        static constexpr std::u16string_view kWeekdays[]{u"Sunday", u"Monday", u"Tuesday", u"Wednesday",
                                                          u"Thursday", u"Friday", u"Saturday"};
        static constexpr std::u16string_view kMonths[]{u"January", u"February", u"March", u"April",
                                                        u"May", u"June", u"July", u"August", u"September",
                                                        u"October", u"November", u"December"};
        value.append(kWeekdays[date->day_of_week]);
        value.append(u", ");
        value.append(kMonths[date->month - 1U]);
        value.push_back(u' ');
        append_decimal(value, date->day, 1);
        value.append(u", ");
        append_decimal(value, date->year, 4);
    } else {
        append_decimal(value, date->month, 1);
        value.push_back(u'/');
        append_decimal(value, date->day, 1);
        value.push_back(u'/');
        append_decimal(value, date->year, 4);
    }
    const int result = write_locale_value(value, data, data_count);
    if (result != 0) trace_locale_extension("date-format", flags == abi::kDateLongDate ? "long" : "short");
    return result;
}

TL_MSABI int tl_GetTimeFormatW(const std::uint32_t locale, const std::uint32_t flags,
                               const abi::GuestSystemTime* const time,
                               const std::uint16_t* const format,
                               std::uint16_t* const data, const int data_count) noexcept {
    const std::uint32_t allowed_flags = abi::kTimeNoSeconds | abi::kTimeNoTimeMarker |
                                        abi::kTimeForce24HourFormat;
    if (!supported_locale_lcid(locale) || time == nullptr ||
        !mapped_guest_range(time, sizeof(*time), false) || format != nullptr || data_count < 0 ||
        (flags & ~allowed_flags) != 0U || !valid_system_time(*time)) {
        set_last_error((flags & ~allowed_flags) != 0U ? abi::kErrorInvalidFlags
                                                       : abi::kErrorInvalidParameter);
        return 0;
    }
    const bool use_24_hour = (flags & (abi::kTimeNoTimeMarker | abi::kTimeForce24HourFormat)) != 0U;
    const bool show_seconds = (flags & abi::kTimeNoSeconds) == 0U;
    std::u16string value;
    if (use_24_hour) {
        append_decimal(value, time->hour, 2);
    } else {
        const std::uint16_t hour = static_cast<std::uint16_t>(time->hour % 12U == 0U ? 12U : time->hour % 12U);
        append_decimal(value, hour, 1);
    }
    value.push_back(u':');
    append_decimal(value, time->minute, 2);
    if (show_seconds) {
        value.push_back(u':');
        append_decimal(value, time->second, 2);
    }
    if (!use_24_hour) {
        value.append(time->hour < 12U ? u" AM" : u" PM");
    }
    const int result = write_locale_value(value, data, data_count);
    if (result != 0) trace_locale_extension("time-format", use_24_hour ? "24h" : "12h");
    return result;
}

TL_MSABI int tl_LCMapStringW(const std::uint32_t locale, const std::uint32_t flags,
                             const std::uint16_t* const source, const int source_count,
                             std::uint16_t* const destination, const int destination_count) noexcept {
    if (!supported_locale_lcid(locale)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const int result = map_locale_string(flags, source, source_count, destination, destination_count);
    if (result != 0) {
        const std::array<diagnostics::TraceField, 4> fields{
            diagnostics::TraceField{"operation", "map"},
            diagnostics::TraceField{"locale", "en-US"},
            diagnostics::TraceField{"status", "success"},
            diagnostics::TraceField{"units", std::to_string(result)},
        };
        runtime_trace("locale", fields, 4);
    }
    return result;
}

TL_MSABI int tl_LCMapStringEx(const std::uint16_t* const locale_name, const std::uint32_t flags,
                              const std::uint16_t* const source, const int source_count,
                              std::uint16_t* const destination, const int destination_count,
                              const void* const version_information, void* const reserved,
                              const std::uintptr_t sort_handle) noexcept {
    if (!locale_name_is_en_us(locale_name) || version_information != nullptr || reserved != nullptr ||
        sort_handle != 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    return tl_LCMapStringW(abi::kLocaleEnglishUnitedStates, flags, source, source_count,
                           destination, destination_count);
}

TL_MSABI int tl_MoveFileA(const char* from, const char* to) noexcept {
    if (from == nullptr || to == nullptr || !mapped_guest_cstring(from) || !mapped_guest_cstring(to) ||
        from[0] == '\0' || to[0] == '\0') {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char from_path[4096]{};
    char to_path[4096]{};
    if (!translate_windows_path(from, from_path, sizeof(from_path)) ||
        !translate_windows_path(to, to_path, sizeof(to_path)) || ::rename(from_path, to_path) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_FindFirstFileW(const std::uint16_t* path, void* find_data) noexcept {
    if (!mapped_guest_wstring(path) || find_data == nullptr ||
        !mapped_guest_range(find_data, sizeof(LegacyFindDataW), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    }
    std::string utf8_path;
    if (!wide_path_to_string(path, utf8_path)) {
        set_last_error(abi::kErrorInvalidParameter);
        return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    }
    Win32FindDataA ansi{};
    void* const handle = tl_FindFirstFileA(utf8_path.c_str(), &ansi);
    if (handle == reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max())) {
        return handle;
    }
    convert_find_data(ansi, *static_cast<LegacyFindDataW*>(find_data));
    return handle;
}

TL_MSABI void* tl_FindFirstFileExW(const std::uint16_t* const path, const int info_level,
                                   void* const find_data, const int search_operation,
                                   const void* const search_filter,
                                   const std::uint32_t additional_flags) noexcept {
    if ((info_level != static_cast<int>(abi::kFindExInfoStandard) &&
         info_level != static_cast<int>(abi::kFindExInfoBasic)) ||
        search_operation != static_cast<int>(abi::kFindExSearchNameMatch) ||
        search_filter != nullptr ||
        (additional_flags & ~abi::kFindFirstExLargeFetch) != 0) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_filesystem("find-first-ex", "failed", "unsupported-parameters");
        return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    }
    void* const handle = tl_FindFirstFileW(path, find_data);
    trace_filesystem("find-first-ex",
                     handle == reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max())
                         ? "failed"
                         : "success",
                     info_level == static_cast<int>(abi::kFindExInfoBasic) ? "basic" : "standard");
    return handle;
}

TL_MSABI int tl_FindNextFileW(const void* handle, void* find_data) noexcept {
    if (find_data == nullptr || !mapped_guest_range(find_data, sizeof(LegacyFindDataW), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    Win32FindDataA ansi{};
    if (tl_FindNextFileA(handle, &ansi) == 0) {
        return 0;
    }
    convert_find_data(ansi, *static_cast<LegacyFindDataW*>(find_data));
    return 1;
}

TL_MSABI std::uint32_t tl_GetTempFileNameW(const std::uint16_t* path_name,
                                           const std::uint16_t* prefix_string,
                                           std::uint32_t unique,
                                           std::uint16_t* temp_file_name) noexcept {
    constexpr std::size_t kMaxTempPath = 260;
    if (!mapped_guest_wstring(path_name) || !mapped_guest_wstring(prefix_string) ||
        path_name == nullptr || prefix_string == nullptr || temp_file_name == nullptr ||
        !mapped_guest_range(temp_file_name, kMaxTempPath * sizeof(*temp_file_name), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string directory;
    if (!normalize_wide_path(path_name, directory)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string prefix = util::wide_to_utf8(prefix_string).substr(0, 3);
    std::string pattern = directory;
    if (!pattern.empty() && pattern.back() != '/') {
        pattern.push_back('/');
    }
    pattern += prefix;
    pattern += "XXXXXX";
    std::vector<char> mutable_pattern(pattern.begin(), pattern.end());
    mutable_pattern.push_back('\0');
    const int fd = ::mkstemp(mutable_pattern.data());
    if (fd < 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    ::close(fd);
    const std::u16string wide_name = util::utf8_to_wide(mutable_pattern.data());
    std::uint32_t error = abi::kErrorSuccess;
    if (!copy_wide_string(wide_name, temp_file_name, kMaxTempPath, error)) {
        set_last_error(error);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return unique != 0U ? unique : 1U;
}

TL_MSABI int tl_SetEndOfFile(const void* handle) noexcept {
    FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr || slot->position < 0 || ::ftruncate(slot->fd, slot->position) != 0) {
        set_last_error(slot == nullptr ? abi::kErrorInvalidHandle : errno_to_win32(errno));
        return 0;
    }
    slot->file_size = static_cast<std::uint64_t>(slot->position);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetFileAttributesExW(const std::uint16_t* path, int info_level, void* data) noexcept {
    if (info_level != 0 || data == nullptr || !mapped_guest_range(data, sizeof(LegacyFileAttributeData), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string normalized;
    struct stat st{};
    if (!normalize_wide_path(path, normalized) || ::stat(normalized.c_str(), &st) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    auto* result = static_cast<LegacyFileAttributeData*>(data);
    *result = {};
    result->attributes = stat_to_win32_attributes(normalized.c_str(), st);
    write_filetime(st.st_ctim, result->creation);
    write_filetime(st.st_atim, result->last_access);
    write_filetime(st.st_mtim, result->last_write);
    const auto file_size = static_cast<std::uint64_t>(st.st_size);
    result->size_low = static_cast<std::uint32_t>(file_size);
    result->size_high = static_cast<std::uint32_t>(file_size >> 32U);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_MoveFileW(const std::uint16_t* from, const std::uint16_t* to) noexcept {
    std::string from_utf8;
    std::string to_utf8;
    if (!wide_path_to_string(from, from_utf8) || !wide_path_to_string(to, to_utf8)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    return tl_MoveFileA(from_utf8.c_str(), to_utf8.c_str());
}

TL_MSABI int tl_MoveFileExW(const std::uint16_t* from, const std::uint16_t* to,
                            std::uint32_t flags) noexcept {
    constexpr std::uint32_t kMoveFileReplaceExisting = 0x1U;
    if ((flags & ~kMoveFileReplaceExisting) != 0U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    return tl_MoveFileW(from, to);
}

TL_MSABI int tl_CopyFileW(const std::uint16_t* from, const std::uint16_t* to,
                          int fail_if_exists) noexcept {
    std::string source_path;
    std::string target_path;
    if (!normalize_wide_path(from, source_path) || !normalize_wide_path(to, target_path)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const int source = ::open(source_path.c_str(), O_RDONLY);
    if (source < 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    const int target_flags = O_WRONLY | O_CREAT | (fail_if_exists != 0 ? O_EXCL : O_TRUNC);
    const int target = ::open(target_path.c_str(), target_flags, 0666);
    if (target < 0) {
        const int failure = errno;
        ::close(source);
        set_last_error(errno_to_win32(failure));
        return 0;
    }
    bool success = true;
    std::array<char, 16384> buffer{};
    ssize_t count = 0;
    while ((count = ::read(source, buffer.data(), buffer.size())) > 0) {
        ssize_t written = 0;
        while (written < count) {
            const ssize_t result = ::write(target, buffer.data() + written, static_cast<std::size_t>(count - written));
            if (result <= 0) {
                success = false;
                break;
            }
            written += result;
        }
        if (!success) {
            break;
        }
    }
    if (count < 0) {
        success = false;
    }
    const int failure = errno;
    ::close(source);
    ::close(target);
    if (!success) {
        set_last_error(errno_to_win32(failure));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_RemoveDirectoryW(const std::uint16_t* path) noexcept {
    std::string normalized;
    if (!normalize_wide_path(path, normalized) || ::rmdir(normalized.c_str()) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetTempPathW(std::uint32_t buffer_length, std::uint16_t* buffer) noexcept {
    const std::u16string value = u"C:\\windows\\temp\\";
    const std::size_t required = value.size() + 1U;
    if (buffer == nullptr || buffer_length < required ||
        !mapped_guest_range(buffer, static_cast<std::size_t>(buffer_length) * sizeof(*buffer), true)) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return static_cast<std::uint32_t>(required);
    }
    std::copy(value.begin(), value.end(), buffer);
    buffer[value.size()] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(value.size());
}

namespace {

std::string normalize_windows_path_segments(const std::string& absolute_with_drive) {
    // Normaliza um caminho Windows absoluto já com drive ou UNC.
    // Inspirado em Wine dlls/kernel32/path.c: colapsa ".", ".." e "\" duplicados.
    std::string drive;
    std::string_view rest;
    bool is_unc = false;
    if (absolute_with_drive.size() >= 2 && absolute_with_drive[0] == '\\' &&
        absolute_with_drive[1] == '\\') {
        is_unc = true;
        // UNC: \\server\share\...
        // Mantém prefixo "\\" e normaliza o resto.
        rest = std::string_view(absolute_with_drive).substr(2);
        drive = "\\\\";
    } else if (absolute_with_drive.size() >= 2 && absolute_with_drive[1] == ':') {
        drive = absolute_with_drive.substr(0, 2);
        rest = std::string_view(absolute_with_drive).substr(2);
    } else {
        rest = absolute_with_drive;
    }

    // Quebra em segmentos por '\' e '/'.
    std::vector<std::string> stack;
    std::string current;
    for (std::size_t i = 0; i <= rest.size(); ++i) {
        const char c = i < rest.size() ? rest[i] : '\\'; // sentinela
        if (c == '\\' || c == '/') {
            if (current.empty() || current == ".") {
                // ignora
            } else if (current == "..") {
                if (!stack.empty()) stack.pop_back();
            } else {
                stack.push_back(current);
            }
            current.clear();
        } else {
            current.push_back(c);
        }
    }

    std::string out = drive;
    if (is_unc) {
        // UNC já tem "\\"
        for (const auto& seg : stack) {
            out += seg;
            out.push_back('\\');
        }
        if (!stack.empty()) out.pop_back();
        if (out == "\\\\") out = "\\\\";
    } else {
        if (!drive.empty()) {
            out.push_back('\\');
        } else if (!stack.empty()) {
            // sem drive mas absoluto tipo "\foo"
            out.push_back('\\');
        }
        for (std::size_t i = 0; i < stack.size(); ++i) {
            out += stack[i];
            if (i + 1 < stack.size()) out.push_back('\\');
        }
        if (out.empty()) out = drive.empty() ? "\\" : drive + "\\";
    }
    return out;
}

std::string build_full_windows_path(const std::string& input_raw) {
    // Converte input_raw (com '\' e '/') para caminho absoluto Windows.
    std::string input = input_raw;
    std::replace(input.begin(), input.end(), '/', '\\');

    char cwd_buf[4096]{};
    const char* cwd_cstr = ::getcwd(cwd_buf, sizeof(cwd_buf)) != nullptr ? cwd_buf : ".";
    std::string win_cwd =
        prefix::to_windows_path(std::filesystem::path(cwd_cstr), guest_prefix_root());
    std::replace(win_cwd.begin(), win_cwd.end(), '/', '\\');

    // Garante que win_cwd tenha formato "C:\..."
    if (win_cwd.size() == 2 && win_cwd[1] == ':') win_cwd += "\\";

    const bool is_unc = input.size() >= 2 && input[0] == '\\' && input[1] == '\\';
    const bool is_drive_abs =
        input.size() >= 3 && std::isalpha(static_cast<unsigned char>(input[0])) &&
        input[1] == ':' && (input[2] == '\\' || input[2] == '/');
    const bool is_rooted = !input.empty() && (input[0] == '\\' || input[0] == '/');
    const bool is_drive_relative =
        input.size() >= 2 && std::isalpha(static_cast<unsigned char>(input[0])) && input[1] == ':';

    std::string combined;
    if (is_unc || is_drive_abs) {
        combined = input;
    } else if (is_rooted) {
        // "\Windows\Foo" -> drive do cwd + resto
        std::string drive = win_cwd.substr(0, 2);
        combined = drive + input;
    } else if (is_drive_relative) {
        // "C:foo" ou "C:foo\bar"
        std::string drive = input.substr(0, 2);
        std::string rest = input.substr(2);
        while (!rest.empty() && (rest[0] == '\\' || rest[0] == '/')) rest.erase(0, 1);
        std::string cwd_drive = win_cwd.substr(0, 2);
        if (drive == cwd_drive) {
            std::string cwd_rest = win_cwd.substr(2);
            if (!cwd_rest.empty() && cwd_rest.back() == '\\') cwd_rest.pop_back();
            // cwd_rest é "\a\b" ou "\"
            if (rest.empty()) {
                combined = win_cwd;
            } else {
                combined = win_cwd;
                if (!combined.empty() && combined.back() != '\\') combined.push_back('\\');
                combined += rest;
            }
        } else {
            combined = drive + "\\" + rest;
        }
    } else {
        // relativo puro
        combined = win_cwd;
        if (!combined.empty() && combined.back() != '\\') combined.push_back('\\');
        combined += input;
    }

    return normalize_windows_path_segments(combined);
}

}  // namespace

TL_MSABI std::uint32_t tl_GetFullPathNameW(const std::uint16_t* path, std::uint32_t buffer_length,
                                           std::uint16_t* buffer, std::uint16_t** file_part) noexcept {
    if (path == nullptr || !mapped_guest_wstring(path)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string utf8 = util::wide_to_utf8(path);
    if (utf8.empty() && path[0] != 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // Caso especial: string vazia -> retorna 0 como Wine.
    if (utf8.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string full = build_full_windows_path(utf8);
    const std::u16string wide = util::utf8_to_wide(full);
    if (file_part != nullptr && !mapped_guest_range(file_part, sizeof(*file_part), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (buffer == nullptr || buffer_length == 0) {
        // Wine: com buffer nulo, retorna tamanho necessário sem escrever.
        // Retornamos wide.size() (sem terminador) para compatibilidade com teste existente,
        // mas documentamos que inclui terminador no cálculo de insuficiência.
        set_last_error(abi::kErrorSuccess);
        return static_cast<std::uint32_t>(wide.size() + 1);
    }
    if (buffer_length <= wide.size() ||
        !mapped_guest_range(buffer, static_cast<std::size_t>(buffer_length) * sizeof(*buffer), true)) {
        if (file_part != nullptr) *file_part = nullptr;
        set_last_error(abi::kErrorInsufficientBuffer);
        return static_cast<std::uint32_t>(wide.size() + 1);
    }
    std::copy(wide.begin(), wide.end(), buffer);
    buffer[wide.size()] = 0;
    if (file_part != nullptr) {
        // Para simplicidade, recalcula via wide: encontra último '\' no buffer.
        std::size_t wide_slash = wide.find_last_of(u'\\');
        std::size_t wide_colon = wide.find_last_of(u':');
        std::size_t wpos = std::u16string::npos;
        if (wide_slash != std::u16string::npos) wpos = wide_slash;
        if (wide_colon != std::u16string::npos && wide_colon + 1 > wpos) wpos = wide_colon;
        *file_part = wpos == std::u16string::npos ? buffer : buffer + wpos + 1;
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(wide.size());
}

TL_MSABI std::uint32_t tl_GetFullPathNameA(const char* path, std::uint32_t buffer_length, char* buffer,
                                          char** file_part) noexcept {
    if (path == nullptr || !mapped_guest_cstring(path)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::u16string wpath = util::utf8_to_wide(path);
    // Reusa lógica W para garantir mesma normalização.
    const std::string full = build_full_windows_path(path);
    if (file_part != nullptr && !mapped_guest_range(file_part, sizeof(*file_part), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (buffer == nullptr || buffer_length == 0) {
        set_last_error(abi::kErrorSuccess);
        return static_cast<std::uint32_t>(full.size() + 1);
    }
    if (buffer_length <= full.size() ||
        !mapped_guest_range(buffer, static_cast<std::size_t>(buffer_length), true)) {
        if (file_part != nullptr) *file_part = nullptr;
        set_last_error(abi::kErrorInsufficientBuffer);
        return static_cast<std::uint32_t>(full.size() + 1);
    }
    std::memcpy(buffer, full.data(), full.size());
    buffer[full.size()] = '\0';
    if (file_part != nullptr) {
        const std::size_t slash = full.find_last_of('\\');
        const std::size_t colon = full.find_last_of(':');
        std::size_t pos = std::string::npos;
        if (slash != std::string::npos) pos = slash;
        if (colon != std::string::npos && colon + 1 > pos) pos = colon;
        *file_part = pos == std::string::npos ? buffer : buffer + pos + 1;
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(full.size());
}

TL_MSABI int tl_GetFileTime(const void* handle, void* creation_time, void* access_time,
                            void* write_time) noexcept {
    const FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if ((creation_time != nullptr && !mapped_guest_range(creation_time, sizeof(LegacyFileTime), true)) ||
        (access_time != nullptr && !mapped_guest_range(access_time, sizeof(LegacyFileTime), true)) ||
        (write_time != nullptr && !mapped_guest_range(write_time, sizeof(LegacyFileTime), true))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    struct stat st{};
    if (::fstat(slot->fd, &st) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    if (creation_time != nullptr) {
        write_filetime(st.st_ctim, *static_cast<LegacyFileTime*>(creation_time));
    }
    if (access_time != nullptr) {
        write_filetime(st.st_atim, *static_cast<LegacyFileTime*>(access_time));
    }
    if (write_time != nullptr) {
        write_filetime(st.st_mtim, *static_cast<LegacyFileTime*>(write_time));
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetFileTime(const void* handle, const void* creation_time,
                            const void* access_time, const void* write_time) noexcept {
    FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if ((creation_time != nullptr && !mapped_guest_range(creation_time, sizeof(LegacyFileTime), false)) ||
        (access_time != nullptr && !mapped_guest_range(access_time, sizeof(LegacyFileTime), false)) ||
        (write_time != nullptr && !mapped_guest_range(write_time, sizeof(LegacyFileTime), false))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    struct stat st{};
    if (::fstat(slot->fd, &st) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    timespec times[2]{st.st_atim, st.st_mtim};
    if (access_time != nullptr && !filetime_to_timespec(*static_cast<const LegacyFileTime*>(access_time), times[0])) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (write_time != nullptr && !filetime_to_timespec(*static_cast<const LegacyFileTime*>(write_time), times[1])) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (::futimens(slot->fd, times) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetFileInformationByHandle(const void* handle, void* information) noexcept {
    const FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (information == nullptr || !mapped_guest_range(information, sizeof(LegacyByHandleFileInformation), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    struct stat st{};
    if (::fstat(slot->fd, &st) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    auto* result = static_cast<LegacyByHandleFileInformation*>(information);
    *result = {};
    result->attributes = stat_to_win32_attributes(slot->path.c_str(), st);
    write_filetime(st.st_ctim, result->creation);
    write_filetime(st.st_atim, result->last_access);
    write_filetime(st.st_mtim, result->last_write);
    const auto file_size = static_cast<std::uint64_t>(st.st_size);
    const auto inode = static_cast<std::uint64_t>(st.st_ino);
    result->volume_serial_number = static_cast<std::uint32_t>(st.st_dev);
    result->size_low = static_cast<std::uint32_t>(file_size);
    result->size_high = static_cast<std::uint32_t>(file_size >> 32U);
    result->number_of_links = static_cast<std::uint32_t>(st.st_nlink);
    result->file_index_low = static_cast<std::uint32_t>(inode);
    result->file_index_high = static_cast<std::uint32_t>(inode >> 32U);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetFileInformationByHandleEx(const void* handle, int info_class,
                                             void* buffer, std::uint32_t size) noexcept {
    const FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (info_class != 0 || buffer == nullptr || size < sizeof(LegacyBasicFileInformation) ||
        !mapped_guest_range(buffer, sizeof(LegacyBasicFileInformation), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    struct stat st{};
    if (::fstat(slot->fd, &st) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    *static_cast<LegacyBasicFileInformation*>(buffer) = {
        filetime_ticks(st.st_ctim), filetime_ticks(st.st_atim), filetime_ticks(st.st_mtim),
        filetime_ticks(st.st_ctim), stat_to_win32_attributes(slot->path.c_str(), st), 0};
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetFileInformationByHandle(const void* const handle, const int info_class,
                                           const void* const buffer,
                                           const std::uint32_t size) noexcept {
    FileSlot* const slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_filesystem("set-information", "failed", "invalid-handle");
        return 0;
    }
    if (buffer == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_filesystem("set-information", "failed", "null-buffer");
        return 0;
    }

    if (info_class == static_cast<int>(abi::kFileBasicInfo)) {
        if (size < sizeof(abi::GuestFileBasicInfo) ||
            !mapped_guest_range(buffer, sizeof(abi::GuestFileBasicInfo), false)) {
            set_last_error(abi::kErrorInvalidParameter);
            trace_filesystem("set-information", "failed", "short-basic-info");
            return 0;
        }
        const auto& info = *static_cast<const abi::GuestFileBasicInfo*>(buffer);
        if (info.reserved != 0) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        struct stat st{};
        if (::fstat(slot->fd, &st) != 0) {
            const std::uint32_t error = errno_to_win32(errno);
            set_last_error(error);
            return 0;
        }
        timespec times[2]{st.st_atim, st.st_mtim};
        const auto to_file_time = [](const std::int64_t value) noexcept {
            const std::uint64_t bits = static_cast<std::uint64_t>(value);
            return LegacyFileTime{static_cast<std::uint32_t>(bits & 0xFFFFFFFFU),
                                  static_cast<std::uint32_t>(bits >> 32U)};
        };
        if ((info.last_access_time != 0 &&
             !filetime_to_timespec(to_file_time(info.last_access_time), times[0])) ||
            (info.last_write_time != 0 &&
             !filetime_to_timespec(to_file_time(info.last_write_time), times[1]))) {
            set_last_error(abi::kErrorInvalidParameter);
            trace_filesystem("set-information", "failed", "invalid-time");
            return 0;
        }
        if ((info.last_access_time != 0 || info.last_write_time != 0) &&
            ::futimens(slot->fd, times) != 0) {
            const std::uint32_t error = errno_to_win32(errno);
            set_last_error(error);
            return 0;
        }
        if (info.file_attributes != 0) {
            const std::uint32_t error = apply_win32_file_attributes(
                slot->path.c_str(), info.file_attributes);
            if (error != abi::kErrorSuccess) {
                set_last_error(error);
                trace_filesystem("set-information", "failed", "invalid-attributes");
                return 0;
            }
        }
        set_last_error(abi::kErrorSuccess);
        trace_filesystem("set-information", "success", "FileBasicInfo");
        return 1;
    }

    bool delete_file = false;
    bool posix_semantics = false;
    bool ignore_readonly = false;
    if (info_class == static_cast<int>(abi::kFileDispositionInfo)) {
        if (size < sizeof(abi::GuestFileDispositionInfo) ||
            !mapped_guest_range(buffer, sizeof(abi::GuestFileDispositionInfo), false)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        delete_file = static_cast<const abi::GuestFileDispositionInfo*>(buffer)->delete_file != 0;
    } else if (info_class == static_cast<int>(abi::kFileDispositionInfoEx)) {
        if (size < sizeof(abi::GuestFileDispositionInfoEx) ||
            !mapped_guest_range(buffer, sizeof(abi::GuestFileDispositionInfoEx), false)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        const std::uint32_t flags =
            static_cast<const abi::GuestFileDispositionInfoEx*>(buffer)->flags;
        constexpr std::uint32_t kSupportedFlags = abi::kFileDispositionFlagDelete |
                                                   abi::kFileDispositionFlagPosixSemantics |
                                                   abi::kFileDispositionFlagOnClose |
                                                   abi::kFileDispositionFlagIgnoreReadonlyAttribute;
        if ((flags & ~kSupportedFlags) != 0 ||
            ((flags & ~abi::kFileDispositionFlagDelete) != 0 &&
             (flags & abi::kFileDispositionFlagDelete) == 0)) {
            set_last_error(abi::kErrorInvalidParameter);
            trace_filesystem("set-information", "failed", "invalid-disposition-flags");
            return 0;
        }
        delete_file = (flags & abi::kFileDispositionFlagDelete) != 0;
        posix_semantics = (flags & abi::kFileDispositionFlagPosixSemantics) != 0;
        ignore_readonly =
            (flags & abi::kFileDispositionFlagIgnoreReadonlyAttribute) != 0;
    } else {
        set_last_error(abi::kErrorInvalidParameter);
        trace_filesystem("set-information", "failed", "unsupported-class");
        return 0;
    }

    if (!delete_file) {
        slot->delete_pending = false;
        set_last_error(abi::kErrorSuccess);
        trace_filesystem("set-information", "success", "delete-cancelled");
        return 1;
    }
    struct stat st{};
    if (::fstat(slot->fd, &st) != 0) {
        const std::uint32_t error = errno_to_win32(errno);
        set_last_error(error);
        return 0;
    }
    if (!ignore_readonly && (stat_to_win32_attributes(slot->path.c_str(), st) &
                             abi::kFileAttributeReadOnly) != 0) {
        set_last_error(abi::kErrorAccessDenied);
        trace_filesystem("set-information", "failed", "read-only");
        return 0;
    }
    if (posix_semantics) {
        if (!slot->unlinked && ::unlink(slot->path.c_str()) != 0) {
            const std::uint32_t error = errno_to_win32(errno);
            set_last_error(error);
            trace_filesystem("set-information", "failed", "posix-delete");
            return 0;
        }
        slot->unlinked = true;
        slot->delete_pending = false;
        trace_filesystem("set-information", "success", "posix-delete");
    } else {
        slot->delete_pending = true;
        trace_filesystem("set-information", "success", "delete-on-close");
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetFinalPathNameByHandleW(const void* handle, std::uint16_t* buffer,
                                                    std::uint32_t buffer_length,
                                                    std::uint32_t flags) noexcept {
    if (flags != 0U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    const std::u16string path = final_windows_path(slot->path);
    if (buffer == nullptr || buffer_length <= path.size()) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return static_cast<std::uint32_t>(path.size() + 1U);
    }
    if (!mapped_guest_range(buffer, static_cast<std::size_t>(buffer_length) * sizeof(*buffer), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::copy(path.begin(), path.end(), buffer);
    buffer[path.size()] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(path.size());
}

TL_MSABI int tl_CreateProcessW(const std::uint16_t* application_name,
                               std::uint16_t* command_line, const void* process_attributes,
                               const void* thread_attributes, int inherit_handles,
                               std::uint32_t creation_flags, const void* environment,
                               const std::uint16_t* current_directory, void* startup_info,
                               void* process_information) noexcept {
    if ((current_directory != nullptr && !mapped_guest_wstring(current_directory)) ||
        (application_name != nullptr && !mapped_guest_wstring(application_name)) ||
        (application_name == nullptr && !mapped_guest_wstring(command_line))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string application = application_name != nullptr ? util::wide_to_utf8(application_name) : "";
    std::string command = command_line != nullptr ? util::wide_to_utf8(command_line) : "";
    const std::string directory =
        current_directory != nullptr ? util::wide_to_utf8(current_directory) : "";
    char* command_pointer = command.empty() ? nullptr : command.data();
    const char* application_pointer = application.empty() ? nullptr : application.c_str();
    const char* directory_pointer = directory.empty() ? nullptr : directory.c_str();
    return tl_CreateProcessA(application_pointer, command_pointer, process_attributes, thread_attributes,
                             inherit_handles, creation_flags, environment, directory_pointer, startup_info,
                             process_information);
}

TL_MSABI int tl_Rectangle(const void* dc, int left, int top, int right, int bottom) noexcept {
    WindowSlot* slot = find_window_slot(dc);
    if (slot == nullptr || slot->native == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (right > left && bottom > top) {
        gui::draw_rectangle(slot->native, left, top, right - left, bottom - top);
        gui::flush_window(slot->native);
    }
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "Rectangle"},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("Rectangle", fields, 2);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

}  // namespace tradutorlinux
