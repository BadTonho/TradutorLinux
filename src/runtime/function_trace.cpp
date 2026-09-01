#include "tradutorlinux/diagnostics/trace.hpp"

#include <array>
#include <cstdint>
#include <dlfcn.h>
#include <functional>
#include <string>
#include <thread>

namespace {

thread_local bool g_in_function_trace = false;

[[nodiscard]] bool is_runtime_api(const char* const symbol) noexcept {
    return symbol != nullptr && std::string_view{symbol}.find("tl_") != std::string_view::npos;
}

void write_function_event(const char* const event, const void* const address) noexcept {
    if (g_in_function_trace || !tradutorlinux::diagnostics::is_trace_json_enabled()) return;
    g_in_function_trace = true;
    try {
        Dl_info info{};
        if (dladdr(address, &info) == 0 || !is_runtime_api(info.dli_sname)) {
            g_in_function_trace = false;
            return;
        }
        const auto fields = std::array{
            tradutorlinux::diagnostics::TraceField{"address", std::to_string(reinterpret_cast<std::uintptr_t>(address))},
            tradutorlinux::diagnostics::TraceField{"symbol", info.dli_sname},
            tradutorlinux::diagnostics::TraceField{"thread", std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()))},
        };
        tradutorlinux::diagnostics::write_json_trace(
            tradutorlinux::diagnostics::TraceComponent::Gui,
            tradutorlinux::diagnostics::TraceLevel::Debug, event, fields);
    } catch (...) {
    }
    g_in_function_trace = false;
}

}  // namespace

extern "C" __attribute__((no_instrument_function))
void __cyg_profile_func_enter(void* const function, void* const caller) noexcept {
    (void)caller;
    write_function_event("function-enter", function);
}

extern "C" __attribute__((no_instrument_function))
void __cyg_profile_func_exit(void* const function, void* const caller) noexcept {
    (void)caller;
    write_function_event("function-exit", function);
}
