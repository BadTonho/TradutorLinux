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
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <sys/types.h>

#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/util/unicode.hpp"

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

std::mutex g_registry_mutex;
std::array<RegistryKey, 256> g_keys{};
std::vector<RegistryValue> g_values;
bool g_loaded = false;

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

inline bool mapped_cstring(const char* value) noexcept {
    return runtime::validate_mapped_cstring(value);
}

inline bool mapped_wstring(const std::uint16_t* value) noexcept {
    return runtime::validate_mapped_wstring(value);
}

std::string wide_to_utf8(const std::uint16_t* value) {
    if (!mapped_wstring(value)) {
        return {};
    }
    return util::wide_to_utf8(value);
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
    if (result == nullptr || !mapped_range(result, sizeof(*result), true) ||
        (disposition != nullptr && !mapped_range(disposition, sizeof(*disposition), true))) {
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
    *result = &*free_it;
    if (disposition != nullptr) {
        *disposition = existed ? 2U : 1U;
    }
    return abi::kErrorSuccess;
}

std::uint32_t set_value(const void* key, const std::string& name, const std::uint32_t type,
                        const unsigned char* data, const std::uint32_t data_size) noexcept {
    if (data_size != 0 && (data == nullptr || !mapped_range(data, data_size, false))) {
        return abi::kErrorInvalidParameter;
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
        replacement.data.assign(data, data + data_size);
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
                          unsigned char* data, std::uint32_t* data_size) noexcept {
    if (data_size == nullptr || !mapped_range(data_size, sizeof(*data_size), true) ||
        (type != nullptr && !mapped_range(type, sizeof(*type), true))) {
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
    if ((security_attributes != nullptr && !mapped_range(security_attributes, sizeof(std::uint32_t), false)) || !mapped_cstring(subkey)) {
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
    if ((security_attributes != nullptr && !mapped_range(security_attributes, sizeof(std::uint32_t), false)) || !mapped_wstring(subkey)) {
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

TL_ADVAPI_MSABI int tl_CryptAcquireContextA(void** prov_handle, const char* container,
                                            const char* provider, const std::uint32_t prov_type,
                                            const std::uint32_t flags) noexcept {
    (void)container;
    (void)provider;
    (void)prov_type;
    (void)flags;
    if (prov_handle == nullptr || !mapped_range(prov_handle, sizeof(void*), true)) {
        return 0;
    }
    static char g_crypto_provider_token = 0;
    *prov_handle = &g_crypto_provider_token;
    return 1;
}

TL_ADVAPI_MSABI int tl_CryptAcquireContextW(void** prov_handle, const std::uint16_t* container,
                                            const std::uint16_t* provider, const std::uint32_t prov_type,
                                            const std::uint32_t flags) noexcept {
    (void)container;
    (void)provider;
    (void)prov_type;
    (void)flags;
    if (prov_handle == nullptr || !mapped_range(prov_handle, sizeof(void*), true)) {
        return 0;
    }
    static char g_crypto_provider_token = 0;
    *prov_handle = &g_crypto_provider_token;
    return 1;
}

TL_ADVAPI_MSABI int tl_CryptGenRandom(void* prov_handle, const std::uint32_t length,
                                      std::uint8_t* buffer) noexcept {
    if (prov_handle == nullptr || buffer == nullptr || length == 0 ||
        !mapped_range(buffer, length, true)) {
        return 0;
    }
    std::ifstream urandom{"/dev/urandom", std::ios::binary};
    if (urandom) {
        urandom.read(reinterpret_cast<char*>(buffer), length);
        if (urandom.gcount() == static_cast<std::streamsize>(length)) {
            return 1;
        }
    }
    for (std::uint32_t i = 0; i < length; ++i) {
        buffer[i] = static_cast<std::uint8_t>(std::rand() & 0xFF);
    }
    return 1;
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
    if (luid == nullptr || !mapped_range(luid, sizeof(GuestLuid), true)) {
        return 0;
    }
    GuestLuid out{};
    out.low_part = 1;
    out.high_part = 0;
    std::memcpy(luid, &out, sizeof(GuestLuid));
    return 1;
}

TL_ADVAPI_MSABI int tl_AdjustTokenPrivileges(void* token_handle, int disable_all_privileges,
                                             void* new_state, std::uint32_t buffer_length,
                                             void* previous_state, std::uint32_t* return_length) noexcept {
    (void)token_handle;
    (void)disable_all_privileges;
    (void)new_state;
    (void)buffer_length;
    (void)previous_state;
    if (return_length != nullptr && mapped_range(return_length, sizeof(*return_length), true)) {
        *return_length = 0;
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
    if (length_needed != nullptr && mapped_range(length_needed, sizeof(*length_needed), true)) {
        *length_needed = 0;
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
    if (*size < kLen) {
        *size = kLen;
        tl_SetLastError(122); // ERROR_INSUFFICIENT_BUFFER
        return 0;
    }
    if (buffer != nullptr && mapped_range(buffer, kLen * sizeof(std::uint16_t), true)) {
        std::memcpy(buffer, kUser, kLen * sizeof(std::uint16_t));
    }
    *size = kLen;
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
    if (domain_size != nullptr && mapped_range(domain_size, sizeof(std::uint32_t), true)) {
        *domain_size = 10;
        if (referenced_domain != nullptr && mapped_range(referenced_domain, 10 * sizeof(std::uint16_t), true)) {
            std::memcpy(referenced_domain, kDomain, sizeof(kDomain));
        }
    }
    if (sid_size != nullptr && mapped_range(sid_size, sizeof(std::uint32_t), true)) {
        *sid_size = 28;
        if (sid != nullptr && mapped_range(sid, 28, true)) {
            std::memset(sid, 0, 28);
        }
    }
    if (sid_name_use != nullptr && mapped_range(sid_name_use, sizeof(std::uint32_t), true)) {
        *static_cast<std::uint32_t*>(sid_name_use) = 1; // SidTypeUser
    }
    tl_SetLastError(abi::kErrorSuccess);
    return 1;
}

TL_ADVAPI_MSABI int tl_LsaOpenPolicy(void* const system_name, void* const obj_attributes,
                                    const std::uint32_t access_mask, void** const policy_handle) noexcept {
    (void)system_name;
    (void)obj_attributes;
    (void)access_mask;
    if (policy_handle != nullptr && mapped_range(policy_handle, sizeof(void*), true)) {
        *policy_handle = reinterpret_cast<void*>(0x4C534150ULL); // 'LSAP'
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
    if (sub_keys != nullptr && mapped_range(sub_keys, sizeof(std::uint32_t), true)) {
        *sub_keys = 0;
    }
    if (max_sub_key_len != nullptr && mapped_range(max_sub_key_len, sizeof(std::uint32_t), true)) {
        *max_sub_key_len = 0;
    }
    if (values != nullptr && mapped_range(values, sizeof(std::uint32_t), true)) {
        *values = 0;
    }
    if (max_value_name_len != nullptr && mapped_range(max_value_name_len, sizeof(std::uint32_t), true)) {
        *max_value_name_len = 0;
    }
    if (max_value_len != nullptr && mapped_range(max_value_len, sizeof(std::uint32_t), true)) {
        *max_value_len = 0;
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
    if (sub_keys != nullptr && mapped_range(sub_keys, sizeof(std::uint32_t), true)) {
        *sub_keys = 0;
    }
    if (max_sub_key_len != nullptr && mapped_range(max_sub_key_len, sizeof(std::uint32_t), true)) {
        *max_sub_key_len = 0;
    }
    if (values != nullptr && mapped_range(values, sizeof(std::uint32_t), true)) {
        *values = 0;
    }
    if (max_value_name_len != nullptr && mapped_range(max_value_name_len, sizeof(std::uint32_t), true)) {
        *max_value_name_len = 0;
    }
    if (max_value_len != nullptr && mapped_range(max_value_len, sizeof(std::uint32_t), true)) {
        *max_value_len = 0;
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
    if (hash == nullptr) {
        tl_SetLastError(kErrorInvalidParameter);
        return 0;
    }
    *hash = 0x48415348ULL; // 'HASH'
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
    if (data == nullptr || *data_len < hash_size) {
        *data_len = hash_size;
        tl_SetLastError(static_cast<std::uint32_t>(abi::kErrorSuccess));
        return 1;
    }
    std::memset(data, 0xAA, hash_size);
    *data_len = hash_size;
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
    if (signature == nullptr || *sig_len < dummy_sig_len) {
        *sig_len = dummy_sig_len;
        return 1;
    }
    std::memset(signature, 0x55, dummy_sig_len);
    *sig_len = dummy_sig_len;
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
    if (data == nullptr || *data_len < key_blob_size) {
        *data_len = key_blob_size;
        return 1;
    }
    std::memset(data, 0x11, key_blob_size);
    *data_len = key_blob_size;
    tl_SetLastError(static_cast<std::uint32_t>(abi::kErrorSuccess));
    return 1;
}

TL_ADVAPI_MSABI int tl_CryptGetUserKey(const std::uintptr_t prov, const std::uint32_t key_spec,
                                       std::uintptr_t* const user_key) noexcept {
    (void)prov;
    (void)key_spec;
    if (user_key == nullptr) {
        tl_SetLastError(kErrorInvalidParameter);
        return 0;
    }
    *user_key = 0x4B455931ULL; // 'KEY1'
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
    if (data == nullptr || *data_len < param_size) {
        *data_len = param_size;
        return 1;
    }
    std::memset(data, 0, param_size);
    *data_len = param_size;
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
    if (buffer == nullptr || length == 0 || !mapped_range(buffer, length, true)) {
        return 0;
    }
    std::ifstream urandom{"/dev/urandom", std::ios::binary};
    if (urandom) {
        urandom.read(reinterpret_cast<char*>(buffer), length);
        if (urandom.gcount() == static_cast<std::streamsize>(length)) {
            return 1;
        }
    }
    for (std::uint32_t i = 0; i < length; ++i) {
        static_cast<std::uint8_t*>(buffer)[i] = static_cast<std::uint8_t>(std::rand() & 0xFF);
    }
    return 1;
}

TL_ADVAPI_MSABI int tl_GetUserNameA(char* const buffer, std::uint32_t* const size) noexcept {
    static const char kUser[] = "Tonho";
    constexpr std::uint32_t kLen = 6;
    if (size == nullptr) {
        tl_SetLastError(abi::kErrorInvalidParameter);
        return 0;
    }
    if (*size < kLen) {
        *size = kLen;
        tl_SetLastError(122); // ERROR_INSUFFICIENT_BUFFER
        return 0;
    }
    if (buffer != nullptr && mapped_range(buffer, kLen, true)) {
        std::memcpy(buffer, kUser, kLen);
    }
    *size = kLen;
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
