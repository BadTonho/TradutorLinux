#include "kernel32_internal.hpp"
namespace tradutorlinux {

namespace {

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

std::string format_message_text(const std::uint32_t flags,
                                const std::uint32_t message_id) {
    if ((flags & abi::kFormatMessageFromSystem) == 0U) {
        return "Unknown error " + std::to_string(message_id) + ".";
    }
    switch (message_id) {
        case abi::kErrorSuccess:
            return "The operation completed successfully.";
        case abi::kErrorFileNotFound:
            return "The system cannot find the file specified.";
        case abi::kErrorAccessDenied:
            return "Access is denied.";
        case abi::kErrorInvalidHandle:
            return "The handle is invalid.";
        case abi::kErrorNotEnoughMemory:
            return "Not enough memory resources are available to process this command.";
        case abi::kErrorBadLength:
            return "The program issued a command but the command length is incorrect.";
        case abi::kErrorAlreadyExists:
            return "Cannot create a file when that file already exists.";
        case abi::kErrorEnvvarNotFound:
            return "The system could not find the environment option that was entered.";
        default:
            return "Unknown error " + std::to_string(message_id) + ".";
    }
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
    return is_guest_executable_address(address);
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

} // namespace

extern "C" {

TL_MSABI std::uint32_t tl_GetSystemDirectoryW(std::uint16_t* const buffer,
                                              const std::uint32_t size) noexcept {
    static constexpr std::u16string_view kSystemDirectory = u"C:\\Windows\\System32";
    const std::uint32_t required = static_cast<std::uint32_t>(kSystemDirectory.size() + 1U);
    if (buffer == nullptr || size == 0) {
        set_last_error(abi::kErrorSuccess);
        return required;
    }
    if (size < required ||
        !mapped_guest_range(buffer, static_cast<std::size_t>(size) * sizeof(*buffer), true)) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return required;
    }
    std::copy(kSystemDirectory.begin(), kSystemDirectory.end(), buffer);
    buffer[kSystemDirectory.size()] = 0;
    set_last_error(abi::kErrorSuccess);
    trace_process_console("process-context", "system-directory", "C:\\Windows\\System32");
    return static_cast<std::uint32_t>(kSystemDirectory.size());
}

TL_MSABI int tl_IsProcessorFeaturePresent(const std::uint32_t processor_feature) noexcept {
    const bool present = processor_feature == abi::kPfCompareExchangeDouble ||
                         processor_feature == abi::kPfMmxInstructionsAvailable ||
                         processor_feature == abi::kPfXmmiInstructionsAvailable ||
                         processor_feature == abi::kPfRdtscInstructionAvailable ||
                         processor_feature == abi::kPfPaeEnabled ||
                         processor_feature == abi::kPfXmmi64InstructionsAvailable ||
                         processor_feature == abi::kPfNxEnabled;
    set_last_error(abi::kErrorSuccess);
    trace_process_console("process-context", "processor-feature",
                          std::to_string(processor_feature));
    return present ? 1 : 0;
}

TL_MSABI std::uint32_t tl_GetLastError() noexcept {
    if (g_current_teb != nullptr) {
        return g_current_teb->last_error_value;
    }
    return g_last_error;
}

TL_MSABI void tl_SetLastError(const std::uint32_t error_code) noexcept {
    set_last_error(error_code);
}

TL_MSABI int tl_GetVersionExA(void* version_information) noexcept {
    if (version_information == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // Ler dwOSVersionInfoSize (primeiro DWORD)
    if (!mapped_guest_range(version_information, sizeof(std::uint32_t), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::uint32_t size = 0;
    std::memcpy(&size, version_information, sizeof(size));
    if (size < 20U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(version_information, size, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // Windows 10 19044
    constexpr std::uint32_t kMajor = 10;
    constexpr std::uint32_t kMinor = 0;
    constexpr std::uint32_t kBuild = 19044;
    constexpr std::uint32_t kPlatform = abi::kVerPlatformWin32Nt; // 2
    auto* base = static_cast<std::uint8_t*>(version_information);
    // Preenche campos comuns (offsets fixos Windows)
    auto write_u32 = [&](std::size_t off, std::uint32_t v) {
        if (off + 4 <= size) std::memcpy(base + off, &v, 4);
    };
    write_u32(4, kMajor);
    write_u32(8, kMinor);
    write_u32(12, kBuild);
    write_u32(16, kPlatform);
    // szCSDVersion (A): offset 20, 128 bytes char
    if (size >= 148U) {
        std::memset(base + 20, 0, 128);
        if (size >= 156U) {
            // OSVERSIONINFOEXA
            std::uint16_t wMajor = 0;
            std::uint16_t wMinor = 0;
            std::uint16_t suite = 0;
            std::uint8_t prod = static_cast<std::uint8_t>(abi::kVerNtWorkstation);
            std::uint8_t reserved = 0;
            if (148 + 2 <= size) std::memcpy(base + 148, &wMajor, 2);
            if (150 + 2 <= size) std::memcpy(base + 150, &wMinor, 2);
            if (152 + 2 <= size) std::memcpy(base + 152, &suite, 2);
            if (154 + 1 <= size) std::memcpy(base + 154, &prod, 1);
            if (155 + 1 <= size) std::memcpy(base + 155, &reserved, 1);
            // padding já zero se houver
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetVersionExW(void* version_information) noexcept {
    if (version_information == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(version_information, sizeof(std::uint32_t), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::uint32_t size = 0;
    std::memcpy(&size, version_information, sizeof(size));
    if (size < 20U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(version_information, size, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    constexpr std::uint32_t kMajor = 10;
    constexpr std::uint32_t kMinor = 0;
    constexpr std::uint32_t kBuild = 19044;
    constexpr std::uint32_t kPlatform = abi::kVerPlatformWin32Nt;
    auto* base = static_cast<std::uint8_t*>(version_information);
    auto write_u32 = [&](std::size_t off, std::uint32_t v) {
        if (off + 4 <= size) std::memcpy(base + off, &v, 4);
    };
    write_u32(4, kMajor);
    write_u32(8, kMinor);
    write_u32(12, kBuild);
    write_u32(16, kPlatform);
    if (size >= 276U) {
        // szCSDVersion W: 128 WCHAR (256 bytes) a partir de 20
        std::memset(base + 20, 0, 256);
        if (size >= 284U) {
            std::uint16_t wMajor = 0;
            std::uint16_t wMinor = 0;
            std::uint16_t suite = 0;
            std::uint8_t prod = static_cast<std::uint8_t>(abi::kVerNtWorkstation);
            std::uint8_t reserved = 0;
            if (276 + 2 <= size) std::memcpy(base + 276, &wMajor, 2);
            if (278 + 2 <= size) std::memcpy(base + 278, &wMinor, 2);
            if (280 + 2 <= size) std::memcpy(base + 280, &suite, 2);
            if (282 + 1 <= size) std::memcpy(base + 282, &prod, 1);
            if (283 + 1 <= size) std::memcpy(base + 283, &reserved, 1);
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_VerifyVersionInfoW(void* version_information, std::uint32_t type_mask,
                                   std::uint64_t condition_mask) noexcept {
    (void)condition_mask;
    if (version_information == nullptr || type_mask == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // Ler size para validar range
    if (!mapped_guest_range(version_information, sizeof(std::uint32_t), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::uint32_t size = 0;
    std::memcpy(&size, version_information, sizeof(size));
    if (size < 20U || !mapped_guest_range(version_information, size, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // Stub: sempre considera versão compatível (Windows 10). App quer saber se está em Win10+.
    // Retorna TRUE (1)
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint64_t tl_VerSetConditionMask(std::uint64_t condition_mask, std::uint32_t type_mask,
                                              std::uint8_t condition) noexcept {
    condition &= 0x07U;
    if (type_mask == 0) return condition_mask;
    for (int i = 0; i < 32; ++i) {
        if (type_mask & (1U << i)) {
            const int shift = i * 3;
            if (shift < 64) {
                condition_mask &= ~ (static_cast<std::uint64_t>(0x07ULL) << shift);
                condition_mask |= (static_cast<std::uint64_t>(condition & 0x07) << shift);
            }
        }
    }
    return condition_mask;
}

TL_MSABI int tl_GetUserDefaultLocaleName(std::uint16_t* locale_name, int locale_name_length) noexcept {
    constexpr std::u16string_view kDefault = u"en-US";
    constexpr int kNeeded = 6; // inclui NUL: e n - U S \0
    if (locale_name == nullptr || locale_name_length == 0) {
        // Retorna tamanho necessário incluindo NUL, como no Windows quando buffer null
        return kNeeded;
    }
    if (locale_name_length < 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(locale_name, static_cast<std::size_t>(locale_name_length) * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (locale_name_length < kNeeded) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    for (std::size_t i = 0; i < 5; ++i) {
        locale_name[i] = static_cast<std::uint16_t>(kDefault[i]);
    }
    locale_name[5] = 0;
    set_last_error(abi::kErrorSuccess);
    return kNeeded;
}

TL_MSABI std::uint32_t tl_LocaleNameToLCID(const std::uint16_t* name, std::uint32_t flags) noexcept {
    (void)flags;
    if (name == nullptr || !mapped_guest_wstring(name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (name[0] == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string utf8 = util::wide_to_utf8(name);
    std::string lower;
    lower.reserve(utf8.size());
    for (char c : utf8) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    // remove trailing spaces?
    if (lower == "en-us") {
        set_last_error(abi::kErrorSuccess);
        return 0x0409U;
    }
    if (lower == "pt-br") {
        set_last_error(abi::kErrorSuccess);
        return 0x0416U;
    }
    if (lower == "en") {
        set_last_error(abi::kErrorSuccess);
        return 0x0009U;
    }
    if (lower == "pt") {
        set_last_error(abi::kErrorSuccess);
        return 0x0016U;
    }
    set_last_error(abi::kErrorInvalidParameter);
    return 0;
}

TL_MSABI int tl_MulDiv(const int number, const int numerator, const int denominator) noexcept {
    if (denominator == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return -1;
    }
    const std::int64_t product = static_cast<std::int64_t>(number) * numerator;
    const std::int64_t divisor = denominator;
    const std::int64_t adjustment = product >= 0 ? divisor / 2 : -(divisor / 2);
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>((product + adjustment) / divisor);
}

TL_MSABI std::uint32_t tl_FormatMessageW(const std::uint32_t flags, const void* source,
                                          const std::uint32_t message_id, const std::uint32_t language_id,
                                          std::uint16_t* buffer, const std::uint32_t size,
                                          const void* arguments) noexcept {
    (void)source;
    (void)language_id;
    (void)arguments;
    const bool allocate = (flags & abi::kFormatMessageAllocateBuffer) != 0;
    const std::string text = format_message_text(flags, message_id);
    const std::u16string wide_text = util::utf8_to_wide(text);
    const std::size_t required = wide_text.size() + 1;
    if (allocate) {
        if (buffer == nullptr || !mapped_guest_range(buffer, sizeof(std::uint16_t*), true)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        auto* storage = static_cast<std::uint16_t*>(std::malloc(required * sizeof(std::uint16_t)));
        if (storage == nullptr) {
            set_last_error(abi::kErrorNotEnoughMemory);
            return 0;
        }
        std::copy(wide_text.begin(), wide_text.end(), storage);
        storage[wide_text.size()] = 0;
        if (!register_local_free_block(storage)) {
            std::free(storage);
            set_last_error(abi::kErrorNotEnoughMemory);
            return 0;
        }
        auto** output = reinterpret_cast<std::uint16_t**>(buffer);
        *output = storage;
        set_last_error(abi::kErrorSuccess);
        return static_cast<std::uint32_t>(wide_text.size());
    }
    if (buffer == nullptr || size == 0U || required > size ||
        !mapped_guest_range(buffer, static_cast<std::size_t>(size) * sizeof(*buffer), true)) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    std::copy(wide_text.begin(), wide_text.end(), buffer);
    buffer[wide_text.size()] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(wide_text.size());
}

TL_MSABI std::uint32_t tl_FormatMessageA(const std::uint32_t flags, const void* source,
                                          const std::uint32_t message_id, const std::uint32_t language_id,
                                          char* buffer, const std::uint32_t size,
                                          const void* arguments) noexcept {
    (void)source;
    (void)language_id;
    (void)arguments;
    const bool allocate = (flags & abi::kFormatMessageAllocateBuffer) != 0;
    const std::string text = format_message_text(flags, message_id);
    const std::size_t required = text.size() + 1;
    if (allocate) {
        if (buffer == nullptr || !mapped_guest_range(buffer, sizeof(char*), true)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        auto* storage = static_cast<char*>(std::malloc(required));
        if (storage == nullptr) {
            set_last_error(abi::kErrorNotEnoughMemory);
            return 0;
        }
        std::copy(text.begin(), text.end(), storage);
        storage[text.size()] = '\0';
        if (!register_local_free_block(storage)) {
            std::free(storage);
            set_last_error(abi::kErrorNotEnoughMemory);
            return 0;
        }
        *reinterpret_cast<char**>(buffer) = storage;
        set_last_error(abi::kErrorSuccess);
        return static_cast<std::uint32_t>(text.size());
    }
    if (buffer == nullptr || size == 0U || required > size ||
        !mapped_guest_range(buffer, static_cast<std::size_t>(size), true)) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    std::copy(text.begin(), text.end(), buffer);
    buffer[text.size()] = '\0';
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(text.size());
}

TL_MSABI void tl_GetSystemInfo(void* system_info) noexcept {
    if (system_info == nullptr || !mapped_guest_range(system_info, sizeof(abi::GuestSystemInfo), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    auto* si = static_cast<abi::GuestSystemInfo*>(system_info);
    *si = {};
    si->processor_architecture = 9;  // PROCESSOR_ARCHITECTURE_AMD64
    si->page_size = 4096;
    si->minimum_application_address = reinterpret_cast<void*>(0x10000);
    si->maximum_application_address = reinterpret_cast<void*>(0x7FFFFFFF0000ULL);
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocs < 1) nprocs = 1;
    si->number_of_processors = static_cast<std::uint32_t>(nprocs);
    si->active_processor_mask = (1ULL << std::min<long>(nprocs, 64)) - 1ULL;
    si->allocation_granularity = 65536;
    si->processor_type = 8664; // PROCESSOR_AMD_X8664
    si->processor_level = 6;
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI void tl_GetNativeSystemInfo(void* system_info) noexcept {
    tl_GetSystemInfo(system_info);
}

TL_MSABI int tl_CompareStringA(const std::uint32_t, const std::uint32_t flags,
                               const char* string1, const int count1,
                               const char* string2, const int count2) noexcept {
    if (string1 == nullptr || string2 == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string_view s1(string1, count1 >= 0 ? static_cast<std::size_t>(count1) : std::strlen(string1));
    const std::string_view s2(string2, count2 >= 0 ? static_cast<std::size_t>(count2) : std::strlen(string2));
    const bool ignore_case = (flags & 0x00000001) != 0;
    int res = 0;
    if (ignore_case) {
        res = util::ascii_case_insensitive_compare(s1, s2);
    } else {
        res = s1.compare(s2);
    }
    set_last_error(abi::kErrorSuccess);
    return (res < 0) ? 1 : ((res > 0) ? 3 : 2);
}

TL_MSABI int tl_CompareStringW(const std::uint32_t locale, const std::uint32_t flags,
                               const std::uint16_t* string1, const int count1,
                               const std::uint16_t* string2, const int count2) noexcept {
    const std::size_t length1 = count1 >= 0 ? static_cast<std::size_t>(count1) : 65535U;
    const std::size_t length2 = count2 >= 0 ? static_cast<std::size_t>(count2) : 65535U;
    const std::string utf8_1 = (string1 != nullptr) ? util::wide_to_utf8(string1, length1) : "";
    const std::string utf8_2 = (string2 != nullptr) ? util::wide_to_utf8(string2, length2) : "";
    return tl_CompareStringA(locale, flags, utf8_1.c_str(), -1, utf8_2.c_str(), -1);
}

TL_MSABI std::uint32_t tl_GetUserDefaultLCID() noexcept {
    return 0x0409U;
}

TL_MSABI std::uint32_t tl_GetSystemDefaultLCID() noexcept {
    return 0x0409U;
}

TL_MSABI int tl_GetComputerNameA(char* buffer, std::uint32_t* size) noexcept {
    if (buffer == nullptr || size == nullptr || *size == 0 || !mapped_guest_range(buffer, *size, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char host[256]{};
    if (gethostname(host, sizeof(host)) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    const std::size_t len = std::strlen(host);
    if (*size <= len) {
        *size = static_cast<std::uint32_t>(len + 1);
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    std::memcpy(buffer, host, len + 1);
    *size = static_cast<std::uint32_t>(len);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetComputerNameW(std::uint16_t* buffer, std::uint32_t* size) noexcept {
    if (buffer == nullptr || size == nullptr || *size == 0 || !mapped_guest_range(buffer, *size * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char host[256]{};
    if (gethostname(host, sizeof(host)) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    const std::u16string u16 = util::utf8_to_wide(host);
    if (*size <= u16.size()) {
        *size = static_cast<std::uint32_t>(u16.size() + 1);
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    std::copy(u16.begin(), u16.end(), buffer);
    buffer[u16.size()] = 0;
    *size = static_cast<std::uint32_t>(u16.size());
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_FoldStringW(const std::uint32_t map_flags, const std::uint16_t* const src_str,
                            const int cch_src, std::uint16_t* const dest_str,
                            const int cch_dest) noexcept {
    (void)map_flags;
    if (src_str == nullptr || !mapped_guest_wstring(src_str)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::size_t null_term_len = 0;
    while (src_str[null_term_len] != 0) {
        ++null_term_len;
    }
    const std::size_t src_len = (cch_src < 0) ? (null_term_len + 1) : static_cast<std::size_t>(cch_src);
    if (cch_dest == 0) {
        return static_cast<int>(src_len);
    }
    if (cch_dest < 0 || dest_str == nullptr ||
        !mapped_guest_range(dest_str, static_cast<std::size_t>(cch_dest) * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::size_t to_copy = std::min(static_cast<std::size_t>(cch_dest), src_len);
    for (std::size_t i = 0; i < to_copy; ++i) {
        dest_str[i] = src_str[i];
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(to_copy);
}

TL_MSABI int tl_IsDBCSLeadByte(const std::uint8_t test_char) noexcept {
    (void)test_char;
    return 0;
}

TL_MSABI int tl_GetNumberFormatW(const std::uint32_t locale, const std::uint32_t flags,
                                 const std::uint16_t* const value, const void* const format,
                                 std::uint16_t* const number_str, const int cch_number) noexcept {
    (void)locale;
    (void)flags;
    (void)format;
    if (value == nullptr || !mapped_guest_wstring(value)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::size_t val_len = 0;
    while (value[val_len] != 0) {
        ++val_len;
    }
    const std::size_t len = val_len + 1;
    if (cch_number == 0) {
        return static_cast<int>(len);
    }
    if (cch_number < 0 || number_str == nullptr ||
        !mapped_guest_range(number_str, static_cast<std::size_t>(cch_number) * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::size_t to_copy = std::min(static_cast<std::size_t>(cch_number), len);
    for (std::size_t i = 0; i < to_copy; ++i) {
        number_str[i] = value[i];
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(to_copy);
}

TL_MSABI std::uint32_t tl_GetVersion(void) noexcept {
    // Windows 7 / NT 6.1 (0x00060001)
    return 0x00060001U;
}

TL_MSABI std::uint16_t tl_GetUserDefaultUILanguage(void) noexcept {
    return 0x0409; // en-US
}

TL_MSABI std::uint16_t tl_GetSystemDefaultLangID() noexcept {
    return 0x0409; // en-US
}

TL_MSABI std::uint16_t tl_GetUserDefaultLangID() noexcept {
    return 0x0409; // en-US
}

TL_MSABI std::uint32_t tl_GetWindowsDirectoryW(std::uint16_t* const buffer, const std::uint32_t size) noexcept {
    static const std::uint16_t kWinDir[] = {'C', ':', '\\', 'W', 'i', 'n', 'd', 'o', 'w', 's', 0};
    constexpr std::uint32_t kLen = 10;
    if (buffer == nullptr || size <= kLen) {
        return kLen + 1;
    }
    if (!mapped_guest_range(buffer, (kLen + 1) * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::memcpy(buffer, kWinDir, (kLen + 1) * sizeof(std::uint16_t));
    set_last_error(abi::kErrorSuccess);
    return kLen;
}

TL_MSABI int tl_lstrlenW(const std::uint16_t* const str) noexcept {
    if (str == nullptr) {
        return 0;
    }
    int len = 0;
    while (str[len] != 0) {
        ++len;
    }
    return len;
}

TL_MSABI int tl_CompareStringEx(const wchar_t* const locale_name, const std::uint32_t flags,
                                const wchar_t* const string1, const int count1,
                                const wchar_t* const string2, const int count2,
                                void* const version_information, void* const reserved, const std::intptr_t param) noexcept {
    (void)locale_name;
    (void)flags;
    (void)version_information;
    (void)reserved;
    (void)param;
    if (string1 == nullptr || string2 == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    int cmp = 0;
    if (count1 < 0 || count2 < 0) {
        cmp = std::wcscmp(string1, string2);
    } else {
        cmp = std::wcsncmp(string1, string2, static_cast<std::size_t>(std::min(count1, count2)));
        if (cmp == 0 && count1 != count2) {
            cmp = count1 < count2 ? -1 : 1;
        }
    }
    return cmp < 0 ? 1 : (cmp == 0 ? 2 : 3); // 1 = CSTR_LESS_THAN, 2 = CSTR_EQUAL, 3 = CSTR_GREATER_THAN
}

TL_MSABI std::uint32_t tl_GetSystemDirectoryA(char* const buffer, const std::uint32_t size) noexcept {
    if (buffer == nullptr || size == 0 || !mapped_guest_range(buffer, size, true)) {
        return 0;
    }
    const char sys[] = "C:\\Windows\\System32";
    const std::uint32_t len = static_cast<std::uint32_t>(std::strlen(sys));
    if (size <= len) {
        return len + 1;
    }
    std::memcpy(buffer, sys, len + 1);
    set_last_error(abi::kErrorSuccess);
    return len;
}

TL_MSABI std::uint32_t tl_GetSystemFirmwareTable(const std::uint32_t firmware_table_provider_signature,
                                                 const std::uint32_t firmware_table_id,
                                                 void* const firmware_table_buffer,
                                                 const std::uint32_t buffer_size) noexcept {
    (void)firmware_table_provider_signature;
    (void)firmware_table_id;
    (void)firmware_table_buffer;
    (void)buffer_size;
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI int tl_GetLocaleInfoA(const std::uint32_t lcid, const std::uint32_t lctype, char* const lcdata, const int cch_data) noexcept {
    (void)lcid;
    (void)lctype;
    if (cch_data > 0 && lcdata != nullptr && mapped_guest_range(lcdata, static_cast<std::size_t>(cch_data), true)) {
        std::strncpy(lcdata, "0409", static_cast<std::size_t>(cch_data) - 1);
        lcdata[cch_data - 1] = '\0';
        return static_cast<int>(std::strlen(lcdata) + 1);
    }
    return 5;
}

TL_MSABI std::uint32_t tl_GetWindowsDirectoryA(char* const buffer, const std::uint32_t size) noexcept {
    const char win_dir[] = "C:\\Windows";
    const std::uint32_t len = sizeof(win_dir) - 1;
    if (size <= len || buffer == nullptr || !mapped_guest_range(buffer, size, true)) {
        return len + 1;
    }
    std::memcpy(buffer, win_dir, len + 1);
    return len;
}

TL_MSABI int tl_GetTimeFormatEx(const wchar_t* const lpLocaleName, const std::uint32_t dwFlags, const void* const lpTime, const wchar_t* const lpFormat, wchar_t* const lpTimeStr, const int cchTime) noexcept {
    (void)lpLocaleName;
    (void)dwFlags;
    (void)lpTime;
    (void)lpFormat;
    const wchar_t dummy[] = L"12:00:00";
    const int len = sizeof(dummy) / sizeof(wchar_t);
    if (lpTimeStr == nullptr || cchTime == 0) return len;
    if (cchTime < len) {
        set_last_error(122);
        return 0;
    }
    std::memcpy(lpTimeStr, dummy, sizeof(dummy));
    return len;
}

TL_MSABI int tl_GetDateFormatEx(const wchar_t* const lpLocaleName, const std::uint32_t dwFlags, const void* const lpDate, const wchar_t* const lpFormat, wchar_t* const lpDateStr, const int cchDate, const wchar_t* const lpCalendar) noexcept {
    (void)lpLocaleName;
    (void)dwFlags;
    (void)lpDate;
    (void)lpFormat;
    (void)lpCalendar;
    const wchar_t dummy[] = L"2026-08-28";
    const int len = sizeof(dummy) / sizeof(wchar_t);
    if (lpDateStr == nullptr || cchDate == 0) return len;
    if (cchDate < len) {
        set_last_error(122);
        return 0;
    }
    std::memcpy(lpDateStr, dummy, sizeof(dummy));
    return len;
}

TL_MSABI std::uint16_t* tl_lstrcpynW(std::uint16_t* const lpString1,
                                     const std::uint16_t* const lpString2,
                                     const int iMaxLength) noexcept {
    if (lpString1 == nullptr || iMaxLength <= 0) return lpString1;
    if (lpString2 == nullptr) {
        lpString1[0] = 0;
        return lpString1;
    }
    int i = 0;
    while (i < iMaxLength - 1 && lpString2[i] != 0) {
        lpString1[i] = lpString2[i];
        ++i;
    }
    lpString1[i] = 0;
    return lpString1;
}

TL_MSABI int tl_lstrcmpiA(const char* const lpString1, const char* const lpString2) noexcept {
    if (lpString1 == lpString2) return 0;
    if (lpString1 == nullptr) return -1;
    if (lpString2 == nullptr) return 1;
    return strcasecmp(lpString1, lpString2);
}

TL_MSABI char* tl_lstrcpynA(char* const lpString1, const char* const lpString2, const int iMaxLength) noexcept {
    if (lpString1 == nullptr || iMaxLength <= 0) return lpString1;
    if (lpString2 == nullptr) {
        lpString1[0] = 0;
        return lpString1;
    }
    int i = 0;
    while (i < iMaxLength - 1 && lpString2[i] != 0) {
        lpString1[i] = lpString2[i];
        ++i;
    }
    lpString1[i] = 0;
    return lpString1;
}

TL_MSABI int tl_GetStringTypeExW(const std::uint32_t Locale, const std::uint32_t dwInfoType, const wchar_t* const lpSrcStr, const int cchSrc, std::uint16_t* const lpCharType) noexcept {
    (void)Locale;
    (void)dwInfoType;
    (void)lpSrcStr;
    if (lpCharType == nullptr) return 0;
    const int count = cchSrc > 0 ? cchSrc : 1;
    for (int i = 0; i < count; ++i) {
        lpCharType[i] = 0x0001; // C1_UPPER/ALPHA
    }
    return 1;
}

TL_MSABI int tl_LCMapStringA(const std::uint32_t Locale, const std::uint32_t dwMapFlags, const char* const lpSrcStr, const int cchSrc, char* const lpDestStr, const int cchDest) noexcept {
    (void)Locale;
    (void)dwMapFlags;
    if (lpSrcStr == nullptr) return 0;
    const int len = cchSrc > 0 ? cchSrc : static_cast<int>(std::strlen(lpSrcStr) + 1);
    if (lpDestStr == nullptr || cchDest == 0) return len;
    const int copy_len = std::min(len, cchDest);
    std::memcpy(lpDestStr, lpSrcStr, static_cast<std::size_t>(copy_len));
    return copy_len;
}

TL_MSABI int tl_GetStringTypeExA(const std::uint32_t Locale, const std::uint32_t dwInfoType, const char* const lpSrcStr, const int cchSrc, std::uint16_t* const lpCharType) noexcept {
    (void)Locale;
    (void)dwInfoType;
    (void)lpSrcStr;
    if (lpCharType == nullptr) return 0;
    const int count = cchSrc > 0 ? cchSrc : 1;
    for (int i = 0; i < count; ++i) {
        lpCharType[i] = 0x0001;
    }
    return 1;
}

TL_MSABI std::uint16_t* tl_lstrcpyW(std::uint16_t* const lpString1,
                                    const std::uint16_t* const lpString2) noexcept {
    if (lpString1 == nullptr) return nullptr;
    if (lpString2 == nullptr) {
        lpString1[0] = 0;
        return lpString1;
    }
    std::size_t i = 0;
    while (lpString2[i] != 0) {
        lpString1[i] = lpString2[i];
        ++i;
    }
    lpString1[i] = 0;
    return lpString1;
}

TL_MSABI int tl_lstrcmpW(const std::uint16_t* const lpString1,
                         const std::uint16_t* const lpString2) noexcept {
    if (lpString1 == lpString2) return 0;
    if (lpString1 == nullptr) return -1;
    if (lpString2 == nullptr) return 1;
    std::size_t i = 0;
    while (lpString1[i] != 0 && lpString2[i] != 0) {
        if (lpString1[i] != lpString2[i]) {
            return lpString1[i] < lpString2[i] ? -1 : 1;
        }
        ++i;
    }
    if (lpString1[i] == lpString2[i]) return 0;
    return lpString1[i] < lpString2[i] ? -1 : 1;
}

TL_MSABI int tl_lstrcmpiW(const std::uint16_t* const lpString1,
                          const std::uint16_t* const lpString2) noexcept {
    if (lpString1 == lpString2) return 0;
    if (lpString1 == nullptr) return -1;
    if (lpString2 == nullptr) return 1;
    std::size_t i = 0;
    while (lpString1[i] != 0 && lpString2[i] != 0) {
        const auto c1 = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(lpString1[i])));
        const auto c2 = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(lpString2[i])));
        if (c1 != c2) {
            return c1 < c2 ? -1 : 1;
        }
        ++i;
    }
    if (lpString1[i] == lpString2[i]) return 0;
    return lpString1[i] < lpString2[i] ? -1 : 1;
}

TL_MSABI int tl_IsDBCSLeadByteEx(std::uint32_t /*code_page*/, std::uint8_t /*test_char*/) noexcept {
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI int tl_MultiByteToWideChar(std::uint32_t code_page, std::uint32_t flags,
                                    const char* mb_str, int mb_count,
                                    std::uint16_t* wide_str, int wide_count) noexcept {
    const bool supported_page = code_page == abi::kCpAcp || code_page == abi::kCp1252 ||
                                code_page == abi::kCpOem || code_page == abi::kCp437 ||
                                code_page == abi::kCpUtf8;
    if (mb_str == nullptr || mb_count == 0 || mb_count < -1 || !supported_page ||
        (flags & ~(abi::kMbPrecomposed | abi::kMbUseGlyphChars |
                   abi::kMbErrInvalidChars)) != 0U ||
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
        std::uint32_t codepoint = decode_multibyte(
            code_page, bytes, byte_count, index, (flags & abi::kMbUseGlyphChars) != 0U);
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
        std::uint32_t codepoint = decode_multibyte(
            code_page, bytes, byte_count, index, (flags & abi::kMbUseGlyphChars) != 0U);
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

}  // extern "C"
}  // namespace tradutorlinux
