#include "kernel32_common.hpp"
#include "kernel32_file_internal.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <cstring>

namespace tradutorlinux {

namespace {

struct GuestTimeZoneInformation {
    std::int32_t bias{0};
    std::uint16_t standard_name[32]{};
    std::uint16_t standard_date[8]{};
    std::int32_t standard_bias{0};
    std::uint16_t daylight_name[32]{};
    std::uint16_t daylight_date[8]{};
    std::int32_t daylight_bias{0};
};

} // namespace

extern "C" {

TL_MSABI std::uint64_t tl_GetTickCount64() noexcept {
    using namespace std::chrono;
    const auto now = steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(now).count());
}

TL_MSABI void tl_GetSystemTimeAsFileTime(void* file_time) noexcept {
    if (file_time == nullptr || !mapped_guest_range(file_time, sizeof(std::uint64_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    auto* ft = static_cast<std::uint64_t*>(file_time);
    using namespace std::chrono;
    const auto now = system_clock::now().time_since_epoch();
    const auto since_epoch = duration_cast<nanoseconds>(now).count();
    const std::uint64_t ticks_100ns = static_cast<std::uint64_t>(since_epoch) / 100;
    const std::uint64_t epoch_diff = 116444736000000000ULL;
    *ft = ticks_100ns + epoch_diff;
}

TL_MSABI int tl_QueryPerformanceCounter(std::int64_t* performance_count) noexcept {
    if (performance_count == nullptr || !mapped_guest_range(performance_count, sizeof(*performance_count), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    struct timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
    }
    const std::int64_t ticks = static_cast<std::int64_t>(ts.tv_sec) * 10000000LL +
                               static_cast<std::int64_t>(ts.tv_nsec) / 100LL;
    *performance_count = ticks;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_QueryPerformanceFrequency(std::int64_t* frequency) noexcept {
    if (frequency == nullptr || !mapped_guest_range(frequency, sizeof(*frequency), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    *frequency = 10000000LL;  // 10 MHz
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void tl_GetSystemTime(void* system_time) noexcept {
    if (system_time == nullptr || !mapped_guest_range(system_time, sizeof(abi::GuestSystemTime), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    std::time_t t = std::time(nullptr);
    std::tm tm_utc{};
    gmtime_r(&t, &tm_utc);
    auto* st = static_cast<abi::GuestSystemTime*>(system_time);
    st->year = static_cast<std::uint16_t>(tm_utc.tm_year + 1900);
    st->month = static_cast<std::uint16_t>(tm_utc.tm_mon + 1);
    st->day_of_week = static_cast<std::uint16_t>(tm_utc.tm_wday);
    st->day = static_cast<std::uint16_t>(tm_utc.tm_mday);
    st->hour = static_cast<std::uint16_t>(tm_utc.tm_hour);
    st->minute = static_cast<std::uint16_t>(tm_utc.tm_min);
    st->second = static_cast<std::uint16_t>(tm_utc.tm_sec);
    st->milliseconds = 0;
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI void tl_GetLocalTime(void* system_time) noexcept {
    if (system_time == nullptr || !mapped_guest_range(system_time, sizeof(abi::GuestSystemTime), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    std::time_t t = std::time(nullptr);
    std::tm tm_loc{};
    localtime_r(&t, &tm_loc);
    auto* st = static_cast<abi::GuestSystemTime*>(system_time);
    st->year = static_cast<std::uint16_t>(tm_loc.tm_year + 1900);
    st->month = static_cast<std::uint16_t>(tm_loc.tm_mon + 1);
    st->day_of_week = static_cast<std::uint16_t>(tm_loc.tm_wday);
    st->day = static_cast<std::uint16_t>(tm_loc.tm_mday);
    st->hour = static_cast<std::uint16_t>(tm_loc.tm_hour);
    st->minute = static_cast<std::uint16_t>(tm_loc.tm_min);
    st->second = static_cast<std::uint16_t>(tm_loc.tm_sec);
    st->milliseconds = 0;
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI int tl_FileTimeToSystemTime(const void* file_time, void* system_time) noexcept {
    if (file_time == nullptr || system_time == nullptr ||
        !mapped_guest_range(file_time, 8, false) ||
        !mapped_guest_range(system_time, sizeof(abi::GuestSystemTime), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto* ft = static_cast<const GuestFileTime*>(file_time);
    const std::time_t t = filetime_to_unix_time(*ft);
    std::tm tm_utc{};
    gmtime_r(&t, &tm_utc);
    auto* st = static_cast<abi::GuestSystemTime*>(system_time);
    st->year = static_cast<std::uint16_t>(tm_utc.tm_year + 1900);
    st->month = static_cast<std::uint16_t>(tm_utc.tm_mon + 1);
    st->day_of_week = static_cast<std::uint16_t>(tm_utc.tm_wday);
    st->day = static_cast<std::uint16_t>(tm_utc.tm_mday);
    st->hour = static_cast<std::uint16_t>(tm_utc.tm_hour);
    st->minute = static_cast<std::uint16_t>(tm_utc.tm_min);
    st->second = static_cast<std::uint16_t>(tm_utc.tm_sec);
    st->milliseconds = 0;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SystemTimeToFileTime(const void* system_time, void* file_time) noexcept {
    if (system_time == nullptr || file_time == nullptr ||
        !mapped_guest_range(system_time, sizeof(abi::GuestSystemTime), false) ||
        !mapped_guest_range(file_time, 8, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto* st = static_cast<const abi::GuestSystemTime*>(system_time);
    std::tm tm_utc{};
    tm_utc.tm_year = st->year - 1900;
    tm_utc.tm_mon = st->month - 1;
    tm_utc.tm_mday = st->day;
    tm_utc.tm_hour = st->hour;
    tm_utc.tm_min = st->minute;
    tm_utc.tm_sec = st->second;
    const std::time_t t = timegm(&tm_utc);
    auto* ft = static_cast<GuestFileTime*>(file_time);
    filetime_from_unix(t, *ft);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetTimeZoneInformation(void* const tz_info) noexcept {
    if (tz_info == nullptr || !mapped_guest_range(tz_info, sizeof(GuestTimeZoneInformation), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0xFFFFFFFFU;
    }
    auto* const tzi = static_cast<GuestTimeZoneInformation*>(tz_info);
    *tzi = GuestTimeZoneInformation{};
    tzi->bias = 0;
    const std::u16string std_name = util::utf8_to_wide("UTC");
    std::copy(std_name.begin(), std_name.end(), tzi->standard_name);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_FileTimeToLocalFileTime(const void* const file_time, void* const local_file_time) noexcept {
    if (file_time == nullptr || local_file_time == nullptr ||
        !mapped_guest_range(file_time, sizeof(std::uint64_t), false) ||
        !mapped_guest_range(local_file_time, sizeof(std::uint64_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    *static_cast<std::uint64_t*>(local_file_time) = *static_cast<const std::uint64_t*>(file_time);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetTickCount(void) noexcept {
    struct timespec ts{};
    if (::clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        const std::uint64_t ms = static_cast<std::uint64_t>(ts.tv_sec) * 1000U +
                                 static_cast<std::uint64_t>(ts.tv_nsec) / 1000000U;
        return static_cast<std::uint32_t>(ms & 0xFFFFFFFFU);
    }
    return 1000U;
}

TL_MSABI int tl_SystemTimeToTzSpecificLocalTime(const void* const tz_info,
                                               const void* const universal_time,
                                               void* const local_time) noexcept {
    (void)tz_info;
    if (universal_time == nullptr || local_time == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(universal_time, 16, false) || !mapped_guest_range(local_time, 16, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::memcpy(local_time, universal_time, 16);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_FileTimeToDosDateTime(const void* const file_time, std::uint16_t* const fat_date,
                                       std::uint16_t* const fat_time) noexcept {
    if (file_time == nullptr || fat_date == nullptr || fat_time == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(file_time, 8, false) || !mapped_guest_range(fat_date, 2, true) ||
        !mapped_guest_range(fat_time, 2, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    *fat_date = 0x5821; // 2024-01-01
    *fat_time = 0x0000; // 00:00:00
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_DosDateTimeToFileTime(const std::uint16_t fat_date, const std::uint16_t fat_time,
                                       void* const file_time) noexcept {
    if (file_time == nullptr || !mapped_guest_range(file_time, 8, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uint32_t day = fat_date & 0x1FU;
    const std::uint32_t month = (fat_date >> 5U) & 0x0FU;
    const std::uint32_t year = ((fat_date >> 9U) & 0x7FU) + 1980U;
    const std::uint32_t second = (fat_time & 0x1FU) * 2U;
    const std::uint32_t minute = (fat_time >> 5U) & 0x3FU;
    const std::uint32_t hour = (fat_time >> 11U) & 0x1FU;
    if (day < 1U || day > 31U || month < 1U || month > 12U || year < 1980U || year > 2107U ||
        second > 59U || minute > 59U || hour > 23U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // Validar dias do mês (inclui fevereiro bissexto).
    const bool is_leap = (year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U));
    const std::uint32_t days_in_month[] = {31, is_leap ? 29U : 28U, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (day > days_in_month[month - 1U]) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::tm tm_utc{};
    tm_utc.tm_year = static_cast<int>(year - 1900U);
    tm_utc.tm_mon = static_cast<int>(month - 1U);
    tm_utc.tm_mday = static_cast<int>(day);
    tm_utc.tm_hour = static_cast<int>(hour);
    tm_utc.tm_min = static_cast<int>(minute);
    tm_utc.tm_sec = static_cast<int>(second);
    tm_utc.tm_isdst = -1;
    const std::time_t seconds = timegm(&tm_utc);
    if (seconds == static_cast<std::time_t>(-1)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    auto* const out = static_cast<GuestFileTime*>(file_time);
    filetime_from_unix(seconds, *out);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::int32_t tl_CompareFileTime(const void* const file_time1, const void* const file_time2) noexcept {
    if (file_time1 == nullptr || file_time2 == nullptr) {
        return 0;
    }
    std::uint64_t t1 = 0;
    std::uint64_t t2 = 0;
    std::memcpy(&t1, file_time1, sizeof(t1));
    std::memcpy(&t2, file_time2, sizeof(t2));
    if (t1 < t2) return -1;
    if (t1 > t2) return 1;
    return 0;
}

TL_MSABI int tl_TzSpecificLocalTimeToSystemTime(const void* const tz_info, const void* const local_time,
                                               void* const universal_time) noexcept {
    (void)tz_info;
    if (local_time == nullptr || universal_time == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(local_time, 16, false) || !mapped_guest_range(universal_time, 16, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::memcpy(universal_time, local_time, 16);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_LocalFileTimeToFileTime(const void* const local_file_time, void* const file_time) noexcept {
    if (local_file_time != nullptr && file_time != nullptr &&
        mapped_guest_range(local_file_time, 8, false) && mapped_guest_range(file_time, 8, true)) {
        *reinterpret_cast<std::uint64_t*>(file_time) = *reinterpret_cast<const std::uint64_t*>(local_file_time);
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    set_last_error(abi::kErrorInvalidParameter);
    return 0;
}

}  // extern "C"
}  // namespace tradutorlinux
