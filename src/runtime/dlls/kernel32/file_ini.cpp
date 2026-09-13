#include "kernel32_file_internal.hpp"

#include "tradutorlinux/util/basics.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <cstring>
#include <limits>

namespace tradutorlinux {
using namespace file_internal;

namespace {

constexpr std::size_t kProfileInputLimit = 4096U;

bool copy_optional_cstring(const char* const source, std::string& destination) noexcept {
    if (source == nullptr) {
        destination.clear();
        return true;
    }
    return runtime::copy_guest_cstring(source, kProfileInputLimit, destination);
}

bool copy_optional_wstring(const std::uint16_t* const source,
                           std::u16string& destination) noexcept {
    if (source == nullptr) {
        destination.clear();
        return true;
    }
    return runtime::copy_guest_wstring(source, kProfileInputLimit, destination);
}

bool write_profile_bytes(void* const destination, const void* const source,
                         const std::size_t length, const std::size_t capacity) noexcept {
    if (destination == nullptr || capacity == 0U) return false;
    const std::size_t copy_length = std::min(length, capacity - 1U);
    if (copy_length > 0U &&
        runtime::write_guest_memory(destination, source, copy_length).status !=
            runtime::GuestMemoryAccessStatus::Success) {
        return false;
    }
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(destination);
    if (copy_length > std::numeric_limits<std::uintptr_t>::max() - base) return false;
    const std::uintptr_t terminator_address = base + copy_length;
    const std::uint8_t terminator = 0U;
    return runtime::write_guest_memory(reinterpret_cast<void*>(terminator_address), &terminator,
                                       sizeof(terminator)).status ==
           runtime::GuestMemoryAccessStatus::Success;
}

bool write_profile_wstring(std::uint16_t* const destination, const std::u16string& value,
                           const std::uint32_t capacity) noexcept {
    if (destination == nullptr || capacity == 0U) return false;
    const std::size_t length = std::min<std::size_t>(value.size(), capacity - 1U);
    if (length > 0U &&
        runtime::write_guest_memory(destination, value.data(), length * sizeof(std::uint16_t)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
        return false;
    }
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(destination);
    if (length > (std::numeric_limits<std::uintptr_t>::max() - base) / sizeof(std::uint16_t)) {
        return false;
    }
    const std::uint16_t terminator = 0U;
    return runtime::write_guest_memory(
               reinterpret_cast<void*>(base + length * sizeof(std::uint16_t)), &terminator,
               sizeof(terminator)).status == runtime::GuestMemoryAccessStatus::Success;
}

}  // namespace

extern "C" {
TL_MSABI std::uint32_t tl_GetPrivateProfileStringA(const char* app_name, const char* key_name,
                                                   const char* default_val, char* returned_string,
                                                   const std::uint32_t size, const char* file_name) noexcept {
    if (returned_string == nullptr || size == 0U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string fallback;
    std::string guest_file;
    std::string target_section;
    std::string target_key;
    if (!copy_optional_cstring(default_val, fallback) ||
        !copy_optional_cstring(file_name, guest_file) ||
        !copy_optional_cstring(app_name, target_section) ||
        !copy_optional_cstring(key_name, target_key)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (file_name == nullptr) {
        if (!write_profile_bytes(returned_string, fallback.data(), fallback.size(), size)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        set_last_error(abi::kErrorSuccess);
        return static_cast<std::uint32_t>(std::min<std::size_t>(fallback.size(), size - 1U));
    }

    char normalized[4096]{};
    if (!translate_windows_path(guest_file.c_str(), normalized, sizeof(normalized))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::ifstream file{normalized};
    if (!file) {
        if (!write_profile_bytes(returned_string, fallback.data(), fallback.size(), size)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        set_last_error(abi::kErrorSuccess);
        return static_cast<std::uint32_t>(std::min<std::size_t>(fallback.size(), size - 1U));
    }
    std::string current_section;
    std::string line;
    std::string found_val = fallback;

    while (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            current_section = line.substr(1, line.size() - 2);
            continue;
        }
        if (!target_section.empty() && !util::ascii_iequals(current_section, target_section)) {
            continue;
        }
        const auto eq = line.find('=');
        if (eq != std::string::npos) {
            std::string k = line.substr(0, eq);
            std::string v = line.substr(eq + 1);
            k.erase(k.find_last_not_of(" \t") + 1);
            v.erase(0, v.find_first_not_of(" \t"));
            if (util::ascii_iequals(k, target_key)) {
                found_val = v;
                break;
            }
        }
    }
    if (!write_profile_bytes(returned_string, found_val.data(), found_val.size(), size)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(std::min<std::size_t>(found_val.size(), size - 1U));
}
TL_MSABI std::uint32_t tl_GetPrivateProfileStringW(const std::uint16_t* app_name, const std::uint16_t* key_name,
                                                   const std::uint16_t* default_val, std::uint16_t* returned_string,
                                                   const std::uint32_t size, const std::uint16_t* file_name) noexcept {
    if (returned_string == nullptr || size == 0U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::u16string guest_app;
    std::u16string guest_key;
    std::u16string guest_default;
    std::u16string guest_file;
    if (!copy_optional_wstring(app_name, guest_app) ||
        !copy_optional_wstring(key_name, guest_key) ||
        !copy_optional_wstring(default_val, guest_default) ||
        !copy_optional_wstring(file_name, guest_file)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string utf8_app = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_app.data()), guest_app.size());
    const std::string utf8_key = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_key.data()), guest_key.size());
    const std::string utf8_def = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_default.data()), guest_default.size());
    const std::string utf8_file = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_file.data()), guest_file.size());
    char buf[4096]{};
    const std::uint32_t result = tl_GetPrivateProfileStringA(
                                app_name == nullptr ? nullptr : utf8_app.c_str(),
                                key_name == nullptr ? nullptr : utf8_key.c_str(),
                                default_val == nullptr ? nullptr : utf8_def.c_str(),
                                buf, sizeof(buf),
                                file_name == nullptr ? nullptr : utf8_file.c_str());
    if (result == 0U && tl_GetLastError() == abi::kErrorInvalidParameter) return 0U;
    const std::u16string u16 = util::utf8_to_wide(std::string_view{buf});
    const std::size_t len = std::min<std::size_t>(u16.size(), size - 1U);
    if (!write_profile_wstring(returned_string, u16, size)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0U;
    }
    return static_cast<std::uint32_t>(len);
}
TL_MSABI std::uint32_t tl_GetPrivateProfileIntA(const char* app_name, const char* key_name,
                                                const int default_val, const char* file_name) noexcept {
    char buf[64]{};
    tl_GetPrivateProfileStringA(app_name, key_name, std::to_string(default_val).c_str(), buf, sizeof(buf), file_name);
    return static_cast<std::uint32_t>(std::atoi(buf));
}
TL_MSABI std::uint32_t tl_GetPrivateProfileIntW(const std::uint16_t* app_name, const std::uint16_t* key_name,
                                                const int default_val, const std::uint16_t* file_name) noexcept {
    std::uint16_t buf[64]{};
    std::u16string def_u16 = util::utf8_to_wide(std::to_string(default_val));
    tl_GetPrivateProfileStringW(app_name, key_name,
                                reinterpret_cast<const std::uint16_t*>(def_u16.c_str()), buf, 64,
                                file_name);
    return static_cast<std::uint32_t>(std::atoi(util::wide_to_utf8(buf).c_str()));
}
TL_MSABI int tl_WritePrivateProfileStringA(const char* app_name, const char* key_name,
                                           const char* string_val, const char* file_name) noexcept {
    (void)app_name;
    (void)key_name;
    (void)string_val;
    (void)file_name;
    set_last_error(abi::kErrorSuccess);
    return 1;
}
TL_MSABI int tl_WritePrivateProfileStringW(const std::uint16_t* app_name, const std::uint16_t* key_name,
                                           const std::uint16_t* string_val, const std::uint16_t* file_name) noexcept {
    (void)app_name;
    (void)key_name;
    (void)string_val;
    (void)file_name;
    set_last_error(abi::kErrorSuccess);
    return 1;
}
TL_MSABI std::uint32_t tl_GetPrivateProfileSectionA(const char* app_name, char* returned_string,
                                                    const std::uint32_t size, const char* file_name) noexcept {
    (void)app_name;
    (void)file_name;
    if (returned_string != nullptr && size >= 2U) {
        const std::uint8_t empty[2]{0U, 0U};
        if (runtime::write_guest_memory(returned_string, empty, sizeof(empty)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            set_last_error(abi::kErrorInvalidParameter);
        }
    }
    return 0;
}
TL_MSABI std::uint32_t tl_GetPrivateProfileSectionW(const std::uint16_t* app_name, std::uint16_t* returned_string,
                                                    const std::uint32_t size, const std::uint16_t* file_name) noexcept {
    (void)app_name;
    (void)file_name;
    if (returned_string != nullptr && size >= 2U) {
        const std::uint16_t empty[2]{0U, 0U};
        if (runtime::write_guest_memory(returned_string, empty, sizeof(empty)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            set_last_error(abi::kErrorInvalidParameter);
        }
    }
    return 0;
}
}
}  // namespace tradutorlinux
