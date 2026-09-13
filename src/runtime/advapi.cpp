#include "tradutorlinux/runtime/advapi.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
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
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <sys/types.h>

#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/util/unicode.hpp"
#include "core/runtime_state_common.hpp"

namespace tradutorlinux {
namespace {

constexpr std::uintptr_t kHkeyCurrentUser = 0x80000001U;
constexpr std::uint32_t kRegSz = 1;
constexpr std::uint32_t kRegExpandSz = 2;
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

std::mutex g_registry_mutex;
std::array<RegistryKey, 256> g_keys{};
std::vector<RegistryValue> g_values;
bool g_loaded = false;

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

bool copy_ansi_string(const char* const value, std::string& result) noexcept {
    return runtime::copy_guest_cstring(value, 65535U, result);
}

bool copy_wide_string(const std::uint16_t* const value, std::string& result) noexcept {
    std::u16string copy;
    if (!runtime::copy_guest_wstring(value, 65535U, copy)) {
        result.clear();
        return false;
    }
    try {
        result = util::wide_to_utf8(reinterpret_cast<const std::uint16_t*>(copy.data()), copy.size());
    } catch (...) {
        result.clear();
        return false;
    }
    return true;
}

bool is_string_value(const std::uint32_t type) noexcept {
    return type == kRegSz || type == kRegExpandSz;
}

std::vector<unsigned char> ansi_string_to_utf8(const unsigned char* data,
                                               const std::uint32_t data_size) {
    std::vector<unsigned char> result;
    result.reserve(data_size * 2U);
    for (std::size_t index = 0; index < data_size; ++index) {
        const std::uint32_t codepoint = util::cp1252_to_unicode(data[index]);
        if (codepoint == 0) {
            result.push_back(0);
            break;
        }
        char bytes[4]{};
        const std::size_t count = util::utf8_bytes_for(codepoint, bytes);
        result.insert(result.end(), bytes, bytes + static_cast<std::ptrdiff_t>(count));
    }
    if (result.empty() || result.back() != 0) {
        result.push_back(0);
    }
    return result;
}

std::vector<unsigned char> wide_string_to_utf8(const unsigned char* data,
                                               const std::uint32_t data_size) {
    if ((data_size % sizeof(std::uint16_t)) != 0) {
        return {};
    }
    const std::size_t unit_count = data_size / sizeof(std::uint16_t);
    std::vector<std::uint16_t> units(unit_count + 1U, 0);
    for (std::size_t index = 0; index < unit_count; ++index) {
        units[index] = static_cast<std::uint16_t>(data[index * 2U]) |
                       (static_cast<std::uint16_t>(data[index * 2U + 1U]) << 8U);
    }
    const std::string utf8 = util::wide_to_utf8(units.data(), units.size());
    std::vector<unsigned char> result(utf8.begin(), utf8.end());
    result.push_back(0);
    return result;
}

std::vector<unsigned char> string_value_to_utf8(const unsigned char* data,
                                                const std::uint32_t data_size,
                                                const bool wide) {
    if (data_size == 0) {
        return {0};
    }
    return wide ? wide_string_to_utf8(data, data_size)
                : ansi_string_to_utf8(data, data_size);
}

std::vector<unsigned char> utf8_string_to_wide(const std::vector<unsigned char>& data) {
    const auto terminator = std::find(data.begin(), data.end(), static_cast<unsigned char>(0));
    const std::string_view utf8{reinterpret_cast<const char*>(data.data()),
                                static_cast<std::size_t>(terminator - data.begin())};
    const std::u16string wide = util::utf8_to_wide(utf8);
    std::vector<unsigned char> result;
    result.reserve((wide.size() + 1U) * sizeof(std::uint16_t));
    for (const char16_t unit : wide) {
        result.push_back(static_cast<unsigned char>(unit & 0xFFU));
        result.push_back(static_cast<unsigned char>(unit >> 8U));
    }
    result.push_back(0);
    result.push_back(0);
    return result;
}

std::vector<unsigned char> utf8_string_to_ansi(const std::vector<unsigned char>& data) {
    const auto terminator = std::find(data.begin(), data.end(), static_cast<unsigned char>(0));
    const char* const bytes = reinterpret_cast<const char*>(data.data());
    const std::size_t length = static_cast<std::size_t>(terminator - data.begin());
    std::vector<unsigned char> result;
    result.reserve(length + 1U);
    std::size_t position = 0;
    while (position < length) {
        const std::uint32_t codepoint = util::decode_utf8(bytes, length, position);
        std::uint8_t byte = '?';
        if (codepoint != util::kInvalidCodepoint) {
            (void)util::unicode_to_cp1252(codepoint, byte);
        }
        result.push_back(byte);
    }
    result.push_back(0);
    return result;
}

std::vector<unsigned char> registry_data_for_query(const RegistryValue& value,
                                                   const bool wide) {
    if (!is_string_value(value.type)) {
        return value.data;
    }
    return wide ? utf8_string_to_wide(value.data) : utf8_string_to_ansi(value.data);
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

void ensure_default_registry_keys() noexcept {
    auto set_default_sz = [](const std::string& key, const std::string& name, const std::string& str) {
        auto it = std::find_if(g_values.begin(), g_values.end(), [&](const RegistryValue& v) {
            return v.key_path == key && v.value_name == name;
        });
        if (it == g_values.end()) {
            std::vector<unsigned char> data(str.begin(), str.end());
            data.push_back('\0');
            g_values.push_back(RegistryValue{key, name, kRegSz, data});
        }
    };

    // Chaves padrão consultadas por instaladores (Inno Setup, NSIS, MSI, etc.)
    set_default_sz("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion", "ProgramFilesDir", "C:\\Program Files");
    set_default_sz("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion", "ProgramFilesDir (x86)", "C:\\Program Files (x86)");
    set_default_sz("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion", "CommonFilesDir", "C:\\Program Files\\Common Files");
    set_default_sz("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion", "SystemRoot", "C:\\windows");

    set_default_sz("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion", "ProductName", "Windows 10 Pro");
    set_default_sz("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion", "CurrentVersion", "6.3");
    set_default_sz("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion", "CurrentBuild", "19045");
    set_default_sz("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion", "CurrentBuildNumber", "19045");
    set_default_sz("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion", "SystemRoot", "C:\\windows");

    set_default_sz("HKEY_CURRENT_USER\\Environment", "TEMP", "C:\\windows\\temp");
    set_default_sz("HKEY_CURRENT_USER\\Environment", "TMP", "C:\\windows\\temp");
}

void load_registry_locked() noexcept {
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
    ensure_default_registry_keys();
}

void save_registry_locked() noexcept {
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

bool is_predefined_key(const void* key) noexcept {
    const auto val = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(key));
    return val >= 0x80000000U && val <= 0x80000006U;
}

std::string predefined_key_prefix(const void* key) noexcept {
    const auto val = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(key));
    switch (val) {
        case 0x80000000U: return "HKEY_CLASSES_ROOT";
        case 0x80000001U: return "HKEY_CURRENT_USER";
        case 0x80000002U: return "HKEY_LOCAL_MACHINE";
        case 0x80000003U: return "HKEY_USERS";
        case 0x80000005U: return "HKEY_CURRENT_CONFIG";
        default: return "";
    }
}

RegistryKey* find_key(const void* key) noexcept {
    const auto found = std::find_if(g_keys.begin(), g_keys.end(), [key](const RegistryKey& candidate) {
        return candidate.open && key == &candidate;
    });
    return found == g_keys.end() ? nullptr : &*found;
}

std::string compose_key_path(const void* key, const std::string& subkey, bool& valid) noexcept {
    valid = false;
    if (is_predefined_key(key)) {
        valid = true;
        const std::string prefix = predefined_key_prefix(key);
        if (subkey.empty()) {
            return prefix;
        }
        return prefix.empty() ? subkey : prefix + "\\" + subkey;
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
    if (result == nullptr) {
        return kErrorInvalidParameter;
    }
    std::lock_guard<std::mutex> lock(g_registry_mutex);
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
    load_registry_locked();
    const bool existed = std::any_of(g_values.begin(), g_values.end(), [&](const RegistryValue& value) {
        return value.key_path == path;
    });
    free_it->open = true;
    free_it->path = path;
    void* const handle = &*free_it;
    if (!write_guest_value(result, handle)) {
        *free_it = {};
        return kErrorInvalidParameter;
    }
    if (disposition != nullptr &&
        !write_guest_value(disposition, existed ? std::uint32_t{2} : std::uint32_t{1})) {
        static_cast<void>(write_guest_value(result, static_cast<void*>(nullptr)));
        *free_it = {};
        return kErrorInvalidParameter;
    }
    return abi::kErrorSuccess;
}

std::uint32_t set_value(const void* key, const std::string& name, const std::uint32_t type,
                        const unsigned char* data, const std::uint32_t data_size,
                        const bool wide) noexcept {
    std::vector<unsigned char> data_copy;
    try {
        if (data_size != 0) {
            if (data == nullptr) {
                return abi::kErrorInvalidParameter;
            }
            data_copy.resize(data_size);
            if (runtime::read_guest_memory(data, data_copy.data(), data_copy.size()).status !=
                runtime::GuestMemoryAccessStatus::Success) {
                return abi::kErrorInvalidParameter;
            }
        }
    } catch (...) {
        return abi::kErrorNotEnoughMemory;
    }
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    RegistryKey* open = find_key(key);
    if (open == nullptr) {
        return abi::kErrorInvalidParameter;
    }
    load_registry_locked();
    auto found = std::find_if(g_values.begin(), g_values.end(), [&](const RegistryValue& value) {
        return value.key_path == open->path && value.value_name == name;
    });
    RegistryValue replacement{open->path, name, type, {}};
    if (data_size != 0) {
        replacement.data = is_string_value(type)
                               ? string_value_to_utf8(data_copy.data(), data_size, wide)
                               : std::move(data_copy);
        if (is_string_value(type) && replacement.data.empty()) {
            return kErrorInvalidParameter;
        }
    }
    if (found == g_values.end()) {
        g_values.push_back(std::move(replacement));
    } else {
        *found = std::move(replacement);
    }
    save_registry_locked();
    return abi::kErrorSuccess;
}

std::uint32_t query_value(const void* key, const std::string& name, std::uint32_t* type,
                          unsigned char* data, std::uint32_t* data_size,
                          const bool wide) noexcept {
    if (data_size == nullptr) {
        return abi::kErrorInvalidParameter;
    }
    std::uint32_t capacity = 0;
    if (!read_guest_value(data_size, capacity)) {
        return abi::kErrorInvalidParameter;
    }
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    RegistryKey* open = find_key(key);
    if (open == nullptr) {
        return abi::kErrorInvalidParameter;
    }
    load_registry_locked();
    const auto found = std::find_if(g_values.begin(), g_values.end(), [&](const RegistryValue& value) {
        return value.key_path == open->path && value.value_name == name;
    });
    if (found == g_values.end()) {
        return kErrorFileNotFound;
    }
    if (type != nullptr) {
        if (!write_guest_value(type, found->type)) {
            return abi::kErrorInvalidParameter;
        }
    }
    const std::vector<unsigned char> output = registry_data_for_query(*found, wide);
    const std::uint32_t required = static_cast<std::uint32_t>(output.size());
    if (data == nullptr || capacity < required) {
        if (!write_guest_value(data_size, required)) {
            return abi::kErrorInvalidParameter;
        }
        return data == nullptr ? abi::kErrorSuccess : kErrorMoreData;
    }
    if (required != 0 && runtime::write_guest_memory(data, output.data(), required).status !=
                                  runtime::GuestMemoryAccessStatus::Success) {
        return abi::kErrorInvalidParameter;
    }
    if (!write_guest_value(data_size, required)) {
        return abi::kErrorInvalidParameter;
    }
    return abi::kErrorSuccess;
}

std::uint32_t delete_value(const void* key, const std::string& name) noexcept {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    RegistryKey* open = find_key(key);
    if (open == nullptr) {
        return abi::kErrorInvalidHandle;
    }
    load_registry_locked();
    const auto found = std::find_if(g_values.begin(), g_values.end(), [&](const RegistryValue& value) {
        return value.key_path == open->path && value.value_name == name;
    });
    if (found == g_values.end()) {
        return kErrorFileNotFound;
    }
    g_values.erase(found);
    save_registry_locked();
    return abi::kErrorSuccess;
}

}  // namespace

TL_ADVAPI_MSABI std::uint32_t tl_RegCloseKey(const void* key) noexcept {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
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
    std::string subkey_copy;
    if (!copy_ansi_string(subkey, subkey_copy)) {
        return kErrorInvalidParameter;
    }
    return open_key(key, subkey_copy, result, nullptr, false);
}

TL_ADVAPI_MSABI std::uint32_t tl_RegOpenKeyExW(const void* key, const std::uint16_t* subkey,
                                               const std::uint32_t options,
                                               const std::uint32_t access, void** result) noexcept {
    (void)options;
    (void)access;
    std::string subkey_copy;
    if (!copy_wide_string(subkey, subkey_copy)) {
        return kErrorInvalidParameter;
    }
    return open_key(key, subkey_copy, result, nullptr, false);
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
    std::uint32_t security_attributes_prefix = 0;
    std::string subkey_copy;
    if ((security_attributes != nullptr && !read_guest_value(security_attributes,
                                                              security_attributes_prefix)) ||
        !copy_ansi_string(subkey, subkey_copy)) {
        return kErrorInvalidParameter;
    }
    return open_key(key, subkey_copy, result, disposition, true);
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
    std::uint32_t security_attributes_prefix = 0;
    std::string subkey_copy;
    if ((security_attributes != nullptr && !read_guest_value(security_attributes,
                                                              security_attributes_prefix)) ||
        !copy_wide_string(subkey, subkey_copy)) {
        return kErrorInvalidParameter;
    }
    return open_key(key, subkey_copy, result, disposition, true);
}

TL_ADVAPI_MSABI std::uint32_t tl_RegQueryValueExA(const void* key, const char* value_name,
                                                  std::uint32_t* reserved, std::uint32_t* type,
                                                  unsigned char* data,
                                                  std::uint32_t* data_size) noexcept {
    (void)reserved;
    std::string value_name_copy;
    if (!copy_ansi_string(value_name, value_name_copy)) {
        return kErrorInvalidParameter;
    }
    return query_value(key, value_name_copy, type, data, data_size, false);
}

TL_ADVAPI_MSABI std::uint32_t tl_RegQueryValueExW(const void* key,
                                                  const std::uint16_t* value_name,
                                                  std::uint32_t* reserved, std::uint32_t* type,
                                                  unsigned char* data,
                                                  std::uint32_t* data_size) noexcept {
    (void)reserved;
    std::string value_name_copy;
    if (!copy_wide_string(value_name, value_name_copy)) {
        return kErrorInvalidParameter;
    }
    return query_value(key, value_name_copy, type, data, data_size, true);
}

TL_ADVAPI_MSABI std::uint32_t tl_RegSetValueExA(const void* key, const char* value_name,
                                                const std::uint32_t reserved,
                                                const std::uint32_t type,
                                                const unsigned char* data,
                                                const std::uint32_t data_size) noexcept {
    (void)reserved;
    std::string value_name_copy;
    if (!copy_ansi_string(value_name, value_name_copy)) {
        return kErrorInvalidParameter;
    }
    return set_value(key, value_name_copy, type, data, data_size, false);
}

TL_ADVAPI_MSABI std::uint32_t tl_RegSetValueExW(const void* key,
                                                const std::uint16_t* value_name,
                                                const std::uint32_t reserved,
                                                const std::uint32_t type,
                                                const unsigned char* data,
                                                const std::uint32_t data_size) noexcept {
    (void)reserved;
    std::string value_name_copy;
    if (!copy_wide_string(value_name, value_name_copy)) {
        return kErrorInvalidParameter;
    }
    return set_value(key, value_name_copy, type, data, data_size, true);
}

TL_ADVAPI_MSABI std::uint32_t tl_RegDeleteValueA(const void* key, const char* value_name) noexcept {
    std::string value_name_copy;
    if (!copy_ansi_string(value_name, value_name_copy)) {
        return kErrorInvalidParameter;
    }
    return delete_value(key, value_name_copy);
}

TL_ADVAPI_MSABI std::uint32_t tl_RegDeleteValueW(const void* key,
                                                 const std::uint16_t* value_name) noexcept {
    std::string value_name_copy;
    if (!copy_wide_string(value_name, value_name_copy)) {
        return kErrorInvalidParameter;
    }
    return delete_value(key, value_name_copy);
}

TL_ADVAPI_MSABI int tl_CryptAcquireContextA(void** prov_handle, const char* container,
                                            const char* provider, const std::uint32_t prov_type,
                                            const std::uint32_t flags) noexcept {
    (void)container;
    (void)provider;
    (void)prov_type;
    (void)flags;
    if (prov_handle == nullptr) {
        return 0;
    }
    static char g_crypto_provider_token = 0;
    if (!write_guest_value(prov_handle, static_cast<void*>(&g_crypto_provider_token))) {
        return 0;
    }
    return 1;
}

TL_ADVAPI_MSABI int tl_CryptAcquireContextW(void** prov_handle, const std::uint16_t* container,
                                            const std::uint16_t* provider, const std::uint32_t prov_type,
                                            const std::uint32_t flags) noexcept {
    (void)container;
    (void)provider;
    (void)prov_type;
    (void)flags;
    if (prov_handle == nullptr) {
        return 0;
    }
    static char g_crypto_provider_token = 0;
    if (!write_guest_value(prov_handle, static_cast<void*>(&g_crypto_provider_token))) {
        return 0;
    }
    return 1;
}

TL_ADVAPI_MSABI int tl_CryptGenRandom(void* prov_handle, const std::uint32_t length,
                                      std::uint8_t* buffer) noexcept {
    if (prov_handle == nullptr || buffer == nullptr || length == 0) {
        return 0;
    }
    try {
        std::vector<std::uint8_t> random_bytes(length);
        std::ifstream urandom{"/dev/urandom", std::ios::binary};
        if (urandom) {
            urandom.read(reinterpret_cast<char*>(random_bytes.data()), length);
            if (urandom.gcount() != static_cast<std::streamsize>(length)) {
                for (std::uint8_t& byte : random_bytes) {
                    byte = static_cast<std::uint8_t>(std::rand() & 0xFF);
                }
            }
        } else {
            for (std::uint8_t& byte : random_bytes) {
                byte = static_cast<std::uint8_t>(std::rand() & 0xFF);
            }
        }
        if (runtime::write_guest_memory(buffer, random_bytes.data(), random_bytes.size()).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            return 0;
        }
        return 1;
    } catch (...) {
        return 0;
    }
}

TL_ADVAPI_MSABI int tl_CryptReleaseContext(void* prov_handle, const std::uint32_t flags) noexcept {
    (void)prov_handle;
    (void)flags;
    return 1;
}

struct GuestLuid {
    std::uint32_t low_part{1};
    std::int32_t high_part{0};
};

TL_ADVAPI_MSABI int tl_LookupPrivilegeValueW(const std::uint16_t* system_name,
                                             const std::uint16_t* name,
                                             void* luid) noexcept {
    (void)system_name;
    (void)name;
    if (luid == nullptr) {
        return 0;
    }
    GuestLuid out{};
    out.low_part = 1;
    out.high_part = 0;
    return write_guest_value(luid, out) ? 1 : 0;
}

TL_ADVAPI_MSABI int tl_AdjustTokenPrivileges(void* token_handle, int disable_all_privileges,
                                             void* new_state, std::uint32_t buffer_length,
                                             void* previous_state, std::uint32_t* return_length) noexcept {
    (void)token_handle;
    (void)disable_all_privileges;
    (void)new_state;
    (void)buffer_length;
    (void)previous_state;
    if (return_length != nullptr && !write_guest_value(return_length, std::uint32_t{0})) {
        return 0;
    }
    return 1;
}

TL_ADVAPI_MSABI int tl_GetFileSecurityW(const std::uint16_t* file_name, std::uint32_t requested_information,
                                        void* security_descriptor, std::uint32_t length,
                                        std::uint32_t* length_needed) noexcept {
    (void)file_name;
    (void)requested_information;
    (void)security_descriptor;
    (void)length;
    if (length_needed != nullptr && !write_guest_value(length_needed, std::uint32_t{0})) {
        return 0;
    }
    return 1;
}

TL_ADVAPI_MSABI std::int32_t tl_RegDeleteTreeW(void* key, const std::uint16_t* sub_key) noexcept {
    (void)key;
    (void)sub_key;
    return static_cast<std::int32_t>(abi::kErrorSuccess);
}

TL_ADVAPI_MSABI std::int32_t tl_RegEnumValueW(void* key, std::uint32_t index, std::uint16_t* value_name,
                                               std::uint32_t* cch_value_name, std::uint32_t* reserved,
                                               std::uint32_t* type, std::uint8_t* data,
                                               std::uint32_t* cb_data) noexcept {
    (void)key;
    (void)index;
    (void)value_name;
    (void)cch_value_name;
    (void)reserved;
    (void)type;
    (void)data;
    (void)cb_data;
    return 259; // ERROR_NO_MORE_ITEMS
}

TL_ADVAPI_MSABI std::int32_t tl_RegEnumKeyExW(void* key, std::uint32_t index, std::uint16_t* name,
                                               std::uint32_t* cch_name, std::uint32_t* reserved,
                                               std::uint16_t* class_name, std::uint32_t* cch_class_name,
                                               void* last_write_time) noexcept {
    (void)key;
    (void)index;
    (void)name;
    (void)cch_name;
    (void)reserved;
    (void)class_name;
    (void)cch_class_name;
    (void)last_write_time;
    return 259; // ERROR_NO_MORE_ITEMS
}

TL_ADVAPI_MSABI std::int32_t tl_RegDeleteKeyExW(void* key, const std::uint16_t* sub_key,
                                                std::uint32_t sam_desired, std::uint32_t reserved) noexcept {
    (void)key;
    (void)sub_key;
    (void)sam_desired;
    (void)reserved;
    return static_cast<std::int32_t>(abi::kErrorSuccess);
}

TL_ADVAPI_MSABI std::int32_t tl_RegDeleteKeyW(void* key, const std::uint16_t* sub_key) noexcept {
    (void)key;
    (void)sub_key;
    return static_cast<std::int32_t>(abi::kErrorSuccess);
}

TL_ADVAPI_MSABI int tl_GetUserNameW(std::uint16_t* const buffer, std::uint32_t* const size) noexcept {
    static const std::uint16_t kUser[] = {'T', 'o', 'n', 'h', 'o', 0};
    constexpr std::uint32_t kLen = 6;
    if (size == nullptr) {
        tl_SetLastError(abi::kErrorInvalidParameter);
        return 0;
    }
    std::uint32_t capacity = 0;
    if (!read_guest_value(size, capacity)) {
        tl_SetLastError(abi::kErrorInvalidParameter);
        return 0;
    }
    if (capacity < kLen) {
        if (!write_guest_value(size, kLen)) {
            tl_SetLastError(abi::kErrorInvalidParameter);
            return 0;
        }
        tl_SetLastError(122); // ERROR_INSUFFICIENT_BUFFER
        return 0;
    }
    if (buffer != nullptr &&
        runtime::write_guest_memory(buffer, kUser, sizeof(kUser)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
        tl_SetLastError(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!write_guest_value(size, kLen)) {
        tl_SetLastError(abi::kErrorInvalidParameter);
        return 0;
    }
    tl_SetLastError(abi::kErrorSuccess);
    return 1;
}

TL_ADVAPI_MSABI int tl_LookupAccountNameW(const std::uint16_t* const system_name,
                                         const std::uint16_t* const account_name,
                                         void* const sid, std::uint32_t* const sid_size,
                                         std::uint16_t* const referenced_domain,
                                         std::uint32_t* const domain_size,
                                         void* const sid_name_use) noexcept {
    (void)system_name;
    (void)account_name;
    static const std::uint16_t kDomain[] = {'W', 'O', 'R', 'K', 'G', 'R', 'O', 'U', 'P', 0};
    if (domain_size != nullptr) {
        if (!write_guest_value(domain_size, std::uint32_t{10})) {
            tl_SetLastError(kErrorInvalidParameter);
            return 0;
        }
        if (referenced_domain != nullptr &&
            runtime::write_guest_memory(referenced_domain, kDomain, sizeof(kDomain)).status !=
                runtime::GuestMemoryAccessStatus::Success) {
            tl_SetLastError(kErrorInvalidParameter);
            return 0;
        }
    }
    if (sid_size != nullptr) {
        if (!write_guest_value(sid_size, std::uint32_t{28})) {
            tl_SetLastError(kErrorInvalidParameter);
            return 0;
        }
        if (sid != nullptr) {
            const std::array<std::uint8_t, 28> empty_sid{};
            if (runtime::write_guest_memory(sid, empty_sid.data(), empty_sid.size()).status !=
                runtime::GuestMemoryAccessStatus::Success) {
                tl_SetLastError(kErrorInvalidParameter);
                return 0;
            }
        }
    }
    if (sid_name_use != nullptr && !write_guest_value(sid_name_use, std::uint32_t{1})) {
        tl_SetLastError(kErrorInvalidParameter);
        return 0;
    }
    tl_SetLastError(abi::kErrorSuccess);
    return 1;
}

TL_ADVAPI_MSABI int tl_LsaOpenPolicy(void* const system_name, void* const obj_attributes,
                                    const std::uint32_t access_mask, void** const policy_handle) noexcept {
    (void)system_name;
    (void)obj_attributes;
    (void)access_mask;
    if (policy_handle != nullptr &&
        !write_guest_value(policy_handle, reinterpret_cast<void*>(0x4C534150ULL))) {
        return abi::kErrorInvalidParameter;
    }
    return 0; // STATUS_SUCCESS
}

TL_ADVAPI_MSABI int tl_LsaClose(void* const policy_handle) noexcept {
    (void)policy_handle;
    return 0; // STATUS_SUCCESS
}

TL_ADVAPI_MSABI int tl_LsaAddAccountRights(void* const policy_handle, void* const account_sid,
                                          void* const user_rights, const std::uint32_t count) noexcept {
    (void)policy_handle;
    (void)account_sid;
    (void)user_rights;
    (void)count;
    return 0; // STATUS_SUCCESS
}

TL_ADVAPI_MSABI std::int32_t tl_RegQueryInfoKeyA(void* const key, char* const class_name,
                                                 std::uint32_t* const cch_class_name,
                                                 std::uint32_t* const reserved,
                                                 std::uint32_t* const sub_keys,
                                                 std::uint32_t* const max_sub_key_len,
                                                 std::uint32_t* const max_class_len,
                                                 std::uint32_t* const values,
                                                 std::uint32_t* const max_value_name_len,
                                                 std::uint32_t* const max_value_len,
                                                 std::uint32_t* const security_descriptor,
                                                 void* const last_write_time) noexcept {
    (void)key;
    (void)class_name;
    (void)cch_class_name;
    (void)reserved;
    (void)max_class_len;
    (void)security_descriptor;
    (void)last_write_time;
    const auto write_zero = [](std::uint32_t* const output) {
        return output == nullptr || write_guest_value(output, std::uint32_t{0});
    };
    if (!write_zero(sub_keys) || !write_zero(max_sub_key_len) || !write_zero(values) ||
        !write_zero(max_value_name_len) || !write_zero(max_value_len)) {
        return static_cast<std::int32_t>(kErrorInvalidParameter);
    }
    return static_cast<std::int32_t>(abi::kErrorSuccess);
}

TL_ADVAPI_MSABI std::int32_t tl_RegQueryInfoKeyW(void* const key, std::uint16_t* const class_name,
                                                 std::uint32_t* const cch_class_name,
                                                 std::uint32_t* const reserved,
                                                 std::uint32_t* const sub_keys,
                                                 std::uint32_t* const max_sub_key_len,
                                                 std::uint32_t* const max_class_len,
                                                 std::uint32_t* const values,
                                                 std::uint32_t* const max_value_name_len,
                                                 std::uint32_t* const max_value_len,
                                                 std::uint32_t* const security_descriptor,
                                                 void* const last_write_time) noexcept {
    (void)key;
    (void)class_name;
    (void)cch_class_name;
    (void)reserved;
    (void)max_class_len;
    (void)security_descriptor;
    (void)last_write_time;
    const auto write_zero = [](std::uint32_t* const output) {
        return output == nullptr || write_guest_value(output, std::uint32_t{0});
    };
    if (!write_zero(sub_keys) || !write_zero(max_sub_key_len) || !write_zero(values) ||
        !write_zero(max_value_name_len) || !write_zero(max_value_len)) {
        return static_cast<std::int32_t>(kErrorInvalidParameter);
    }
    return static_cast<std::int32_t>(abi::kErrorSuccess);
}

TL_ADVAPI_MSABI std::int32_t tl_RegEnumKeyA(void* const key, const std::uint32_t index,
                                            char* const name, const std::uint32_t cch_name) noexcept {
    (void)key;
    (void)index;
    (void)name;
    (void)cch_name;
    return 259; // ERROR_NO_MORE_ITEMS
}

TL_ADVAPI_MSABI std::int32_t tl_RegEnumValueA(void* const key, const std::uint32_t index,
                                              char* const value_name, std::uint32_t* const cch_value_name,
                                              std::uint32_t* const reserved, std::uint32_t* const type,
                                              std::uint8_t* const data, std::uint32_t* const cb_data) noexcept {
    (void)key;
    (void)index;
    (void)value_name;
    (void)cch_value_name;
    (void)reserved;
    (void)type;
    (void)data;
    (void)cb_data;
    return 259; // ERROR_NO_MORE_ITEMS
}

TL_ADVAPI_MSABI std::int32_t tl_RegDeleteKeyA(void* const key, const char* const sub_key) noexcept {
    (void)key;
    (void)sub_key;
    return static_cast<std::int32_t>(abi::kErrorSuccess);
}

TL_ADVAPI_MSABI std::int32_t tl_RegGetValueW(void* const key, const wchar_t* const sub_key, const wchar_t* const value,
                                             const std::uint32_t flags, std::uint32_t* const type,
                                             void* const data, std::uint32_t* const data_len) noexcept {
    (void)flags;
    return static_cast<std::int32_t>(tl_RegQueryValueExW(key, reinterpret_cast<const std::uint16_t*>(value != nullptr ? value : sub_key),
                                                         nullptr, type, reinterpret_cast<unsigned char*>(data), data_len));
}

TL_ADVAPI_MSABI void* tl_RegisterEventSourceW(const wchar_t* const server_name, const wchar_t* const source_name) noexcept {
    (void)server_name;
    (void)source_name;
    return reinterpret_cast<void*>(0x45564E54ULL); // 'EVNT'
}

TL_ADVAPI_MSABI int tl_DeregisterEventSource(void* const event_log) noexcept {
    (void)event_log;
    return 1;
}

TL_ADVAPI_MSABI int tl_ReportEventW(void* const event_log, const std::uint16_t type, const std::uint16_t category,
                                    const std::uint32_t event_id, void* const user_sid,
                                    const std::uint16_t num_strings, const std::uint32_t data_size,
                                    const wchar_t** const strings, void* const raw_data) noexcept {
    (void)event_log;
    (void)type;
    (void)category;
    (void)event_id;
    (void)user_sid;
    (void)num_strings;
    (void)data_size;
    (void)strings;
    (void)raw_data;
    return 1;
}

TL_ADVAPI_MSABI int tl_CryptCreateHash(const std::uintptr_t prov, const std::uint32_t algid,
                                       const std::uintptr_t key, const std::uint32_t flags,
                                       std::uintptr_t* const hash) noexcept {
    (void)prov;
    (void)algid;
    (void)key;
    (void)flags;
    if (hash == nullptr ||
        !write_guest_value(hash, static_cast<std::uintptr_t>(0x48415348ULL))) {
        tl_SetLastError(kErrorInvalidParameter);
        return 0;
    }
    tl_SetLastError(static_cast<std::uint32_t>(abi::kErrorSuccess));
    return 1;
}

TL_ADVAPI_MSABI int tl_CryptHashData(const std::uintptr_t hash, const std::uint8_t* const data,
                                     const std::uint32_t data_len, const std::uint32_t flags) noexcept {
    (void)hash;
    (void)data;
    (void)data_len;
    (void)flags;
    tl_SetLastError(static_cast<std::uint32_t>(abi::kErrorSuccess));
    return 1;
}

TL_ADVAPI_MSABI int tl_CryptGetHashParam(const std::uintptr_t hash, const std::uint32_t param,
                                         std::uint8_t* const data, std::uint32_t* const data_len,
                                         const std::uint32_t flags) noexcept {
    (void)hash;
    (void)param;
    (void)flags;
    if (data_len == nullptr) {
        tl_SetLastError(kErrorInvalidParameter);
        return 0;
    }
    constexpr std::uint32_t hash_size = 32; // SHA-256 size
    std::uint32_t capacity = 0;
    if (!read_guest_value(data_len, capacity)) {
        tl_SetLastError(kErrorInvalidParameter);
        return 0;
    }
    if (data == nullptr || capacity < hash_size) {
        if (!write_guest_value(data_len, hash_size)) {
            tl_SetLastError(kErrorInvalidParameter);
            return 0;
        }
        tl_SetLastError(static_cast<std::uint32_t>(abi::kErrorSuccess));
        return 1;
    }
    const std::array<std::uint8_t, hash_size> hash_bytes = [] {
        std::array<std::uint8_t, hash_size> result{};
        result.fill(0xAA);
        return result;
    }();
    if (runtime::write_guest_memory(data, hash_bytes.data(), hash_bytes.size()).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        !write_guest_value(data_len, hash_size)) {
        tl_SetLastError(kErrorInvalidParameter);
        return 0;
    }
    tl_SetLastError(static_cast<std::uint32_t>(abi::kErrorSuccess));
    return 1;
}

TL_ADVAPI_MSABI int tl_CryptSetHashParam(const std::uintptr_t hash, const std::uint32_t param,
                                         const std::uint8_t* const data, const std::uint32_t flags) noexcept {
    (void)hash;
    (void)param;
    (void)data;
    (void)flags;
    tl_SetLastError(static_cast<std::uint32_t>(abi::kErrorSuccess));
    return 1;
}

TL_ADVAPI_MSABI int tl_CryptDestroyHash(const std::uintptr_t hash) noexcept {
    (void)hash;
    tl_SetLastError(static_cast<std::uint32_t>(abi::kErrorSuccess));
    return 1;
}

TL_ADVAPI_MSABI int tl_CryptSignHashW(const std::uintptr_t hash, const std::uint32_t key_spec,
                                      const wchar_t* const description, const std::uint32_t flags,
                                      std::uint8_t* const signature, std::uint32_t* const sig_len) noexcept {
    (void)hash;
    (void)key_spec;
    (void)description;
    (void)flags;
    if (sig_len == nullptr) {
        tl_SetLastError(kErrorInvalidParameter);
        return 0;
    }
    constexpr std::uint32_t dummy_sig_len = 256;
    std::uint32_t capacity = 0;
    if (!read_guest_value(sig_len, capacity)) {
        tl_SetLastError(kErrorInvalidParameter);
        return 0;
    }
    if (signature == nullptr || capacity < dummy_sig_len) {
        if (!write_guest_value(sig_len, dummy_sig_len)) {
            tl_SetLastError(kErrorInvalidParameter);
            return 0;
        }
        return 1;
    }
    std::array<std::uint8_t, dummy_sig_len> signature_bytes{};
    signature_bytes.fill(0x55);
    if (runtime::write_guest_memory(signature, signature_bytes.data(), signature_bytes.size()).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        !write_guest_value(sig_len, dummy_sig_len)) {
        tl_SetLastError(kErrorInvalidParameter);
        return 0;
    }
    tl_SetLastError(static_cast<std::uint32_t>(abi::kErrorSuccess));
    return 1;
}

TL_ADVAPI_MSABI int tl_CryptDecrypt(const std::uintptr_t key, const std::uintptr_t hash,
                                    const int final_chunk, const std::uint32_t flags,
                                    std::uint8_t* const data, std::uint32_t* const data_len) noexcept {
    (void)key;
    (void)hash;
    (void)final_chunk;
    (void)flags;
    (void)data;
    (void)data_len;
    tl_SetLastError(static_cast<std::uint32_t>(abi::kErrorSuccess));
    return 1;
}

TL_ADVAPI_MSABI int tl_CryptExportKey(const std::uintptr_t key, const std::uintptr_t exp_key,
                                      const std::uint32_t blob_type, const std::uint32_t flags,
                                      std::uint8_t* const data, std::uint32_t* const data_len) noexcept {
    (void)key;
    (void)exp_key;
    (void)blob_type;
    (void)flags;
    if (data_len == nullptr) {
        tl_SetLastError(kErrorInvalidParameter);
        return 0;
    }
    constexpr std::uint32_t key_blob_size = 64;
    std::uint32_t capacity = 0;
    if (!read_guest_value(data_len, capacity)) {
        tl_SetLastError(kErrorInvalidParameter);
        return 0;
    }
    if (data == nullptr || capacity < key_blob_size) {
        if (!write_guest_value(data_len, key_blob_size)) {
            tl_SetLastError(kErrorInvalidParameter);
            return 0;
        }
        return 1;
    }
    std::array<std::uint8_t, key_blob_size> key_blob{};
    key_blob.fill(0x11);
    if (runtime::write_guest_memory(data, key_blob.data(), key_blob.size()).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        !write_guest_value(data_len, key_blob_size)) {
        tl_SetLastError(kErrorInvalidParameter);
        return 0;
    }
    tl_SetLastError(static_cast<std::uint32_t>(abi::kErrorSuccess));
    return 1;
}

TL_ADVAPI_MSABI int tl_CryptGetUserKey(const std::uintptr_t prov, const std::uint32_t key_spec,
                                       std::uintptr_t* const user_key) noexcept {
    (void)prov;
    (void)key_spec;
    if (user_key == nullptr ||
        !write_guest_value(user_key, static_cast<std::uintptr_t>(0x4B455931ULL))) {
        tl_SetLastError(kErrorInvalidParameter);
        return 0;
    }
    tl_SetLastError(static_cast<std::uint32_t>(abi::kErrorSuccess));
    return 1;
}

TL_ADVAPI_MSABI int tl_CryptGetProvParam(const std::uintptr_t prov, const std::uint32_t param,
                                         std::uint8_t* const data, std::uint32_t* const data_len,
                                         const std::uint32_t flags) noexcept {
    (void)prov;
    (void)param;
    (void)flags;
    if (data_len == nullptr) {
        tl_SetLastError(kErrorInvalidParameter);
        return 0;
    }
    constexpr std::uint32_t param_size = 16;
    std::uint32_t capacity = 0;
    if (!read_guest_value(data_len, capacity)) {
        tl_SetLastError(kErrorInvalidParameter);
        return 0;
    }
    if (data == nullptr || capacity < param_size) {
        if (!write_guest_value(data_len, param_size)) {
            tl_SetLastError(kErrorInvalidParameter);
            return 0;
        }
        return 1;
    }
    const std::array<std::uint8_t, param_size> parameter{};
    if (runtime::write_guest_memory(data, parameter.data(), parameter.size()).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        !write_guest_value(data_len, param_size)) {
        tl_SetLastError(kErrorInvalidParameter);
        return 0;
    }
    tl_SetLastError(static_cast<std::uint32_t>(abi::kErrorSuccess));
    return 1;
}

TL_ADVAPI_MSABI int tl_CryptDestroyKey(const std::uintptr_t key) noexcept {
    (void)key;
    tl_SetLastError(static_cast<std::uint32_t>(abi::kErrorSuccess));
    return 1;
}

TL_ADVAPI_MSABI int tl_CryptEnumProvidersW(const std::uint32_t index, std::uint32_t* const reserved,
                                           const std::uint32_t flags, std::uint32_t* const prov_type,
                                           wchar_t* const prov_name, std::uint32_t* const name_len) noexcept {
    (void)reserved;
    (void)flags;
    if (index > 0 || name_len == nullptr) {
        tl_SetLastError(259); // ERROR_NO_MORE_ITEMS
        return 0;
    }
    const wchar_t dummy_name[] = L"Microsoft Enhanced RSA and AES Cryptographic Provider";
    const std::uint32_t len = static_cast<std::uint32_t>(std::wcslen(dummy_name)) + 1;
    if (prov_type != nullptr) {
        *prov_type = 24; // PROV_RSA_AES
    }
    if (prov_name == nullptr || *name_len < len) {
        *name_len = len;
        return 1;
    }
    std::memcpy(prov_name, dummy_name, len * sizeof(wchar_t));
    *name_len = len;
    tl_SetLastError(static_cast<std::uint32_t>(abi::kErrorSuccess));
    return 1;
}

TL_ADVAPI_MSABI int tl_SystemFunction036(void* const buffer, const std::uint32_t length) noexcept {
    if (buffer == nullptr || length == 0) {
        return 0;
    }
    try {
        std::vector<std::uint8_t> random_bytes(length);
        std::ifstream urandom{"/dev/urandom", std::ios::binary};
        if (urandom) {
            urandom.read(reinterpret_cast<char*>(random_bytes.data()), length);
            if (urandom.gcount() != static_cast<std::streamsize>(length)) {
                for (std::uint8_t& byte : random_bytes) {
                    byte = static_cast<std::uint8_t>(std::rand() & 0xFF);
                }
            }
        } else {
            for (std::uint8_t& byte : random_bytes) {
                byte = static_cast<std::uint8_t>(std::rand() & 0xFF);
            }
        }
        return runtime::write_guest_memory(buffer, random_bytes.data(), random_bytes.size()).status ==
                       runtime::GuestMemoryAccessStatus::Success
                   ? 1
                   : 0;
    } catch (...) {
        return 0;
    }
}

TL_ADVAPI_MSABI int tl_GetUserNameA(char* const buffer, std::uint32_t* const size) noexcept {
    static const char kUser[] = "Tonho";
    constexpr std::uint32_t kLen = 6;
    if (size == nullptr) {
        tl_SetLastError(abi::kErrorInvalidParameter);
        return 0;
    }
    std::uint32_t capacity = 0;
    if (!read_guest_value(size, capacity)) {
        tl_SetLastError(abi::kErrorInvalidParameter);
        return 0;
    }
    if (capacity < kLen) {
        if (!write_guest_value(size, kLen)) {
            tl_SetLastError(abi::kErrorInvalidParameter);
            return 0;
        }
        tl_SetLastError(122); // ERROR_INSUFFICIENT_BUFFER
        return 0;
    }
    if (buffer != nullptr &&
        runtime::write_guest_memory(buffer, kUser, kLen).status !=
            runtime::GuestMemoryAccessStatus::Success) {
        tl_SetLastError(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!write_guest_value(size, kLen)) {
        tl_SetLastError(abi::kErrorInvalidParameter);
        return 0;
    }
    tl_SetLastError(abi::kErrorSuccess);
    return 1;
}

TL_ADVAPI_MSABI int tl_SetSecurityDescriptorOwner(void* const sec_desc, void* const owner, const int owner_defaulted) noexcept {
    (void)sec_desc;
    (void)owner;
    (void)owner_defaulted;
    tl_SetLastError(abi::kErrorSuccess);
    return 1;
}

TL_ADVAPI_MSABI int tl_IsTextUnicode(const void* const lpv, const int iSize, int* const lpiResult) noexcept {
    if (lpv == nullptr || iSize < 2 || !mapped_range(lpv, static_cast<std::size_t>(iSize), false)) {
        if (lpiResult != nullptr && mapped_range(lpiResult, sizeof(int), true)) {
            *lpiResult = 0;
        }
        return 0;
    }
    const auto* const bytes = static_cast<const std::uint8_t*>(lpv);
    bool is_unicode = false;
    if (iSize >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        is_unicode = true;
    } else if (iSize >= 2 && bytes[1] == 0x00 && bytes[0] != 0x00) {
        is_unicode = true;
    }
    if (lpiResult != nullptr && mapped_range(lpiResult, sizeof(int), true)) {
        *lpiResult = is_unicode ? 1 : 0;
    }
    return is_unicode ? 1 : 0;
}

}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_advapi32_module() {
    static const ExportedFunction kAdvapi32Exports[] = {
        {"RegCloseKey", 1, reinterpret_cast<std::uintptr_t>(&tl_RegCloseKey), ExportSupport::Full},
        {"RegDeleteValueA", 2, reinterpret_cast<std::uintptr_t>(&tl_RegDeleteValueA), ExportSupport::Full},
        {"RegDeleteValueW", 6, reinterpret_cast<std::uintptr_t>(&tl_RegDeleteValueW), ExportSupport::Full},
        {"RegCreateKeyExA", 7, reinterpret_cast<std::uintptr_t>(&tl_RegCreateKeyExA), ExportSupport::Full},
        {"RegCreateKeyExW", 8, reinterpret_cast<std::uintptr_t>(&tl_RegCreateKeyExW), ExportSupport::Full},
        {"RegOpenKeyExA", 3, reinterpret_cast<std::uintptr_t>(&tl_RegOpenKeyExA), ExportSupport::Full},
        {"RegOpenKeyExW", 9, reinterpret_cast<std::uintptr_t>(&tl_RegOpenKeyExW), ExportSupport::Full},
        {"RegQueryValueExA", 4, reinterpret_cast<std::uintptr_t>(&tl_RegQueryValueExA), ExportSupport::Full},
        {"RegQueryValueExW", 10, reinterpret_cast<std::uintptr_t>(&tl_RegQueryValueExW), ExportSupport::Full},
        {"RegSetValueExA", 5, reinterpret_cast<std::uintptr_t>(&tl_RegSetValueExA), ExportSupport::Full},
        {"RegSetValueExW", 11, reinterpret_cast<std::uintptr_t>(&tl_RegSetValueExW), ExportSupport::Full},
        {"CryptAcquireContextA", 12, reinterpret_cast<std::uintptr_t>(&tl_CryptAcquireContextA), ExportSupport::Full},
        {"CryptAcquireContextW", 13, reinterpret_cast<std::uintptr_t>(&tl_CryptAcquireContextW), ExportSupport::Full},
        {"CryptGenRandom", 14, reinterpret_cast<std::uintptr_t>(&tl_CryptGenRandom), ExportSupport::Full},
        {"CryptReleaseContext", 15, reinterpret_cast<std::uintptr_t>(&tl_CryptReleaseContext), ExportSupport::Full},
        {"OpenProcessToken", 16, reinterpret_cast<std::uintptr_t>(&tl_OpenProcessToken), ExportSupport::Full},
        {"GetTokenInformation", 17, reinterpret_cast<std::uintptr_t>(&tl_GetTokenInformation), ExportSupport::Full},
        {"AllocateAndInitializeSid", 18, reinterpret_cast<std::uintptr_t>(&tl_AllocateAndInitializeSid), ExportSupport::Full},
        {"FreeSid", 19, reinterpret_cast<std::uintptr_t>(&tl_FreeSid), ExportSupport::Full},
        {"GetLengthSid", 20, reinterpret_cast<std::uintptr_t>(&tl_GetLengthSid), ExportSupport::Full},
        {"CopySid", 21, reinterpret_cast<std::uintptr_t>(&tl_CopySid), ExportSupport::Full},
        {"EqualSid", 22, reinterpret_cast<std::uintptr_t>(&tl_EqualSid), ExportSupport::Full},
        {"IsValidSid", 23, reinterpret_cast<std::uintptr_t>(&tl_IsValidSid), ExportSupport::Full},
        {"CreateWellKnownSid", 24, reinterpret_cast<std::uintptr_t>(&tl_CreateWellKnownSid), ExportSupport::Full},
        {"CheckTokenMembership", 25, reinterpret_cast<std::uintptr_t>(&tl_CheckTokenMembership), ExportSupport::Full},
        {"BuildTrusteeWithSidW", 26, reinterpret_cast<std::uintptr_t>(&tl_BuildTrusteeWithSidW), ExportSupport::Full},
        {"InitializeSecurityDescriptor", 27, reinterpret_cast<std::uintptr_t>(&tl_InitializeSecurityDescriptor), ExportSupport::Full},
        {"SetSecurityDescriptorDacl", 28, reinterpret_cast<std::uintptr_t>(&tl_SetSecurityDescriptorDacl), ExportSupport::Full},
        {"SetEntriesInAclW", 29, reinterpret_cast<std::uintptr_t>(&tl_SetEntriesInAclW), ExportSupport::Full},
        {"GetNamedSecurityInfoW", 30, reinterpret_cast<std::uintptr_t>(&tl_GetNamedSecurityInfoW), ExportSupport::Full},
        {"SetNamedSecurityInfoW", 31, reinterpret_cast<std::uintptr_t>(&tl_SetNamedSecurityInfoW), ExportSupport::Full},
        {"SetFileSecurityW", 32, reinterpret_cast<std::uintptr_t>(&tl_SetFileSecurityW), ExportSupport::Full},
        {"LookupPrivilegeValueW", 33, reinterpret_cast<std::uintptr_t>(&tl_LookupPrivilegeValueW), ExportSupport::Full},
        {"AdjustTokenPrivileges", 34, reinterpret_cast<std::uintptr_t>(&tl_AdjustTokenPrivileges), ExportSupport::Full},
        {"GetFileSecurityW", 35, reinterpret_cast<std::uintptr_t>(&tl_GetFileSecurityW), ExportSupport::Full},
        {"RegDeleteTreeW", 36, reinterpret_cast<std::uintptr_t>(&tl_RegDeleteTreeW), ExportSupport::Full},
        {"RegEnumValueW", 37, reinterpret_cast<std::uintptr_t>(&tl_RegEnumValueW), ExportSupport::Full},
        {"RegEnumKeyExW", 38, reinterpret_cast<std::uintptr_t>(&tl_RegEnumKeyExW), ExportSupport::Full},
        {"RegDeleteKeyExW", 39, reinterpret_cast<std::uintptr_t>(&tl_RegDeleteKeyExW), ExportSupport::Full},
        {"RegDeleteKeyW", 40, reinterpret_cast<std::uintptr_t>(&tl_RegDeleteKeyW), ExportSupport::Full},
        {"GetUserNameW", 41, reinterpret_cast<std::uintptr_t>(&tl_GetUserNameW), ExportSupport::Full},
        {"LookupAccountNameW", 42, reinterpret_cast<std::uintptr_t>(&tl_LookupAccountNameW), ExportSupport::Full},
        {"LsaOpenPolicy", 43, reinterpret_cast<std::uintptr_t>(&tl_LsaOpenPolicy), ExportSupport::Full},
        {"LsaClose", 44, reinterpret_cast<std::uintptr_t>(&tl_LsaClose), ExportSupport::Full},
        {"LsaAddAccountRights", 45, reinterpret_cast<std::uintptr_t>(&tl_LsaAddAccountRights), ExportSupport::Full},
        {"RegQueryInfoKeyA", 46, reinterpret_cast<std::uintptr_t>(&tl_RegQueryInfoKeyA), ExportSupport::Full},
        {"RegQueryInfoKeyW", 47, reinterpret_cast<std::uintptr_t>(&tl_RegQueryInfoKeyW), ExportSupport::Full},
        {"RegEnumKeyA", 48, reinterpret_cast<std::uintptr_t>(&tl_RegEnumKeyA), ExportSupport::Full},
        {"RegEnumValueA", 49, reinterpret_cast<std::uintptr_t>(&tl_RegEnumValueA), ExportSupport::Full},
        {"RegDeleteKeyA", 50, reinterpret_cast<std::uintptr_t>(&tl_RegDeleteKeyA), ExportSupport::Full},
        {"RegGetValueW", 51, reinterpret_cast<std::uintptr_t>(&tl_RegGetValueW), ExportSupport::Full},
        {"RegisterEventSourceW", 52, reinterpret_cast<std::uintptr_t>(&tl_RegisterEventSourceW), ExportSupport::Full},
        {"DeregisterEventSource", 53, reinterpret_cast<std::uintptr_t>(&tl_DeregisterEventSource), ExportSupport::Full},
        {"ReportEventW", 54, reinterpret_cast<std::uintptr_t>(&tl_ReportEventW), ExportSupport::Full},
        {"CryptCreateHash", 55, reinterpret_cast<std::uintptr_t>(&tl_CryptCreateHash), ExportSupport::Full},
        {"CryptHashData", 56, reinterpret_cast<std::uintptr_t>(&tl_CryptHashData), ExportSupport::Full},
        {"CryptGetHashParam", 57, reinterpret_cast<std::uintptr_t>(&tl_CryptGetHashParam), ExportSupport::Full},
        {"CryptSetHashParam", 58, reinterpret_cast<std::uintptr_t>(&tl_CryptSetHashParam), ExportSupport::Full},
        {"CryptDestroyHash", 59, reinterpret_cast<std::uintptr_t>(&tl_CryptDestroyHash), ExportSupport::Full},
        {"CryptSignHashW", 60, reinterpret_cast<std::uintptr_t>(&tl_CryptSignHashW), ExportSupport::Full},
        {"CryptDecrypt", 61, reinterpret_cast<std::uintptr_t>(&tl_CryptDecrypt), ExportSupport::Full},
        {"CryptExportKey", 62, reinterpret_cast<std::uintptr_t>(&tl_CryptExportKey), ExportSupport::Full},
        {"CryptGetUserKey", 63, reinterpret_cast<std::uintptr_t>(&tl_CryptGetUserKey), ExportSupport::Full},
        {"CryptGetProvParam", 64, reinterpret_cast<std::uintptr_t>(&tl_CryptGetProvParam), ExportSupport::Full},
        {"CryptDestroyKey", 65, reinterpret_cast<std::uintptr_t>(&tl_CryptDestroyKey), ExportSupport::Full},
        {"CryptEnumProvidersW", 66, reinterpret_cast<std::uintptr_t>(&tl_CryptEnumProvidersW), ExportSupport::Full},
        {"SystemFunction036", 67, reinterpret_cast<std::uintptr_t>(&tl_SystemFunction036), ExportSupport::Full},
        {"GetUserNameA", 68, reinterpret_cast<std::uintptr_t>(&tl_GetUserNameA), ExportSupport::Full},
        {"SetSecurityDescriptorOwner", 69, reinterpret_cast<std::uintptr_t>(&tl_SetSecurityDescriptorOwner), ExportSupport::Full},
        {"IsTextUnicode", 70, reinterpret_cast<std::uintptr_t>(&tl_IsTextUnicode), ExportSupport::Full},
    };
    static const InternalModule kAdvapi32Module{"ADVAPI32.dll", kAdvapi32Exports};
    register_module(kAdvapi32Module);
}

}  // namespace tradutorlinux::loader
