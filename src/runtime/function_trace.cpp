#include "tradutorlinux/diagnostics/trace.hpp"

#include <array>
#include <cstdint>

extern "C" {
// Estado POD, zero-inicializado pelo loader antes de qualquer construtor C++.
// O hook de -finstrument-functions precisa poder consultar este byte sem
// chamar código C++ durante a inicialização do processo.
volatile unsigned char tl_function_trace_enabled = 0;
}

namespace {

thread_local unsigned char g_function_trace_in_hook = 0;

}  // namespace

extern "C" __attribute__((no_instrument_function))
void __cyg_profile_func_enter(void* const function, void* const caller) noexcept {
    if (__atomic_load_n(&tl_function_trace_enabled, __ATOMIC_ACQUIRE) == 0) return;
    if (g_function_trace_in_hook != 0) return;
    g_function_trace_in_hook = 1;
    tradutorlinux::diagnostics::enqueue_function_json_trace(
        true, reinterpret_cast<std::uintptr_t>(&__cyg_profile_func_enter),
        reinterpret_cast<std::uintptr_t>(caller));
    tradutorlinux::diagnostics::enqueue_function_json_trace(
        true, reinterpret_cast<std::uintptr_t>(function), reinterpret_cast<std::uintptr_t>(caller));
    g_function_trace_in_hook = 0;
}

extern "C" __attribute__((no_instrument_function))
void __cyg_profile_func_exit(void* const function, void* const caller) noexcept {
    if (__atomic_load_n(&tl_function_trace_enabled, __ATOMIC_ACQUIRE) == 0) return;
    if (g_function_trace_in_hook != 0) return;
    g_function_trace_in_hook = 1;
    tradutorlinux::diagnostics::enqueue_function_json_trace(
        true, reinterpret_cast<std::uintptr_t>(&__cyg_profile_func_exit),
        reinterpret_cast<std::uintptr_t>(caller));
    tradutorlinux::diagnostics::enqueue_function_json_trace(
        false, reinterpret_cast<std::uintptr_t>(function), reinterpret_cast<std::uintptr_t>(caller));
    g_function_trace_in_hook = 0;
}

extern "C" __attribute__((no_instrument_function))
void trace_assembly_function_entry(const char* const function) noexcept {
    if (__atomic_load_n(&tl_function_trace_enabled, __ATOMIC_ACQUIRE) == 0 || function == nullptr) {
        return;
    }
    tradutorlinux::diagnostics::enqueue_function_json_trace(
        true, reinterpret_cast<std::uintptr_t>(&trace_assembly_function_entry), 0);
    const std::array fields{tradutorlinux::diagnostics::TraceField{"function", function}};
    tradutorlinux::diagnostics::write_json_trace(
        tradutorlinux::diagnostics::TraceComponent::Runtime,
        tradutorlinux::diagnostics::TraceLevel::Debug, "function-enter", fields);
}
