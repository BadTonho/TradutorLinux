#include "kernel32_file_internal.hpp"

#include "tradutorlinux/util/basics.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <cstring>

namespace tradutorlinux {
using namespace file_internal;

extern "C" {
TL_MSABI std::uint32_t tl_GetPrivateProfileStringA(const char* app_name, const char* key_name,
                                                   const char* default_val, char* returned_string,
                                                   const std::uint32_t size, const char* file_name) noexcept {
    if (returned_string == nullptr || size == 0 || !mapped_guest_range(returned_string, size, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string fallback = (default_val != nullptr && mapped_guest_cstring(default_val)) ? default_val : "";
    if (file_name == nullptr || !mapped_guest_cstring(file_name)) {
        std::strncpy(returned_string, fallback.c_str(), size - 1);
        returned_string[size - 1] = '\0';
        return static_cast<std::uint32_t>(std::strlen(returned_string));
    }
    char normalized[4096]{};
    const char* path_to_open = file_name;
    if (translate_windows_path(file_name, normalized, sizeof(normalized))) {
        path_to_open = normalized;
    }
    std::ifstream file{path_to_open};
    if (!file) {
        std::strncpy(returned_string, fallback.c_str(), size - 1);
        returned_string[size - 1] = '\0';
        return static_cast<std::uint32_t>(std::strlen(returned_string));
    }
    std::string target_section = (app_name != nullptr && mapped_guest_cstring(app_name)) ? app_name : "";
    std::string target_key = (key_name != nullptr && mapped_guest_cstring(key_name)) ? key_name : "";
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
    std::strncpy(returned_string, found_val.c_str(), size - 1);
    returned_string[size - 1] = '\0';
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(std::strlen(returned_string));
}
TL_MSABI std::uint32_t tl_GetPrivateProfileStringW(const std::uint16_t* app_name, const std::uint16_t* key_name,
                                                   const std::uint16_t* default_val, std::uint16_t* returned_string,
                                                   const std::uint32_t size, const std::uint16_t* file_name) noexcept {
    if (returned_string == nullptr || size == 0 || !mapped_guest_range(returned_string, size * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string utf8_app = (app_name != nullptr && mapped_guest_wstring(app_name)) ? util::wide_to_utf8(app_name) : "";
    const std::string utf8_key = (key_name != nullptr && mapped_guest_wstring(key_name)) ? util::wide_to_utf8(key_name) : "";
    const std::string utf8_def = (default_val != nullptr && mapped_guest_wstring(default_val)) ? util::wide_to_utf8(default_val) : "";
    const std::string utf8_file = (file_name != nullptr && mapped_guest_wstring(file_name)) ? util::wide_to_utf8(file_name) : "";
    char buf[4096]{};
    tl_GetPrivateProfileStringA(utf8_app.empty() ? nullptr : utf8_app.c_str(),
                                utf8_key.empty() ? nullptr : utf8_key.c_str(),
                                utf8_def.empty() ? nullptr : utf8_def.c_str(),
                                buf, sizeof(buf),
                                utf8_file.empty() ? nullptr : utf8_file.c_str());
    const std::u16string u16 = util::utf8_to_wide(buf);
    const std::size_t len = std::min<std::size_t>(u16.size(), size - 1);
    std::copy(u16.begin(), u16.begin() + static_cast<std::ptrdiff_t>(len), returned_string);
    returned_string[len] = 0;
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
    if (returned_string != nullptr && size >= 2 && mapped_guest_range(returned_string, size, true)) {
        returned_string[0] = '\0';
        returned_string[1] = '\0';
    }
    return 0;
}
TL_MSABI std::uint32_t tl_GetPrivateProfileSectionW(const std::uint16_t* app_name, std::uint16_t* returned_string,
                                                    const std::uint32_t size, const std::uint16_t* file_name) noexcept {
    (void)app_name;
    (void)file_name;
    if (returned_string != nullptr && size >= 2 && mapped_guest_range(returned_string, size * sizeof(std::uint16_t), true)) {
        returned_string[0] = 0;
        returned_string[1] = 0;
    }
    return 0;
}
}
}  // namespace tradutorlinux
