#include "tradutorlinux/runtime/shlwapi.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "tradutorlinux/runtime/memory_validator.hpp"
#include "core/runtime_state_common.hpp"
#include "tradutorlinux/util/basics.hpp"
#include "tradutorlinux/util/unicode.hpp"

namespace tradutorlinux {

namespace {

constexpr std::size_t kMaxShlwapiStringUnits = 4096U;

inline bool copy_guest_string(const char* value, std::string& copy) noexcept {
    return runtime::copy_guest_cstring(value, kMaxShlwapiStringUnits, copy);
}

inline bool copy_guest_string(const std::uint16_t* value, std::u16string& copy) noexcept {
    return runtime::copy_guest_wstring(value, kMaxShlwapiStringUnits, copy);
}

inline bool write_guest_bytes(void* const destination, const void* const source,
                              const std::size_t size) noexcept {
    return runtime::write_guest_memory(destination, source, size).status ==
           runtime::GuestMemoryAccessStatus::Success;
}

template <typename Unit>
[[nodiscard]] Unit* guest_pointer_at(const Unit* const base,
                                     const std::size_t offset) noexcept {
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(base);
    if (base == nullptr || offset > std::numeric_limits<std::uintptr_t>::max() / sizeof(Unit) ||
        address > std::numeric_limits<std::uintptr_t>::max() - offset * sizeof(Unit)) {
        return nullptr;
    }
    return reinterpret_cast<Unit*>(address + offset * sizeof(Unit));
}

}  // namespace

extern "C" {

TL_SHLWAPI_MSABI int tl_PathFileExistsA(const char* path) noexcept {
    std::string guest_path;
    if (!copy_guest_string(path, guest_path) || guest_path.empty()) {
        return 0;
    }
    char normalized[4096]{};
    if (translate_windows_path(guest_path.c_str(), normalized, sizeof(normalized))) {
        struct stat st{};
        if (stat(normalized, &st) == 0) {
            return 1;
        }
    }
    struct stat st{};
    return stat(guest_path.c_str(), &st) == 0 ? 1 : 0;
}

TL_SHLWAPI_MSABI int tl_PathFileExistsW(const std::uint16_t* path) noexcept {
    std::u16string guest_path;
    if (!copy_guest_string(path, guest_path)) {
        return 0;
    }
    char normalized[4096]{};
    const std::string utf8 = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_path.data()), guest_path.size());
    if (translate_windows_path(utf8.c_str(), normalized, sizeof(normalized))) {
        struct stat st{};
        if (stat(normalized, &st) == 0) {
            return 1;
        }
    }
    struct stat st{};
    return stat(utf8.c_str(), &st) == 0 ? 1 : 0;
}

TL_SHLWAPI_MSABI int tl_PathIsDirectoryA(const char* path) noexcept {
    std::string guest_path;
    if (!copy_guest_string(path, guest_path) || guest_path.empty()) {
        return 0;
    }
    char normalized[4096]{};
    if (translate_windows_path(guest_path.c_str(), normalized, sizeof(normalized))) {
        struct stat st{};
        if (stat(normalized, &st) == 0) {
            return S_ISDIR(st.st_mode) ? 1 : 0;
        }
    }
    struct stat st{};
    if (stat(guest_path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode) ? 1 : 0;
    }
    return 0;
}

TL_SHLWAPI_MSABI int tl_PathIsDirectoryW(const std::uint16_t* path) noexcept {
    std::u16string guest_path;
    if (!copy_guest_string(path, guest_path)) {
        return 0;
    }
    char normalized[4096]{};
    const std::string utf8 = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_path.data()), guest_path.size());
    if (translate_windows_path(utf8.c_str(), normalized, sizeof(normalized))) {
        struct stat st{};
        if (stat(normalized, &st) == 0) {
            return S_ISDIR(st.st_mode) ? 1 : 0;
        }
    }
    struct stat st{};
    return stat(utf8.c_str(), &st) == 0 && S_ISDIR(st.st_mode) ? 1 : 0;
}

TL_SHLWAPI_MSABI char* tl_PathCombineA(char* dest, const char* dir, const char* file) noexcept {
    if (dest == nullptr) {
        return nullptr;
    }
    std::string result;
    std::string guest_dir;
    std::string guest_file;
    if (dir != nullptr && !copy_guest_string(dir, guest_dir)) {
        return nullptr;
    }
    if (file != nullptr && !copy_guest_string(file, guest_file)) {
        return nullptr;
    }
    if (!guest_dir.empty()) {
        result = guest_dir;
        if (result.back() != '\\' && result.back() != '/') {
            result += '\\';
        }
    }
    if (!guest_file.empty()) {
        // If file starts with slash/backslash, skip leading slashes if dir present
        std::size_t offset = 0;
        if (!result.empty() && (guest_file[0] == '\\' || guest_file[0] == '/')) {
            offset = 1;
        }
        result += guest_file.substr(offset);
    }
    if (!write_guest_bytes(dest, result.c_str(), result.size() + 1U)) {
        return nullptr;
    }
    return dest;
}

TL_SHLWAPI_MSABI std::uint16_t* tl_PathCombineW(std::uint16_t* dest, const std::uint16_t* dir, const std::uint16_t* file) noexcept {
    if (dest == nullptr) {
        return nullptr;
    }
    std::u16string guest_dir;
    std::u16string guest_file;
    if (dir != nullptr && !copy_guest_string(dir, guest_dir)) {
        return nullptr;
    }
    if (file != nullptr && !copy_guest_string(file, guest_file)) {
        return nullptr;
    }
    const std::string utf8_dir = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_dir.data()), guest_dir.size());
    const std::string utf8_file = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_file.data()), guest_file.size());
    char buf[4096]{};
    if (tl_PathCombineA(buf, utf8_dir.c_str(), utf8_file.c_str()) == nullptr) {
        return nullptr;
    }
    const std::u16string u16 = util::utf8_to_wide(buf);
    if (!write_guest_bytes(dest, u16.data(), (u16.size() + 1U) * sizeof(std::uint16_t))) {
        return nullptr;
    }
    return dest;
}

TL_SHLWAPI_MSABI char* tl_PathFindFileNameA(const char* path) noexcept {
    std::string guest_path;
    if (!copy_guest_string(path, guest_path)) {
        return nullptr;
    }
    std::size_t offset = 0;
    for (std::size_t index = 0; index < guest_path.size(); ++index) {
        if (guest_path[index] == '\\' || guest_path[index] == '/' || guest_path[index] == ':') {
            offset = index + 1U;
        }
    }
    return guest_pointer_at(path, offset);
}

TL_SHLWAPI_MSABI std::uint16_t* tl_PathFindFileNameW(const std::uint16_t* path) noexcept {
    std::u16string guest_path;
    if (!copy_guest_string(path, guest_path)) {
        return nullptr;
    }
    std::size_t offset = 0;
    for (std::size_t index = 0; index < guest_path.size(); ++index) {
        if (guest_path[index] == u'\\' || guest_path[index] == u'/' || guest_path[index] == u':') {
            offset = index + 1U;
        }
    }
    return guest_pointer_at(path, offset);
}

TL_SHLWAPI_MSABI char* tl_PathFindExtensionA(const char* path) noexcept {
    std::string guest_path;
    if (!copy_guest_string(path, guest_path)) {
        return nullptr;
    }
    std::size_t dot_offset = guest_path.size();
    for (std::size_t index = 0; index < guest_path.size(); ++index) {
        if (guest_path[index] == '.') {
            dot_offset = index;
        } else if (guest_path[index] == '\\' || guest_path[index] == '/' || guest_path[index] == ' ') {
            dot_offset = guest_path.size();
        }
    }
    return guest_pointer_at(path, dot_offset);
}

TL_SHLWAPI_MSABI std::uint16_t* tl_PathFindExtensionW(const std::uint16_t* path) noexcept {
    std::u16string guest_path;
    if (!copy_guest_string(path, guest_path)) {
        return nullptr;
    }
    std::size_t dot_offset = guest_path.size();
    for (std::size_t index = 0; index < guest_path.size(); ++index) {
        if (guest_path[index] == u'.') {
            dot_offset = index;
        } else if (guest_path[index] == u'\\' || guest_path[index] == u'/' || guest_path[index] == u' ') {
            dot_offset = guest_path.size();
        }
    }
    return guest_pointer_at(path, dot_offset);
}

TL_SHLWAPI_MSABI int tl_PathRemoveFileSpecA(char* path) noexcept {
    std::string guest_path;
    if (!copy_guest_string(path, guest_path)) {
        return 0;
    }
    std::size_t cut = std::string::npos;
    for (std::size_t index = 0; index < guest_path.size(); ++index) {
        if (guest_path[index] == '\\' || guest_path[index] == '/') {
            cut = index;
        }
    }
    if (cut != std::string::npos) {
        guest_path.resize(cut);
        if (!write_guest_bytes(path, guest_path.c_str(), guest_path.size() + 1U)) {
            return 0;
        }
        return 1;
    }
    return 0;
}

TL_SHLWAPI_MSABI int tl_PathRemoveFileSpecW(std::uint16_t* path) noexcept {
    std::u16string guest_path;
    if (!copy_guest_string(path, guest_path)) {
        return 0;
    }
    std::size_t cut = std::u16string::npos;
    for (std::size_t index = 0; index < guest_path.size(); ++index) {
        if (guest_path[index] == u'\\' || guest_path[index] == u'/') {
            cut = index;
        }
    }
    if (cut != std::u16string::npos) {
        guest_path.resize(cut);
        if (!write_guest_bytes(path, guest_path.data(),
                               (guest_path.size() + 1U) * sizeof(std::uint16_t))) {
            return 0;
        }
        return 1;
    }
    return 0;
}

TL_SHLWAPI_MSABI char* tl_PathAddBackslashA(char* path) noexcept {
    std::string guest_path;
    if (!copy_guest_string(path, guest_path)) {
        return nullptr;
    }
    const std::size_t len = guest_path.size();
    if (len > 0 && guest_path[len - 1] != '\\' && guest_path[len - 1] != '/') {
        guest_path += '\\';
        if (!write_guest_bytes(path, guest_path.c_str(), guest_path.size() + 1U)) {
            return nullptr;
        }
        return guest_pointer_at(path, guest_path.size());
    }
    return guest_pointer_at(path, len);
}

TL_SHLWAPI_MSABI std::uint16_t* tl_PathAddBackslashW(std::uint16_t* path) noexcept {
    std::u16string guest_path;
    if (!copy_guest_string(path, guest_path)) {
        return nullptr;
    }
    const std::size_t len = guest_path.size();
    if (len > 0 && guest_path[len - 1] != u'\\' && guest_path[len - 1] != u'/') {
        guest_path += u'\\';
        if (!write_guest_bytes(path, guest_path.data(),
                               (guest_path.size() + 1U) * sizeof(std::uint16_t))) {
            return nullptr;
        }
        return guest_pointer_at(path, guest_path.size());
    }
    return guest_pointer_at(path, len);
}

TL_SHLWAPI_MSABI char* tl_PathRemoveBackslashA(char* path) noexcept {
    std::string guest_path;
    if (!copy_guest_string(path, guest_path)) {
        return nullptr;
    }
    const std::size_t len = guest_path.size();
    if (len > 0 && (guest_path[len - 1] == '\\' || guest_path[len - 1] == '/')) {
        guest_path.resize(len - 1U);
        if (!write_guest_bytes(path, guest_path.c_str(), guest_path.size() + 1U)) {
            return nullptr;
        }
        return guest_pointer_at(path, guest_path.size());
    }
    return guest_pointer_at(path, len);
}

TL_SHLWAPI_MSABI std::uint16_t* tl_PathRemoveBackslashW(std::uint16_t* path) noexcept {
    std::u16string guest_path;
    if (!copy_guest_string(path, guest_path)) {
        return nullptr;
    }
    const std::size_t len = guest_path.size();
    if (len > 0 && (guest_path[len - 1] == u'\\' || guest_path[len - 1] == u'/')) {
        guest_path.resize(len - 1U);
        if (!write_guest_bytes(path, guest_path.data(),
                               (guest_path.size() + 1U) * sizeof(std::uint16_t))) {
            return nullptr;
        }
        return guest_pointer_at(path, guest_path.size());
    }
    return guest_pointer_at(path, len);
}

TL_SHLWAPI_MSABI char* tl_StrStrIA(const char* first, const char* srch) noexcept {
    std::string guest_first;
    std::string guest_search;
    if (!copy_guest_string(first, guest_first) || !copy_guest_string(srch, guest_search)) {
        return nullptr;
    }
    const std::size_t srch_len = guest_search.size();
    if (srch_len == 0) {
        return guest_pointer_at(first, 0);
    }
    for (std::size_t index = 0; index + srch_len <= guest_first.size(); ++index) {
        if (util::ascii_iequals(std::string_view(guest_first).substr(index, srch_len), guest_search)) {
            return guest_pointer_at(first, index);
        }
    }
    return nullptr;
}

TL_SHLWAPI_MSABI std::uint16_t* tl_StrStrIW(const std::uint16_t* first, const std::uint16_t* srch) noexcept {
    std::u16string guest_first;
    std::u16string guest_search;
    if (!copy_guest_string(first, guest_first) || !copy_guest_string(srch, guest_search)) {
        return nullptr;
    }
    if (guest_search.empty()) {
        return guest_pointer_at(first, 0);
    }
    for (std::size_t index = 0; index + guest_search.size() <= guest_first.size(); ++index) {
        bool equal = true;
        for (std::size_t offset = 0; offset < guest_search.size(); ++offset) {
            const auto lower = [](const char16_t value) {
                return value >= u'A' && value <= u'Z' ? static_cast<char16_t>(value + (u'a' - u'A')) : value;
            };
            if (lower(guest_first[index + offset]) != lower(guest_search[offset])) {
                equal = false;
                break;
            }
        }
        if (equal) {
            return guest_pointer_at(first, index);
        }
    }
    return nullptr;
}

TL_SHLWAPI_MSABI int tl_StrCmpIA(const char* string1, const char* string2) noexcept {
    if (string1 == nullptr || string2 == nullptr) {
        return (string1 == string2) ? 0 : (string1 == nullptr ? -1 : 1);
    }
    std::string guest_string1;
    std::string guest_string2;
    if (!copy_guest_string(string1, guest_string1) || !copy_guest_string(string2, guest_string2)) {
        return 0;
    }
    return util::ascii_case_insensitive_compare(guest_string1, guest_string2);
}

TL_SHLWAPI_MSABI int tl_StrCmpIW(const std::uint16_t* string1, const std::uint16_t* string2) noexcept {
    if (string1 == nullptr || string2 == nullptr) {
        return (string1 == string2) ? 0 : (string1 == nullptr ? -1 : 1);
    }
    std::u16string guest_string1;
    std::u16string guest_string2;
    if (!copy_guest_string(string1, guest_string1) || !copy_guest_string(string2, guest_string2)) {
        return 0;
    }
    const std::string utf8_1 = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_string1.data()), guest_string1.size());
    const std::string utf8_2 = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_string2.data()), guest_string2.size());
    return util::ascii_case_insensitive_compare(utf8_1, utf8_2);
}

TL_SHLWAPI_MSABI int tl_PathIsRelativeA(const char* const path) noexcept {
    std::string guest_path;
    if (path == nullptr || !copy_guest_string(path, guest_path) || guest_path.empty()) {
        return 1;
    }
    if (guest_path[0] == '\\' || guest_path[0] == '/') {
        return 0;
    }
    if (guest_path.size() >= 2U &&
        std::isalpha(static_cast<unsigned char>(guest_path[0])) && guest_path[1] == ':') {
        return 0;
    }
    return 1;
}

TL_SHLWAPI_MSABI int tl_PathIsRelativeW(const std::uint16_t* const path) noexcept {
    std::u16string guest_path;
    if (path == nullptr || !copy_guest_string(path, guest_path) || guest_path.empty()) {
        return 1;
    }
    if (guest_path[0] == u'\\' || guest_path[0] == u'/') {
        return 0;
    }
    if (guest_path.size() >= 2U &&
        ((guest_path[0] >= u'a' && guest_path[0] <= u'z') ||
         (guest_path[0] >= u'A' && guest_path[0] <= u'Z')) &&
        guest_path[1] == u':') {
        return 0;
    }
    return 1;
}

TL_MSABI int tl_PathIsUNCW(const std::uint16_t* const path) noexcept {
    std::u16string guest_path;
    if (path == nullptr || !copy_guest_string(path, guest_path) || guest_path.empty()) {
        return 0;
    }
    return guest_path.size() >= 2U &&
                   ((guest_path[0] == u'\\' && guest_path[1] == u'\\') ||
                    (guest_path[0] == u'/' && guest_path[1] == u'/'))
               ? 1
               : 0;
}

TL_MSABI int tl_PathIsUNCA(const char* const path) noexcept {
    std::string guest_path;
    if (path == nullptr || !copy_guest_string(path, guest_path) || guest_path.empty()) {
        return 0;
    }
    return guest_path.size() >= 2U &&
                   ((guest_path[0] == '\\' && guest_path[1] == '\\') ||
                    (guest_path[0] == '/' && guest_path[1] == '/'))
               ? 1
               : 0;
}

TL_SHLWAPI_MSABI int tl_SHAutoComplete(const void* const hwnd_edit, const std::uint32_t flags) noexcept {
    (void)hwnd_edit;
    (void)flags;
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_SHLWAPI_MSABI int tl_PathStripToRootW(std::uint16_t* const path) noexcept {
    std::u16string guest_path;
    if (!copy_guest_string(path, guest_path)) {
        return 0;
    }
    if (guest_path.size() >= 2U && guest_path[1] == u':') {
        guest_path.resize(3U);
        guest_path[2] = u'\\';
        if (!write_guest_bytes(path, guest_path.data(),
                               (guest_path.size() + 1U) * sizeof(std::uint16_t))) {
            return 0;
        }
        return 1;
    }
    if (!guest_path.empty() && (guest_path[0] == u'\\' || guest_path[0] == u'/')) {
        guest_path.resize(1U);
        if (!write_guest_bytes(path, guest_path.data(),
                               (guest_path.size() + 1U) * sizeof(std::uint16_t))) {
            return 0;
        }
        return 1;
    }
    return 0;
}

TL_SHLWAPI_MSABI int tl_AssocQueryStringW(const std::uint32_t flags, const std::uint32_t str, const wchar_t* const pszAssoc, const wchar_t* const pszExtra, wchar_t* const pszOut, std::uint32_t* const pcchOut) noexcept {
    (void)flags;
    (void)str;
    (void)pszAssoc;
    (void)pszExtra;
    bool output_ok = true;
    if (pcchOut != nullptr) {
        output_ok = write_guest_value(pcchOut, std::uint32_t{0}) && output_ok;
    }
    if (pszOut != nullptr) {
        const std::uint16_t zero = 0;
        output_ok = write_guest_bytes(pszOut, &zero, sizeof(zero)) && output_ok;
    }
    if (!output_ok) {
        set_last_error(abi::kErrorInvalidParameter);
    }
    return static_cast<int>(0x80004005U); // E_FAIL
}

TL_SHLWAPI_MSABI void tl_ColorRGBToHLS(const std::uint32_t clrRGB, std::uint16_t* const pwHue, std::uint16_t* const pwLuminance, std::uint16_t* const pwSaturation) noexcept {
    (void)clrRGB;
    const bool output_ok = (pwHue == nullptr || write_guest_value(pwHue, std::uint16_t{0})) &&
                           (pwLuminance == nullptr || write_guest_value(pwLuminance, std::uint16_t{120})) &&
                           (pwSaturation == nullptr || write_guest_value(pwSaturation, std::uint16_t{120}));
    if (!output_ok) {
        set_last_error(abi::kErrorInvalidParameter);
    }
}

TL_SHLWAPI_MSABI std::uint32_t tl_ColorHLSToRGB(const std::uint16_t wHue, const std::uint16_t wLuminance, const std::uint16_t wSaturation) noexcept {
    (void)wHue;
    (void)wSaturation;
    const std::uint32_t lum = static_cast<std::uint32_t>(wLuminance & 0xFF);
    return lum | (lum << 8) | (lum << 16);
}

TL_SHLWAPI_MSABI std::uint32_t tl_ColorAdjustLuma(const std::uint32_t clrRGB, const int n, const int fBorder) noexcept {
    (void)n;
    (void)fBorder;
    return clrRGB;
}

TL_SHLWAPI_MSABI void tl_PathStripPathW(wchar_t* const pszPath) noexcept {
    std::u16string guest_path;
    auto* const path = reinterpret_cast<std::uint16_t*>(pszPath);
    if (!copy_guest_string(path, guest_path)) return;
    std::size_t last_slash = guest_path.size();
    for (std::size_t i = 0; i < guest_path.size(); ++i) {
        if (guest_path[i] == u'\\' || guest_path[i] == u'/') {
            last_slash = i;
        }
    }
    if (last_slash < guest_path.size()) {
        guest_path.erase(0, last_slash + 1U);
        static_cast<void>(write_guest_bytes(path, guest_path.data(),
                                            (guest_path.size() + 1U) * sizeof(std::uint16_t)));
    }
}

TL_SHLWAPI_MSABI int tl_PathAddExtensionW(wchar_t* const pszPath, const wchar_t* const pszExt) noexcept {
    auto* const path = reinterpret_cast<std::uint16_t*>(pszPath);
    const auto* const ext = reinterpret_cast<const std::uint16_t*>(pszExt);
    std::u16string guest_path;
    std::u16string guest_ext;
    if (!copy_guest_string(path, guest_path) ||
        (ext != nullptr && !copy_guest_string(ext, guest_ext))) {
        return 0;
    }
    const std::size_t len = guest_path.size();
    bool has_dot = false;
    for (std::size_t index = 0; index < len; ++index) {
        if (guest_path[index] == u'.') has_dot = true;
        if (guest_path[index] == u'\\' || guest_path[index] == u'/') has_dot = false;
    }
    if (has_dot) return 0;
    if (ext == nullptr) {
        guest_ext = u".";
    }
    guest_path += guest_ext;
    return write_guest_bytes(path, guest_path.data(),
                             (guest_path.size() + 1U) * sizeof(std::uint16_t))
               ? 1
               : 0;
}

TL_SHLWAPI_MSABI int tl_PathAppendW(wchar_t* const pszPath, const wchar_t* const pszMore) noexcept {
    auto* const path = reinterpret_cast<std::uint16_t*>(pszPath);
    const auto* const more = reinterpret_cast<const std::uint16_t*>(pszMore);
    std::u16string guest_path;
    std::u16string guest_more;
    if (!copy_guest_string(path, guest_path) || !copy_guest_string(more, guest_more)) {
        return 0;
    }
    if (!guest_path.empty() && guest_path.back() != u'\\' && guest_path.back() != u'/') {
        guest_path += u'\\';
    }
    guest_path += guest_more;
    return write_guest_bytes(path, guest_path.data(),
                             (guest_path.size() + 1U) * sizeof(std::uint16_t))
               ? 1
               : 0;
}

TL_SHLWAPI_MSABI void tl_PathRemoveExtensionW(wchar_t* const pszPath) noexcept {
    auto* const path = reinterpret_cast<std::uint16_t*>(pszPath);
    std::u16string guest_path;
    if (!copy_guest_string(path, guest_path)) return;
    std::size_t dot_pos = std::u16string::npos;
    for (std::size_t index = 0; index < guest_path.size(); ++index) {
        if (guest_path[index] == u'.') dot_pos = index;
        if (guest_path[index] == u'\\' || guest_path[index] == u'/') {
            dot_pos = std::u16string::npos;
        }
    }
    if (dot_pos != std::u16string::npos) {
        guest_path.resize(dot_pos);
        static_cast<void>(write_guest_bytes(path, guest_path.data(),
                                            (guest_path.size() + 1U) * sizeof(std::uint16_t)));
    }
}

TL_SHLWAPI_MSABI int tl_PathCompactPathExW(wchar_t* const pszOut, const wchar_t* const pszSrc, const std::uint32_t cchMax, const std::uint32_t dwFlags) noexcept {
    (void)dwFlags;
    auto* const out = reinterpret_cast<std::uint16_t*>(pszOut);
    const auto* const src = reinterpret_cast<const std::uint16_t*>(pszSrc);
    if (out == nullptr || cchMax == 0) return 0;
    if (pszSrc == nullptr) {
        return write_guest_value(out, std::uint16_t{0}) ? 1 : 0;
    }
    std::u16string guest_src;
    if (!copy_guest_string(src, guest_src)) {
        return 0;
    }
    const std::size_t copy_len = std::min<std::size_t>(guest_src.size(), cchMax - 1U);
    return write_guest_bytes(out, guest_src.data(), copy_len * sizeof(std::uint16_t)) &&
                   write_guest_value(guest_pointer_at(out, copy_len), std::uint16_t{0})
               ? 1
               : 0;
}

TL_SHLWAPI_MSABI int tl_PathGetDriveNumberW(const wchar_t* const pszPath) noexcept {
    const auto* const path = reinterpret_cast<const std::uint16_t*>(pszPath);
    std::u16string guest_path;
    if (!copy_guest_string(path, guest_path) || guest_path.size() < 2U || guest_path[1] != u':') {
        return -1;
    }
    const char16_t ch = guest_path[0];
    if (ch >= u'A' && ch <= u'Z') return ch - u'A';
    if (ch >= u'a' && ch <= u'z') return ch - u'a';
    return -1;
}

TL_SHLWAPI_MSABI int tl_PathMatchSpecW(const wchar_t* const pszFile, const wchar_t* const pszSpec) noexcept {
    std::u16string guest_file;
    std::u16string guest_spec;
    return copy_guest_string(reinterpret_cast<const std::uint16_t*>(pszFile), guest_file) &&
                   copy_guest_string(reinterpret_cast<const std::uint16_t*>(pszSpec), guest_spec)
               ? 1
               : 0;
}

TL_MSABI int tl_PathRemoveExtensionA(char* const path) noexcept {
    std::string guest_path;
    if (!copy_guest_string(path, guest_path)) {
        return 0;
    }
    std::size_t dot_pos = std::string::npos;
    for (std::size_t index = 0; index < guest_path.size(); ++index) {
        if (guest_path[index] == '.') dot_pos = index;
        else if (guest_path[index] == '\\' || guest_path[index] == '/' || guest_path[index] == ' ') {
            dot_pos = std::string::npos;
        }
    }
    if (dot_pos != std::string::npos) {
        guest_path.resize(dot_pos);
        if (!write_guest_bytes(path, guest_path.c_str(), guest_path.size() + 1U)) {
            return 0;
        }
    }
    return 1;
}

TL_MSABI int tl_PathRenameExtensionA(char* const path, const char* const ext) noexcept {
    std::string guest_path;
    std::string guest_ext;
    if (!copy_guest_string(path, guest_path) ||
        (ext != nullptr && !copy_guest_string(ext, guest_ext))) {
        return 0;
    }
    std::size_t dot_pos = std::string::npos;
    for (std::size_t index = 0; index < guest_path.size(); ++index) {
        if (guest_path[index] == '.') dot_pos = index;
        else if (guest_path[index] == '\\' || guest_path[index] == '/') dot_pos = std::string::npos;
    }
    if (dot_pos != std::string::npos) {
        guest_path.resize(dot_pos);
    }
    if (!guest_ext.empty()) {
        if (guest_ext.front() != '.') guest_path += '.';
        guest_path += guest_ext;
    }
    if (!write_guest_bytes(path, guest_path.c_str(), guest_path.size() + 1U)) {
        return 0;
    }
    return 1;
}

TL_MSABI char* tl_PathStripPathA(char* const path) noexcept {
    std::string guest_path;
    if (!copy_guest_string(path, guest_path)) {
        return nullptr;
    }
    std::size_t offset = 0;
    for (std::size_t index = 0; index < guest_path.size(); ++index) {
        if (guest_path[index] == '\\' || guest_path[index] == '/' || guest_path[index] == ':') {
            offset = index + 1U;
        }
    }
    if (offset != 0U) {
        guest_path.erase(0, offset);
        if (!write_guest_bytes(path, guest_path.c_str(), guest_path.size() + 1U)) {
            return nullptr;
        }
    }
    return path;
}

TL_MSABI int tl_PathMatchSpecA(const char* const file, const char* const spec) noexcept {
    std::string guest_file;
    std::string guest_spec;
    return copy_guest_string(file, guest_file) && copy_guest_string(spec, guest_spec) ? 1 : 0;
}

}  // extern "C"
}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_shlwapi_module() {
    static const ExportedFunction kShlwapiExports[] = {
        {"PathFileExistsA", 1, reinterpret_cast<std::uintptr_t>(&tl_PathFileExistsA), ExportSupport::Full},
        {"PathFileExistsW", 2, reinterpret_cast<std::uintptr_t>(&tl_PathFileExistsW), ExportSupport::Full},
        {"PathIsDirectoryA", 3, reinterpret_cast<std::uintptr_t>(&tl_PathIsDirectoryA), ExportSupport::Full},
        {"PathIsDirectoryW", 4, reinterpret_cast<std::uintptr_t>(&tl_PathIsDirectoryW), ExportSupport::Full},
        {"PathCombineA", 5, reinterpret_cast<std::uintptr_t>(&tl_PathCombineA), ExportSupport::Full},
        {"PathCombineW", 6, reinterpret_cast<std::uintptr_t>(&tl_PathCombineW), ExportSupport::Full},
        {"PathFindFileNameA", 7, reinterpret_cast<std::uintptr_t>(&tl_PathFindFileNameA), ExportSupport::Full},
        {"PathFindFileNameW", 8, reinterpret_cast<std::uintptr_t>(&tl_PathFindFileNameW), ExportSupport::Full},
        {"PathFindExtensionA", 9, reinterpret_cast<std::uintptr_t>(&tl_PathFindExtensionA), ExportSupport::Full},
        {"PathFindExtensionW", 10, reinterpret_cast<std::uintptr_t>(&tl_PathFindExtensionW), ExportSupport::Full},
        {"PathRemoveFileSpecA", 11, reinterpret_cast<std::uintptr_t>(&tl_PathRemoveFileSpecA), ExportSupport::Full},
        {"PathRemoveFileSpecW", 12, reinterpret_cast<std::uintptr_t>(&tl_PathRemoveFileSpecW), ExportSupport::Full},
        {"PathAddBackslashA", 13, reinterpret_cast<std::uintptr_t>(&tl_PathAddBackslashA), ExportSupport::Full},
        {"PathAddBackslashW", 14, reinterpret_cast<std::uintptr_t>(&tl_PathAddBackslashW), ExportSupport::Full},
        {"PathRemoveBackslashA", 15, reinterpret_cast<std::uintptr_t>(&tl_PathRemoveBackslashA), ExportSupport::Full},
        {"PathRemoveBackslashW", 16, reinterpret_cast<std::uintptr_t>(&tl_PathRemoveBackslashW), ExportSupport::Full},
        {"StrStrIA", 17, reinterpret_cast<std::uintptr_t>(&tl_StrStrIA), ExportSupport::Full},
        {"StrStrIW", 18, reinterpret_cast<std::uintptr_t>(&tl_StrStrIW), ExportSupport::Full},
        {"StrCmpIA", 19, reinterpret_cast<std::uintptr_t>(&tl_StrCmpIA), ExportSupport::Full},
        {"StrCmpIW", 20, reinterpret_cast<std::uintptr_t>(&tl_StrCmpIW), ExportSupport::Full},
        {"PathIsRelativeA", 21, reinterpret_cast<std::uintptr_t>(&tl_PathIsRelativeA), ExportSupport::Full},
        {"PathIsRelativeW", 22, reinterpret_cast<std::uintptr_t>(&tl_PathIsRelativeW), ExportSupport::Full},
        {"SHAutoComplete", 23, reinterpret_cast<std::uintptr_t>(&tl_SHAutoComplete), ExportSupport::Full},
        {"PathStripToRootW", 24, reinterpret_cast<std::uintptr_t>(&tl_PathStripToRootW), ExportSupport::Full},
        {"AssocQueryStringW", 25, reinterpret_cast<std::uintptr_t>(&tl_AssocQueryStringW), ExportSupport::Full},
        {"ColorRGBToHLS", 26, reinterpret_cast<std::uintptr_t>(&tl_ColorRGBToHLS), ExportSupport::Full},
        {"ColorHLSToRGB", 27, reinterpret_cast<std::uintptr_t>(&tl_ColorHLSToRGB), ExportSupport::Full},
        {"ColorAdjustLuma", 28, reinterpret_cast<std::uintptr_t>(&tl_ColorAdjustLuma), ExportSupport::Full},
        {"PathStripPathW", 29, reinterpret_cast<std::uintptr_t>(&tl_PathStripPathW), ExportSupport::Full},
        {"PathAddExtensionW", 30, reinterpret_cast<std::uintptr_t>(&tl_PathAddExtensionW), ExportSupport::Full},
        {"PathAppendW", 31, reinterpret_cast<std::uintptr_t>(&tl_PathAppendW), ExportSupport::Full},
        {"PathRemoveExtensionW", 32, reinterpret_cast<std::uintptr_t>(&tl_PathRemoveExtensionW), ExportSupport::Full},
        {"PathCompactPathExW", 33, reinterpret_cast<std::uintptr_t>(&tl_PathCompactPathExW), ExportSupport::Full},
        {"PathGetDriveNumberW", 34, reinterpret_cast<std::uintptr_t>(&tl_PathGetDriveNumberW), ExportSupport::Full},
        {"PathMatchSpecW", 35, reinterpret_cast<std::uintptr_t>(&tl_PathMatchSpecW), ExportSupport::Full},
        {"PathIsUNCW", 36, reinterpret_cast<std::uintptr_t>(&tl_PathIsUNCW), ExportSupport::Full},
        {"PathIsUNCA", 37, reinterpret_cast<std::uintptr_t>(&tl_PathIsUNCA), ExportSupport::Full},
        {"PathRemoveExtensionA", 38, reinterpret_cast<std::uintptr_t>(&tl_PathRemoveExtensionA), ExportSupport::Full},
        {"PathRenameExtensionA", 39, reinterpret_cast<std::uintptr_t>(&tl_PathRenameExtensionA), ExportSupport::Full},
        {"PathStripPathA", 40, reinterpret_cast<std::uintptr_t>(&tl_PathStripPathA), ExportSupport::Full},
        {"PathMatchSpecA", 41, reinterpret_cast<std::uintptr_t>(&tl_PathMatchSpecA), ExportSupport::Full},
        {"", 176, reinterpret_cast<std::uintptr_t>(&tl_PathStripToRootW), ExportSupport::Full},
        {"", 410, reinterpret_cast<std::uintptr_t>(&tl_PathStripToRootW), ExportSupport::Full},
        {"", 413, reinterpret_cast<std::uintptr_t>(&tl_PathStripToRootW), ExportSupport::Full},
    };
    static const InternalModule kShlwapiModule{"SHLWAPI.dll", kShlwapiExports};
    register_module(kShlwapiModule);
}

}  // namespace tradutorlinux::loader
