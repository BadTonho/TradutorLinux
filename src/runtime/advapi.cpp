#include "tradutorlinux/runtime/advapi.hpp"

#include "tradutorlinux/runtime/winapi.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <sys/types.h>

namespace tradutorlinux {
namespace {

constexpr std::uintptr_t kHkeyCurrentUser = 0x80000001U;
constexpr std::uint32_t kRegSz = 1;
constexpr std::uint32_t kErrorFileNotFound = 2;
constexpr std::uint32_t kErrorMoreData = 234;
constexpr std::uint32_t kErrorInvalidParameter = 87;

struct RegistryKey {
    bool open{false};
    std::string path;
};

struct RegistryValue {
    std::string key_path;
    std::string value_name;
    std::uint32_t type{};
    std::vector<unsigned char> data;
};

std::array<RegistryKey, 64> g_keys{};
std::vector<RegistryValue> g_values;
bool g_loaded = false;

bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    if (address == nullptr) {
        return false;
    }
    const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(address);
    if (size > std::numeric_limits<std::uintptr_t>::max() - target) {
        return false;
    }
    std::ifstream maps{"/proc/self/maps"};
    std::string line;
    while (std::getline(maps, line)) {
        const std::size_t dash = line.find('-');
        const std::size_t space = line.find(' ', dash == std::string::npos ? 0 : dash);
        if (dash == std::string::npos || space == std::string::npos) {
            continue;
        }
        std::uintptr_t begin = 0;
        std::uintptr_t end = 0;
        if (std::from_chars(line.data(), line.data() + dash, begin, 16).ec != std::errc{} ||
            std::from_chars(line.data() + dash + 1, line.data() + space, end, 16).ec != std::errc{} ||
            target < begin || target > end || size > end - target) {
            continue;
        }
        const std::size_t permissions = space + 1;
        return line.size() >= permissions + 2 && line[permissions] == 'r' &&
               (!writable || line[permissions + 1] == 'w');
    }
    return false;
}

bool mapped_cstring(const char* value) noexcept {
    if (value == nullptr) {
        return false;
    }
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(value);
    for (std::size_t index = 0; index < 65535U; ++index) {
        if (index > std::numeric_limits<std::uintptr_t>::max() - address ||
            !mapped_range(reinterpret_cast<const void*>(address + index), 1, false)) {
            return false;
        }
        if (value[index] == '\0') {
            return true;
        }
    }
    return false;
}

bool mapped_wstring(const std::uint16_t* value) noexcept {
    if (value == nullptr) {
        return false;
    }
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(value);
    for (std::size_t index = 0; index < 65535U; ++index) {
        if (index > (std::numeric_limits<std::uintptr_t>::max() - address) / sizeof(*value) ||
            !mapped_range(reinterpret_cast<const void*>(address + index * sizeof(*value)),
                          sizeof(*value), false)) {
            return false;
        }
        if (value[index] == 0) {
            return true;
        }
    }
    return false;
}

std::string wide_to_utf8(const std::uint16_t* value) {
    std::string result;
    if (!mapped_wstring(value)) {
        return result;
    }
    for (std::size_t index = 0; value[index] != 0; ++index) {
        const std::uint32_t unit = value[index];
        if (unit >= 0xD800U && unit <= 0xDBFFU && value[index + 1] >= 0xDC00U &&
            value[index + 1] <= 0xDFFFU) {
            const std::uint32_t codepoint = 0x10000U + ((unit - 0xD800U) << 10U) +
                                            (value[++index] - 0xDC00U);
            result.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
            result.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        } else if (unit < 0x80U) {
            result.push_back(static_cast<char>(unit));
        } else if (unit < 0x800U) {
            result.push_back(static_cast<char>(0xC0U | (unit >> 6U)));
            result.push_back(static_cast<char>(0x80U | (unit & 0x3FU)));
        } else {
            result.push_back(static_cast<char>(0xE0U | (unit >> 12U)));
            result.push_back(static_cast<char>(0x80U | ((unit >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (unit & 0x3FU)));
        }
    }
    return result;
}

std::string registry_path() {
    if (const char* explicit_path = std::getenv("TL_REGISTRY_FILE");
        explicit_path != nullptr && explicit_path[0] != '\0') {
        return explicit_path;
    }
    if (const char* appdata = std::getenv("APPDATA"); appdata != nullptr && appdata[0] != '\0') {
        return std::string{appdata} + "/tradutorlinux-registry.db";
    }
    return ".tl-registry.db";
}

std::vector<unsigned char> decode_hex(const std::string& value) {
    if ((value.size() & 1U) != 0) {
        return {};
    }
    std::vector<unsigned char> result;
    result.reserve(value.size() / 2U);
    for (std::size_t index = 0; index < value.size(); index += 2) {
        unsigned int byte = 0;
        const auto parsed = std::from_chars(value.data() + index, value.data() + index + 2,
                                            byte, 16);
        if (parsed.ec != std::errc{}) {
            return {};
        }
        result.push_back(static_cast<unsigned char>(byte));
    }
    return result;
}

std::string encode_hex(const std::vector<unsigned char>& value);

std::string encode_hex(const std::string& value) {
    return encode_hex(std::vector<unsigned char>{value.begin(), value.end()});
}

std::string encode_hex(const std::vector<unsigned char>& value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2U);
    for (const unsigned char byte : value) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0FU]);
    }
    return result;
}

void load_registry() noexcept {
    if (g_loaded) {
        return;
    }
    g_loaded = true;
    std::ifstream input{registry_path()};
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream fields{line};
        std::string key_hex;
        std::string value_hex;
        std::string type_hex;
        std::string data_hex;
        if (!(fields >> key_hex >> value_hex >> type_hex >> data_hex)) {
            continue;
        }
        const std::vector<unsigned char> key_bytes = decode_hex(key_hex);
        const std::vector<unsigned char> value_bytes = decode_hex(value_hex);
        const std::vector<unsigned char> data = decode_hex(data_hex);
        unsigned int type = 0;
        if ((key_bytes.empty() && !key_hex.empty()) ||
            (value_bytes.empty() && !value_hex.empty()) ||
            (data.empty() && !data_hex.empty()) ||
            std::from_chars(type_hex.data(), type_hex.data() + type_hex.size(), type, 16).ec !=
                std::errc{}) {
            continue;
        }
        g_values.push_back(RegistryValue{
            .key_path = std::string{key_bytes.begin(), key_bytes.end()},
            .value_name = std::string{value_bytes.begin(), value_bytes.end()},
            .type = type,
            .data = data,
        });
    }
}

void save_registry() noexcept {
    const std::filesystem::path path{registry_path()};
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    std::ofstream output{path, std::ios::trunc};
    if (!output) {
        return;
    }
    for (const RegistryValue& value : g_values) {
        output << encode_hex(value.key_path) << ' ' << encode_hex(value.value_name) << ' '
               << std::hex << value.type << ' ' << encode_hex(value.data) << std::dec << '\n';
    }
}

bool is_current_user(const void* key) noexcept {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(key)) ==
           static_cast<std::uint32_t>(kHkeyCurrentUser);
}

RegistryKey* find_key(const void* key) noexcept {
    const auto found = std::find_if(g_keys.begin(), g_keys.end(), [key](const RegistryKey& candidate) {
        return candidate.open && key == &candidate;
    });
    return found == g_keys.end() ? nullptr : &*found;
}

std::string compose_key_path(const void* key, const std::string& subkey, bool& valid) noexcept {
    valid = false;
    if (is_current_user(key)) {
        valid = true;
        return subkey;
    }
    RegistryKey* parent = find_key(key);
    if (parent == nullptr) {
        return {};
    }
    valid = true;
    return parent->path.empty() || subkey.empty() ? parent->path + subkey
                                                  : parent->path + "\\" + subkey;
}

std::uint32_t open_key(const void* key, const std::string& subkey, void** result,
                       std::uint32_t* disposition, const bool create) noexcept {
    if (result == nullptr || !mapped_range(result, sizeof(*result), true) ||
        (disposition != nullptr && !mapped_range(disposition, sizeof(*disposition), true))) {
        return kErrorInvalidParameter;
    }
    bool valid = false;
    const std::string path = compose_key_path(key, subkey, valid);
    if (!valid) {
        return kErrorFileNotFound;
    }
    if (!create && path.empty()) {
        return kErrorFileNotFound;
    }
    auto free_it = std::find_if(g_keys.begin(), g_keys.end(),
                                [](const RegistryKey& candidate) { return !candidate.open; });
    if (free_it == g_keys.end()) {
        return abi::kErrorNotEnoughMemory;
    }
    load_registry();
    const bool existed = std::any_of(g_values.begin(), g_values.end(), [&](const RegistryValue& value) {
        return value.key_path == path;
    });
    free_it->open = true;
    free_it->path = path;
    *result = &*free_it;
    if (disposition != nullptr) {
        *disposition = existed ? 2U : 1U;
    }
    return abi::kErrorSuccess;
}

std::uint32_t set_value(const void* key, const std::string& name, const std::uint32_t type,
                        const unsigned char* data, const std::uint32_t data_size) noexcept {
    RegistryKey* open = find_key(key);
    if (open == nullptr || (data_size != 0 &&
                            (data == nullptr || !mapped_range(data, data_size, false)))) {
        return abi::kErrorInvalidParameter;
    }
    load_registry();
    auto found = std::find_if(g_values.begin(), g_values.end(), [&](const RegistryValue& value) {
        return value.key_path == open->path && value.value_name == name;
    });
    RegistryValue replacement{open->path, name, type, {}};
    if (data_size != 0) {
        replacement.data.assign(data, data + data_size);
    }
    if (found == g_values.end()) {
        g_values.push_back(std::move(replacement));
    } else {
        *found = std::move(replacement);
    }
    save_registry();
    return abi::kErrorSuccess;
}

std::uint32_t query_value(const void* key, const std::string& name, std::uint32_t* type,
                          unsigned char* data, std::uint32_t* data_size) noexcept {
    RegistryKey* open = find_key(key);
    if (open == nullptr || data_size == nullptr || !mapped_range(data_size, sizeof(*data_size), true) ||
        (type != nullptr && !mapped_range(type, sizeof(*type), true))) {
        return abi::kErrorInvalidParameter;
    }
    load_registry();
    const auto found = std::find_if(g_values.begin(), g_values.end(), [&](const RegistryValue& value) {
        return value.key_path == open->path && value.value_name == name;
    });
    if (found == g_values.end()) {
        return kErrorFileNotFound;
    }
    if (type != nullptr) {
        *type = found->type;
    }
    const std::uint32_t required = static_cast<std::uint32_t>(found->data.size());
    if (data == nullptr || *data_size < required) {
        *data_size = required;
        return data == nullptr ? abi::kErrorSuccess : kErrorMoreData;
    }
    if (required != 0 && !mapped_range(data, required, true)) {
        return abi::kErrorInvalidParameter;
    }
    std::copy(found->data.begin(), found->data.end(), data);
    *data_size = required;
    return abi::kErrorSuccess;
}

std::uint32_t delete_value(const void* key, const std::string& name) noexcept {
    RegistryKey* open = find_key(key);
    if (open == nullptr) {
        return abi::kErrorInvalidHandle;
    }
    load_registry();
    const auto found = std::find_if(g_values.begin(), g_values.end(), [&](const RegistryValue& value) {
        return value.key_path == open->path && value.value_name == name;
    });
    if (found == g_values.end()) {
        return kErrorFileNotFound;
    }
    g_values.erase(found);
    save_registry();
    return abi::kErrorSuccess;
}

}  // namespace

TL_ADVAPI_MSABI std::uint32_t tl_RegCloseKey(const void* key) noexcept {
    RegistryKey* open = find_key(key);
    if (open == nullptr) {
        return abi::kErrorInvalidHandle;
    }
    *open = {};
    return abi::kErrorSuccess;
}

TL_ADVAPI_MSABI std::uint32_t tl_RegOpenKeyExA(const void* key, const char* subkey,
                                               const std::uint32_t options,
                                               const std::uint32_t access, void** result) noexcept {
    (void)options;
    (void)access;
    if (!mapped_cstring(subkey)) {
        return kErrorInvalidParameter;
    }
    return open_key(key, subkey, result, nullptr, false);
}

TL_ADVAPI_MSABI std::uint32_t tl_RegOpenKeyExW(const void* key, const std::uint16_t* subkey,
                                               const std::uint32_t options,
                                               const std::uint32_t access, void** result) noexcept {
    (void)options;
    (void)access;
    if (!mapped_wstring(subkey)) {
        return kErrorInvalidParameter;
    }
    return open_key(key, wide_to_utf8(subkey), result, nullptr, false);
}

TL_ADVAPI_MSABI std::uint32_t tl_RegCreateKeyExA(const void* key, const char* subkey,
                                                 const std::uint32_t reserved, char* class_name,
                                                 const std::uint32_t options,
                                                 const std::uint32_t access,
                                                 const void* security_attributes, void** result,
                                                 std::uint32_t* disposition) noexcept {
    (void)reserved;
    (void)class_name;
    (void)options;
    (void)access;
    if (security_attributes != nullptr || !mapped_cstring(subkey)) {
        return kErrorInvalidParameter;
    }
    return open_key(key, subkey, result, disposition, true);
}

TL_ADVAPI_MSABI std::uint32_t tl_RegCreateKeyExW(const void* key, const std::uint16_t* subkey,
                                                 const std::uint32_t reserved,
                                                 std::uint16_t* class_name,
                                                 const std::uint32_t options,
                                                 const std::uint32_t access,
                                                 const void* security_attributes, void** result,
                                                 std::uint32_t* disposition) noexcept {
    (void)reserved;
    (void)class_name;
    (void)options;
    (void)access;
    if (security_attributes != nullptr || !mapped_wstring(subkey)) {
        return kErrorInvalidParameter;
    }
    return open_key(key, wide_to_utf8(subkey), result, disposition, true);
}

TL_ADVAPI_MSABI std::uint32_t tl_RegQueryValueExA(const void* key, const char* value_name,
                                                  std::uint32_t* reserved, std::uint32_t* type,
                                                  unsigned char* data,
                                                  std::uint32_t* data_size) noexcept {
    (void)reserved;
    if (!mapped_cstring(value_name)) {
        return kErrorInvalidParameter;
    }
    return query_value(key, value_name, type, data, data_size);
}

TL_ADVAPI_MSABI std::uint32_t tl_RegQueryValueExW(const void* key,
                                                  const std::uint16_t* value_name,
                                                  std::uint32_t* reserved, std::uint32_t* type,
                                                  unsigned char* data,
                                                  std::uint32_t* data_size) noexcept {
    (void)reserved;
    if (!mapped_wstring(value_name)) {
        return kErrorInvalidParameter;
    }
    return query_value(key, wide_to_utf8(value_name), type, data, data_size);
}

TL_ADVAPI_MSABI std::uint32_t tl_RegSetValueExA(const void* key, const char* value_name,
                                                const std::uint32_t reserved,
                                                const std::uint32_t type,
                                                const unsigned char* data,
                                                const std::uint32_t data_size) noexcept {
    (void)reserved;
    if (!mapped_cstring(value_name)) {
        return kErrorInvalidParameter;
    }
    return set_value(key, value_name, type, data, data_size);
}

TL_ADVAPI_MSABI std::uint32_t tl_RegSetValueExW(const void* key,
                                                const std::uint16_t* value_name,
                                                const std::uint32_t reserved,
                                                const std::uint32_t type,
                                                const unsigned char* data,
                                                const std::uint32_t data_size) noexcept {
    (void)reserved;
    if (!mapped_wstring(value_name)) {
        return kErrorInvalidParameter;
    }
    return set_value(key, wide_to_utf8(value_name), type, data, data_size);
}

TL_ADVAPI_MSABI std::uint32_t tl_RegDeleteValueA(const void* key, const char* value_name) noexcept {
    if (!mapped_cstring(value_name)) {
        return kErrorInvalidParameter;
    }
    return delete_value(key, value_name);
}

TL_ADVAPI_MSABI std::uint32_t tl_RegDeleteValueW(const void* key,
                                                 const std::uint16_t* value_name) noexcept {
    if (!mapped_wstring(value_name)) {
        return kErrorInvalidParameter;
    }
    return delete_value(key, wide_to_utf8(value_name));
}

}  // namespace tradutorlinux
