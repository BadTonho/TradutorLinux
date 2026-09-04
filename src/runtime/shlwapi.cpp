#include "tradutorlinux/runtime/shlwapi.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "tradutorlinux/runtime/memory_validator.hpp"
#include "runtime_context.hpp"
#include "tradutorlinux/util/basics.hpp"
#include "tradutorlinux/util/unicode.hpp"

namespace tradutorlinux {

namespace {

inline bool mapped_cstring(const char* value) noexcept {
    return runtime::validate_mapped_cstring(value);
}

inline bool mapped_wstring(const std::uint16_t* value) noexcept {
    return runtime::validate_mapped_wstring(value);
}

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

std::string normalize_win_path(const std::string& path) {
    std::string out = path;
    for (char& c : out) {
        if (c == '\\') c = '/';
    }
    // Remove C: or similar drive letter prefix if present
    if (out.size() >= 2 && std::isalpha(static_cast<unsigned char>(out[0])) && out[1] == ':') {
        out = out.substr(2);
        if (out.empty() || out[0] != '/') {
            out = "/" + out;
        }
        out = "." + out; // translate to current root/prefix
    }
    return out;
}

}  // namespace

extern "C" {

TL_SHLWAPI_MSABI int tl_PathFileExistsA(const char* path) noexcept {
    if (!mapped_cstring(path) || path[0] == '\0') {
        return 0;
    }
    const std::string norm = normalize_win_path(path);
    struct stat st{};
    return (stat(norm.c_str(), &st) == 0 || stat(path, &st) == 0) ? 1 : 0;
}

TL_SHLWAPI_MSABI int tl_PathFileExistsW(const std::uint16_t* path) noexcept {
    if (!mapped_wstring(path)) {
        return 0;
    }
    const std::string utf8 = util::wide_to_utf8(path);
    return tl_PathFileExistsA(utf8.c_str());
}

TL_SHLWAPI_MSABI int tl_PathIsDirectoryA(const char* path) noexcept {
    if (!mapped_cstring(path) || path[0] == '\0') {
        return 0;
    }
    const std::string norm = normalize_win_path(path);
    struct stat st{};
    if (stat(norm.c_str(), &st) == 0 || stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 1 : 0;
    }
    return 0;
}

TL_SHLWAPI_MSABI int tl_PathIsDirectoryW(const std::uint16_t* path) noexcept {
    if (!mapped_wstring(path)) {
        return 0;
    }
    const std::string utf8 = util::wide_to_utf8(path);
    return tl_PathIsDirectoryA(utf8.c_str());
}

TL_SHLWAPI_MSABI char* tl_PathCombineA(char* dest, const char* dir, const char* file) noexcept {
    if (dest == nullptr) {
        return nullptr;
    }
    std::string result;
    if (dir != nullptr && mapped_cstring(dir) && dir[0] != '\0') {
        result = dir;
        if (result.back() != '\\' && result.back() != '/') {
            result += '\\';
        }
    }
    if (file != nullptr && mapped_cstring(file)) {
        // If file starts with slash/backslash, skip leading slashes if dir present
        std::size_t offset = 0;
        if (!result.empty() && (file[0] == '\\' || file[0] == '/')) {
            offset = 1;
        }
        result += (file + offset);
    }
    std::strcpy(dest, result.c_str());
    return dest;
}

TL_SHLWAPI_MSABI std::uint16_t* tl_PathCombineW(std::uint16_t* dest, const std::uint16_t* dir, const std::uint16_t* file) noexcept {
    if (dest == nullptr) {
        return nullptr;
    }
    const std::string utf8_dir = (dir != nullptr && mapped_wstring(dir)) ? util::wide_to_utf8(dir) : "";
    const std::string utf8_file = (file != nullptr && mapped_wstring(file)) ? util::wide_to_utf8(file) : "";
    char buf[4096]{};
    tl_PathCombineA(buf, utf8_dir.c_str(), utf8_file.c_str());
    const std::u16string u16 = util::utf8_to_wide(buf);
    std::copy(u16.begin(), u16.end(), dest);
    dest[u16.size()] = 0;
    return dest;
}

TL_SHLWAPI_MSABI char* tl_PathFindFileNameA(const char* path) noexcept {
    if (path == nullptr || !mapped_cstring(path)) {
        return nullptr;
    }
    const char* last = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '\\' || *p == '/' || *p == ':') {
            last = p + 1;
        }
    }
    return const_cast<char*>(last);
}

TL_SHLWAPI_MSABI std::uint16_t* tl_PathFindFileNameW(const std::uint16_t* path) noexcept {
    if (path == nullptr || !mapped_wstring(path)) {
        return nullptr;
    }
    const std::uint16_t* last = path;
    for (const std::uint16_t* p = path; *p != 0; ++p) {
        if (*p == u'\\' || *p == u'/' || *p == u':') {
            last = p + 1;
        }
    }
    return const_cast<std::uint16_t*>(last);
}

TL_SHLWAPI_MSABI char* tl_PathFindExtensionA(const char* path) noexcept {
    if (path == nullptr || !mapped_cstring(path)) {
        return nullptr;
    }
    const char* dot = nullptr;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '.') {
            dot = p;
        } else if (*p == '\\' || *p == '/' || *p == ' ') {
            dot = nullptr;
        }
    }
    return const_cast<char*>(dot != nullptr ? dot : (path + std::strlen(path)));
}

TL_SHLWAPI_MSABI std::uint16_t* tl_PathFindExtensionW(const std::uint16_t* path) noexcept {
    if (path == nullptr || !mapped_wstring(path)) {
        return nullptr;
    }
    const std::uint16_t* dot = nullptr;
    const std::uint16_t* p = path;
    for (; *p != 0; ++p) {
        if (*p == u'.') {
            dot = p;
        } else if (*p == u'\\' || *p == u'/' || *p == u' ') {
            dot = nullptr;
        }
    }
    return const_cast<std::uint16_t*>(dot != nullptr ? dot : p);
}

TL_SHLWAPI_MSABI int tl_PathRemoveFileSpecA(char* path) noexcept {
    if (path == nullptr || !mapped_cstring(path)) {
        return 0;
    }
    char* cut = nullptr;
    for (char* p = path; *p != '\0'; ++p) {
        if (*p == '\\' || *p == '/') {
            cut = p;
        }
    }
    if (cut != nullptr) {
        *cut = '\0';
        return 1;
    }
    return 0;
}

TL_SHLWAPI_MSABI int tl_PathRemoveFileSpecW(std::uint16_t* path) noexcept {
    if (path == nullptr || !mapped_wstring(path)) {
        return 0;
    }
    std::uint16_t* cut = nullptr;
    for (std::uint16_t* p = path; *p != 0; ++p) {
        if (*p == u'\\' || *p == u'/') {
            cut = p;
        }
    }
    if (cut != nullptr) {
        *cut = 0;
        return 1;
    }
    return 0;
}

TL_SHLWAPI_MSABI char* tl_PathAddBackslashA(char* path) noexcept {
    if (path == nullptr || !mapped_cstring(path)) {
        return nullptr;
    }
    const std::size_t len = std::strlen(path);
    if (len > 0 && path[len - 1] != '\\' && path[len - 1] != '/') {
        path[len] = '\\';
        path[len + 1] = '\0';
        return path + len + 1;
    }
    return path + len;
}

TL_SHLWAPI_MSABI std::uint16_t* tl_PathAddBackslashW(std::uint16_t* path) noexcept {
    if (path == nullptr || !mapped_wstring(path)) {
        return nullptr;
    }
    std::size_t len = 0;
    while (path[len] != 0) {
        ++len;
    }
    if (len > 0 && path[len - 1] != u'\\' && path[len - 1] != u'/') {
        path[len] = u'\\';
        path[len + 1] = 0;
        return path + len + 1;
    }
    return path + len;
}

TL_SHLWAPI_MSABI char* tl_PathRemoveBackslashA(char* path) noexcept {
    if (path == nullptr || !mapped_cstring(path)) {
        return nullptr;
    }
    const std::size_t len = std::strlen(path);
    if (len > 0 && (path[len - 1] == '\\' || path[len - 1] == '/')) {
        path[len - 1] = '\0';
        return path + len - 1;
    }
    return path + len;
}

TL_SHLWAPI_MSABI std::uint16_t* tl_PathRemoveBackslashW(std::uint16_t* path) noexcept {
    if (path == nullptr || !mapped_wstring(path)) {
        return nullptr;
    }
    std::size_t len = 0;
    while (path[len] != 0) {
        ++len;
    }
    if (len > 0 && (path[len - 1] == u'\\' || path[len - 1] == u'/')) {
        path[len - 1] = 0;
        return path + len - 1;
    }
    return path + len;
}

TL_SHLWAPI_MSABI char* tl_StrStrIA(const char* first, const char* srch) noexcept {
    if (first == nullptr || srch == nullptr || !mapped_cstring(first) || !mapped_cstring(srch)) {
        return nullptr;
    }
    const std::size_t srch_len = std::strlen(srch);
    if (srch_len == 0) {
        return const_cast<char*>(first);
    }
    for (const char* p = first; *p != '\0'; ++p) {
        if (util::ascii_iequals(std::string_view(p, srch_len), std::string_view(srch, srch_len))) {
            return const_cast<char*>(p);
        }
    }
    return nullptr;
}

TL_SHLWAPI_MSABI std::uint16_t* tl_StrStrIW(const std::uint16_t* first, const std::uint16_t* srch) noexcept {
    if (first == nullptr || srch == nullptr || !mapped_wstring(first) || !mapped_wstring(srch)) {
        return nullptr;
    }
    const std::string utf8_first = util::wide_to_utf8(first);
    const std::string utf8_srch = util::wide_to_utf8(srch);
    char* found = tl_StrStrIA(utf8_first.c_str(), utf8_srch.c_str());
    if (found == nullptr) {
        return nullptr;
    }
    const std::size_t char_offset = static_cast<std::size_t>(found - utf8_first.c_str());
    return const_cast<std::uint16_t*>(first + char_offset);
}

TL_SHLWAPI_MSABI int tl_StrCmpIA(const char* string1, const char* string2) noexcept {
    if (string1 == nullptr || string2 == nullptr) {
        return (string1 == string2) ? 0 : (string1 == nullptr ? -1 : 1);
    }
    return util::ascii_case_insensitive_compare(string1, string2);
}

TL_SHLWAPI_MSABI int tl_StrCmpIW(const std::uint16_t* string1, const std::uint16_t* string2) noexcept {
    const std::string utf8_1 = (string1 != nullptr && mapped_wstring(string1)) ? util::wide_to_utf8(string1) : "";
    const std::string utf8_2 = (string2 != nullptr && mapped_wstring(string2)) ? util::wide_to_utf8(string2) : "";
    return tl_StrCmpIA(utf8_1.c_str(), utf8_2.c_str());
}

TL_SHLWAPI_MSABI int tl_PathIsRelativeA(const char* const path) noexcept {
    if (path == nullptr || !mapped_cstring(path) || path[0] == '\0') {
        return 1;
    }
    if (path[0] == '\\' || path[0] == '/') {
        return 0;
    }
    if (std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') {
        return 0;
    }
    return 1;
}

TL_SHLWAPI_MSABI int tl_PathIsRelativeW(const std::uint16_t* const path) noexcept {
    if (path == nullptr || !mapped_wstring(path) || path[0] == 0) {
        return 1;
    }
    if (path[0] == u'\\' || path[0] == u'/') {
        return 0;
    }
    if (((path[0] >= u'a' && path[0] <= u'z') || (path[0] >= u'A' && path[0] <= u'Z')) && path[1] == u':') {
        return 0;
    }
    return 1;
}

TL_MSABI int tl_PathIsUNCW(const std::uint16_t* const path) noexcept {
    if (path == nullptr || !mapped_wstring(path) || path[0] == 0) {
        return 0;
    }
    return (path[0] == u'\\' && path[1] == u'\\') || (path[0] == u'/' && path[1] == u'/') ? 1 : 0;
}

TL_MSABI int tl_PathIsUNCA(const char* const path) noexcept {
    if (path == nullptr || !mapped_cstring(path) || path[0] == '\0') {
        return 0;
    }
    return (path[0] == '\\' && path[1] == '\\') || (path[0] == '/' && path[1] == '/') ? 1 : 0;
}

TL_SHLWAPI_MSABI int tl_SHAutoComplete(const void* const hwnd_edit, const std::uint32_t flags) noexcept {
    (void)hwnd_edit;
    (void)flags;
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_SHLWAPI_MSABI int tl_PathStripToRootW(std::uint16_t* const path) noexcept {
    if (path == nullptr || !mapped_wstring(path)) {
        return 0;
    }
    if (path[0] != 0 && path[1] == u':') {
        path[2] = u'\\';
        path[3] = 0;
        return 1;
    }
    if (path[0] == u'\\' || path[0] == u'/') {
        path[1] = 0;
        return 1;
    }
    return 0;
}

TL_SHLWAPI_MSABI int tl_AssocQueryStringW(const std::uint32_t flags, const std::uint32_t str, const wchar_t* const pszAssoc, const wchar_t* const pszExtra, wchar_t* const pszOut, std::uint32_t* const pcchOut) noexcept {
    (void)flags;
    (void)str;
    (void)pszAssoc;
    (void)pszExtra;
    if (pcchOut != nullptr) {
        *pcchOut = 0;
    }
    if (pszOut != nullptr) {
        *pszOut = 0;
    }
    return static_cast<int>(0x80004005U); // E_FAIL
}

TL_SHLWAPI_MSABI void tl_ColorRGBToHLS(const std::uint32_t clrRGB, std::uint16_t* const pwHue, std::uint16_t* const pwLuminance, std::uint16_t* const pwSaturation) noexcept {
    (void)clrRGB;
    if (pwHue != nullptr) *pwHue = 0;
    if (pwLuminance != nullptr) *pwLuminance = 120;
    if (pwSaturation != nullptr) *pwSaturation = 120;
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
    if (pszPath == nullptr) return;
    std::size_t len = 0;
    while (pszPath[len] != 0) ++len;
    std::size_t last_slash = len;
    for (std::size_t i = 0; i < len; ++i) {
        if (pszPath[i] == L'\\' || pszPath[i] == L'/') {
            last_slash = i;
        }
    }
    if (last_slash < len) {
        std::memmove(pszPath, pszPath + last_slash + 1, (len - last_slash) * sizeof(wchar_t));
    }
}

TL_SHLWAPI_MSABI int tl_PathAddExtensionW(wchar_t* const pszPath, const wchar_t* const pszExt) noexcept {
    if (pszPath == nullptr) return 0;
    std::size_t len = 0;
    bool has_dot = false;
    while (pszPath[len] != 0) {
        if (pszPath[len] == L'.') has_dot = true;
        if (pszPath[len] == L'\\' || pszPath[len] == L'/') has_dot = false;
        ++len;
    }
    if (has_dot) return 0;
    const wchar_t* ext = pszExt != nullptr ? pszExt : L".";
    std::size_t ext_len = 0;
    while (ext[ext_len] != 0) ++ext_len;
    std::memcpy(pszPath + len, ext, (ext_len + 1) * sizeof(wchar_t));
    return 1;
}

TL_SHLWAPI_MSABI int tl_PathAppendW(wchar_t* const pszPath, const wchar_t* const pszMore) noexcept {
    if (pszPath == nullptr || pszMore == nullptr) return 0;
    std::size_t len = 0;
    while (pszPath[len] != 0) ++len;
    if (len > 0 && pszPath[len - 1] != L'\\' && pszPath[len - 1] != L'/') {
        pszPath[len++] = L'\\';
    }
    std::size_t more_len = 0;
    while (pszMore[more_len] != 0) ++more_len;
    std::memcpy(pszPath + len, pszMore, (more_len + 1) * sizeof(wchar_t));
    return 1;
}

TL_SHLWAPI_MSABI void tl_PathRemoveExtensionW(wchar_t* const pszPath) noexcept {
    if (pszPath == nullptr) return;
    std::size_t len = 0;
    std::size_t dot_pos = (std::size_t)-1;
    while (pszPath[len] != 0) {
        if (pszPath[len] == L'.') dot_pos = len;
        if (pszPath[len] == L'\\' || pszPath[len] == L'/') dot_pos = (std::size_t)-1;
        ++len;
    }
    if (dot_pos != (std::size_t)-1) {
        pszPath[dot_pos] = 0;
    }
}

TL_SHLWAPI_MSABI int tl_PathCompactPathExW(wchar_t* const pszOut, const wchar_t* const pszSrc, const std::uint32_t cchMax, const std::uint32_t dwFlags) noexcept {
    (void)dwFlags;
    if (pszOut == nullptr || cchMax == 0) return 0;
    if (pszSrc == nullptr) {
        pszOut[0] = 0;
        return 1;
    }
    std::size_t len = 0;
    while (pszSrc[len] != 0) ++len;
    if (len < cchMax) {
        std::memcpy(pszOut, pszSrc, (len + 1) * sizeof(wchar_t));
    } else {
        std::memcpy(pszOut, pszSrc, (cchMax - 1) * sizeof(wchar_t));
        pszOut[cchMax - 1] = 0;
    }
    return 1;
}

TL_SHLWAPI_MSABI int tl_PathGetDriveNumberW(const wchar_t* const pszPath) noexcept {
    if (pszPath == nullptr || pszPath[0] == 0 || pszPath[1] != L':') return -1;
    const wchar_t ch = pszPath[0];
    if (ch >= L'A' && ch <= L'Z') return ch - L'A';
    if (ch >= L'a' && ch <= L'z') return ch - L'a';
    return -1;
}

TL_SHLWAPI_MSABI int tl_PathMatchSpecW(const wchar_t* const pszFile, const wchar_t* const pszSpec) noexcept {
    (void)pszFile;
    (void)pszSpec;
    return 1;
}

TL_MSABI int tl_PathRemoveExtensionA(char* const path) noexcept {
    if (path == nullptr || !mapped_cstring(path)) {
        return 0;
    }
    char* dot = nullptr;
    for (char* p = path; *p != '\0'; ++p) {
        if (*p == '.') dot = p;
        else if (*p == '\\' || *p == '/' || *p == ' ') dot = nullptr;
    }
    if (dot != nullptr) {
        *dot = '\0';
    }
    return 1;
}

TL_MSABI int tl_PathRenameExtensionA(char* const path, const char* const ext) noexcept {
    if (path == nullptr || !mapped_cstring(path)) {
        return 0;
    }
    tl_PathRemoveExtensionA(path);
    if (ext != nullptr && mapped_cstring(ext) && ext[0] != '\0') {
        const std::size_t len = std::strlen(path);
        const std::size_t ext_len = std::strlen(ext);
        if (ext[0] != '.') {
            path[len] = '.';
            std::memcpy(path + len + 1, ext, ext_len + 1);
        } else {
            std::memcpy(path + len, ext, ext_len + 1);
        }
    }
    return 1;
}

TL_MSABI char* tl_PathStripPathA(char* const path) noexcept {
    if (path == nullptr || !mapped_cstring(path)) {
        return path;
    }
    char* last = path;
    for (char* p = path; *p != '\0'; ++p) {
        if (*p == '\\' || *p == '/' || *p == ':') last = p + 1;
    }
    if (last != path) {
        std::memmove(path, last, std::strlen(last) + 1);
    }
    return path;
}

TL_MSABI int tl_PathMatchSpecA(const char* const file, const char* const spec) noexcept {
    (void)file;
    (void)spec;
    return 1;
}

}  // extern "C"
}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_shlwapi_module() {
    static const ExportedFunction kShlwapiExports[] = {
        {"PathFileExistsA", 1, reinterpret_cast<std::uintptr_t>(&tl_PathFileExistsA)},
        {"PathFileExistsW", 2, reinterpret_cast<std::uintptr_t>(&tl_PathFileExistsW)},
        {"PathIsDirectoryA", 3, reinterpret_cast<std::uintptr_t>(&tl_PathIsDirectoryA)},
        {"PathIsDirectoryW", 4, reinterpret_cast<std::uintptr_t>(&tl_PathIsDirectoryW)},
        {"PathCombineA", 5, reinterpret_cast<std::uintptr_t>(&tl_PathCombineA)},
        {"PathCombineW", 6, reinterpret_cast<std::uintptr_t>(&tl_PathCombineW)},
        {"PathFindFileNameA", 7, reinterpret_cast<std::uintptr_t>(&tl_PathFindFileNameA)},
        {"PathFindFileNameW", 8, reinterpret_cast<std::uintptr_t>(&tl_PathFindFileNameW)},
        {"PathFindExtensionA", 9, reinterpret_cast<std::uintptr_t>(&tl_PathFindExtensionA)},
        {"PathFindExtensionW", 10, reinterpret_cast<std::uintptr_t>(&tl_PathFindExtensionW)},
        {"PathRemoveFileSpecA", 11, reinterpret_cast<std::uintptr_t>(&tl_PathRemoveFileSpecA)},
        {"PathRemoveFileSpecW", 12, reinterpret_cast<std::uintptr_t>(&tl_PathRemoveFileSpecW)},
        {"PathAddBackslashA", 13, reinterpret_cast<std::uintptr_t>(&tl_PathAddBackslashA)},
        {"PathAddBackslashW", 14, reinterpret_cast<std::uintptr_t>(&tl_PathAddBackslashW)},
        {"PathRemoveBackslashA", 15, reinterpret_cast<std::uintptr_t>(&tl_PathRemoveBackslashA)},
        {"PathRemoveBackslashW", 16, reinterpret_cast<std::uintptr_t>(&tl_PathRemoveBackslashW)},
        {"StrStrIA", 17, reinterpret_cast<std::uintptr_t>(&tl_StrStrIA)},
        {"StrStrIW", 18, reinterpret_cast<std::uintptr_t>(&tl_StrStrIW)},
        {"StrCmpIA", 19, reinterpret_cast<std::uintptr_t>(&tl_StrCmpIA)},
        {"StrCmpIW", 20, reinterpret_cast<std::uintptr_t>(&tl_StrCmpIW)},
        {"PathIsRelativeA", 21, reinterpret_cast<std::uintptr_t>(&tl_PathIsRelativeA)},
        {"PathIsRelativeW", 22, reinterpret_cast<std::uintptr_t>(&tl_PathIsRelativeW)},
        {"SHAutoComplete", 23, reinterpret_cast<std::uintptr_t>(&tl_SHAutoComplete)},
        {"PathStripToRootW", 24, reinterpret_cast<std::uintptr_t>(&tl_PathStripToRootW)},
        {"AssocQueryStringW", 25, reinterpret_cast<std::uintptr_t>(&tl_AssocQueryStringW)},
        {"ColorRGBToHLS", 26, reinterpret_cast<std::uintptr_t>(&tl_ColorRGBToHLS)},
        {"ColorHLSToRGB", 27, reinterpret_cast<std::uintptr_t>(&tl_ColorHLSToRGB)},
        {"ColorAdjustLuma", 28, reinterpret_cast<std::uintptr_t>(&tl_ColorAdjustLuma)},
        {"PathStripPathW", 29, reinterpret_cast<std::uintptr_t>(&tl_PathStripPathW)},
        {"PathAddExtensionW", 30, reinterpret_cast<std::uintptr_t>(&tl_PathAddExtensionW)},
        {"PathAppendW", 31, reinterpret_cast<std::uintptr_t>(&tl_PathAppendW)},
        {"PathRemoveExtensionW", 32, reinterpret_cast<std::uintptr_t>(&tl_PathRemoveExtensionW)},
        {"PathCompactPathExW", 33, reinterpret_cast<std::uintptr_t>(&tl_PathCompactPathExW)},
        {"PathGetDriveNumberW", 34, reinterpret_cast<std::uintptr_t>(&tl_PathGetDriveNumberW)},
        {"PathMatchSpecW", 35, reinterpret_cast<std::uintptr_t>(&tl_PathMatchSpecW)},
        {"PathIsUNCW", 36, reinterpret_cast<std::uintptr_t>(&tl_PathIsUNCW)},
        {"PathIsUNCA", 37, reinterpret_cast<std::uintptr_t>(&tl_PathIsUNCA)},
        {"PathRemoveExtensionA", 38, reinterpret_cast<std::uintptr_t>(&tl_PathRemoveExtensionA)},
        {"PathRenameExtensionA", 39, reinterpret_cast<std::uintptr_t>(&tl_PathRenameExtensionA)},
        {"PathStripPathA", 40, reinterpret_cast<std::uintptr_t>(&tl_PathStripPathA)},
        {"PathMatchSpecA", 41, reinterpret_cast<std::uintptr_t>(&tl_PathMatchSpecA)},
        {"", 176, reinterpret_cast<std::uintptr_t>(&tl_PathStripToRootW)},
        {"", 410, reinterpret_cast<std::uintptr_t>(&tl_PathStripToRootW)},
        {"", 413, reinterpret_cast<std::uintptr_t>(&tl_PathStripToRootW)},
    };
    static const InternalModule kShlwapiModule{"SHLWAPI.dll", kShlwapiExports};
    register_module(kShlwapiModule);
}

}  // namespace tradutorlinux::loader
