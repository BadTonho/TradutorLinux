#include "tradutorlinux/runtime/shlwapi.hpp"

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

TL_SHLWAPI_MSABI int tl_SHAutoComplete(const void* const hwnd_edit, const std::uint32_t flags) noexcept {
    (void)hwnd_edit;
    (void)flags;
    set_last_error(abi::kErrorSuccess);
    return 0;
}

}  // extern "C"

}  // namespace tradutorlinux
