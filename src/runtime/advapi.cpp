#include "tradutorlinux/runtime/advapi.hpp"

#include "tradutorlinux/runtime/winapi.hpp"

#include <cstring>
#include <fstream>
#include <string>

namespace tradutorlinux {
namespace {

constexpr std::uintptr_t kHkeyCurrentUser = 0x80000001U;
constexpr std::uint32_t kRegSz = 1;
constexpr std::uint32_t kErrorFileNotFound = 2;
constexpr std::uint32_t kErrorMoreData = 234;
constexpr char kRunKey[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr char kAutorunValue[] = "TodoApp";

struct RegistryKey {
    bool open{false};
};

RegistryKey g_run_key;
bool g_loaded = false;
std::string g_autorun;

void load_registry() noexcept {
    if (g_loaded) {
        return;
    }
    g_loaded = true;
    std::ifstream input{".tl_registry_todo"};
    std::getline(input, g_autorun);
}

void save_registry() noexcept {
    std::ofstream output{".tl_registry_todo", std::ios::trunc};
    if (output) {
        output << g_autorun << '\n';
    }
}

bool is_current_user(const void* key) noexcept {
    return reinterpret_cast<std::uintptr_t>(key) == kHkeyCurrentUser;
}

bool is_open_key(const void* key) noexcept {
    return key == &g_run_key && g_run_key.open;
}

}  // namespace

TL_ADVAPI_MSABI std::uint32_t tl_RegCloseKey(const void* key) noexcept {
    if (!is_open_key(key)) {
        return abi::kErrorInvalidHandle;
    }
    g_run_key.open = false;
    return abi::kErrorSuccess;
}

TL_ADVAPI_MSABI std::uint32_t tl_RegOpenKeyExA(const void* key, const char* subkey,
                                                const std::uint32_t options,
                                                const std::uint32_t access,
                                                void** result) noexcept {
    (void)options;
    (void)access;
    if (!is_current_user(key) || subkey == nullptr || result == nullptr ||
        std::strcmp(subkey, kRunKey) != 0) {
        return kErrorFileNotFound;
    }
    load_registry();
    g_run_key.open = true;
    *result = &g_run_key;
    return abi::kErrorSuccess;
}

TL_ADVAPI_MSABI std::uint32_t tl_RegQueryValueExA(const void* key, const char* value_name,
                                                   std::uint32_t* reserved, std::uint32_t* type,
                                                   unsigned char* data,
                                                   std::uint32_t* data_size) noexcept {
    (void)reserved;
    if (!is_open_key(key) || value_name == nullptr || data_size == nullptr ||
        std::strcmp(value_name, kAutorunValue) != 0 || g_autorun.empty()) {
        return kErrorFileNotFound;
    }
    const std::uint32_t required = static_cast<std::uint32_t>(g_autorun.size() + 1U);
    if (type != nullptr) {
        *type = kRegSz;
    }
    if (data == nullptr || *data_size < required) {
        *data_size = required;
        return data == nullptr ? abi::kErrorSuccess : kErrorMoreData;
    }
    std::memcpy(data, g_autorun.c_str(), required);
    *data_size = required;
    return abi::kErrorSuccess;
}

TL_ADVAPI_MSABI std::uint32_t tl_RegSetValueExA(const void* key, const char* value_name,
                                                 const std::uint32_t reserved,
                                                 const std::uint32_t type,
                                                 const unsigned char* data,
                                                 const std::uint32_t data_size) noexcept {
    (void)reserved;
    if (!is_open_key(key) || value_name == nullptr || data == nullptr || type != kRegSz ||
        std::strcmp(value_name, kAutorunValue) != 0 || data_size == 0) {
        return abi::kErrorInvalidParameter;
    }
    const std::size_t length = strnlen(reinterpret_cast<const char*>(data), data_size);
    g_autorun.assign(reinterpret_cast<const char*>(data), length);
    save_registry();
    return abi::kErrorSuccess;
}

TL_ADVAPI_MSABI std::uint32_t tl_RegDeleteValueA(const void* key, const char* value_name) noexcept {
    if (!is_open_key(key) || value_name == nullptr || std::strcmp(value_name, kAutorunValue) != 0) {
        return kErrorFileNotFound;
    }
    if (g_autorun.empty()) {
        return kErrorFileNotFound;
    }
    g_autorun.clear();
    save_registry();
    return abi::kErrorSuccess;
}

}  // namespace tradutorlinux
