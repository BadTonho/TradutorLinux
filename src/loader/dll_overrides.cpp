#include "tradutorlinux/loader/dll_overrides.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <sstream>

namespace tradutorlinux::loader {
namespace {

std::string normalize_dll_name(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    for (const char c : name) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (!result.ends_with(".dll")) {
        result += ".dll";
    }
    return result;
}

std::mutex g_overrides_mutex;
std::unordered_map<std::string, DllOverrideMode> g_overrides;
bool g_env_loaded = false;

}  // namespace

void set_dll_override(std::string_view dll_name, DllOverrideMode mode) noexcept {
    const std::string normalized = normalize_dll_name(dll_name);
    std::lock_guard<std::mutex> lock(g_overrides_mutex);
    g_overrides[normalized] = mode;
}

DllOverrideMode get_dll_override(std::string_view dll_name) noexcept {
    load_dll_overrides_from_env();
    const std::string normalized = normalize_dll_name(dll_name);
    std::lock_guard<std::mutex> lock(g_overrides_mutex);
    const auto it = g_overrides.find(normalized);
    if (it != g_overrides.end()) {
        return it->second;
    }
    return DllOverrideMode::BuiltinFirst;
}

void load_dll_overrides_from_env() noexcept {
    std::lock_guard<std::mutex> lock(g_overrides_mutex);
    if (g_env_loaded) {
        return;
    }
    g_env_loaded = true;

    const char* env = std::getenv("TL_DLL_OVERRIDES");
    if (env == nullptr || *env == '\0') {
        return;
    }

    std::istringstream stream{env};
    std::string token;
    while (std::getline(stream, token, ',')) {
        const auto eq = token.find('=');
        if (eq == std::string::npos || eq == 0) {
            continue;
        }
        const std::string name = normalize_dll_name(token.substr(0, eq));
        const std::string mode_str = token.substr(eq + 1);

        DllOverrideMode mode = DllOverrideMode::BuiltinFirst;
        if (mode_str == "n" || mode_str == "native" || mode_str == "native_first") {
            mode = DllOverrideMode::NativeFirst;
        } else if (mode_str == "b" || mode_str == "builtin" || mode_str == "builtin_first") {
            mode = DllOverrideMode::BuiltinFirst;
        } else if (mode_str == "no" || mode_str == "native_only") {
            mode = DllOverrideMode::NativeOnly;
        } else if (mode_str == "bo" || mode_str == "builtin_only") {
            mode = DllOverrideMode::BuiltinOnly;
        }

        g_overrides[name] = mode;
    }
}

}  // namespace tradutorlinux::loader
