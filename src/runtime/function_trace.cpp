#include "tradutorlinux/diagnostics/trace.hpp"

#include <cstdint>
#include <dlfcn.h>
#include <string_view>

namespace {

[[nodiscard]] bool should_trace(const void* const address) noexcept {
    Dl_info info{};
    if (dladdr(address, &info) == 0 || info.dli_sname == nullptr) return false;
    const std::string_view name{info.dli_sname};
    if (name.starts_with("_ZNSt") || name.starts_with("_ZSt") ||
        name.starts_with("_ZNS") || name.starts_with("_ZNKSt") ||
        name.starts_with("_ZN9__gnu_cxx") || name.starts_with("__gnu_cxx")) return false;
    return name.find("tl_") != std::string_view::npos ||
           name.find("tradutorlinux") != std::string_view::npos;
}

}  // namespace

extern "C" __attribute__((no_instrument_function))
void __cyg_profile_func_enter(void* const function, void* const caller) noexcept {
    if (!should_trace(function)) return;
    tradutorlinux::diagnostics::enqueue_function_json_trace(
        true, reinterpret_cast<std::uintptr_t>(function), reinterpret_cast<std::uintptr_t>(caller));
}

extern "C" __attribute__((no_instrument_function))
void __cyg_profile_func_exit(void* const function, void* const caller) noexcept {
    if (!should_trace(function)) return;
    tradutorlinux::diagnostics::enqueue_function_json_trace(
        false, reinterpret_cast<std::uintptr_t>(function), reinterpret_cast<std::uintptr_t>(caller));
}
