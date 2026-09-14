#pragma once

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/runtime/error_map.hpp"
#include "tradutorlinux/runtime/guest_context.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/runtime/teb.hpp"
#include "tradutorlinux/runtime/winapi.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace tradutorlinux {

using runtime::current_guest_context;
using runtime::default_guest_context;
using runtime::guest_context;
using runtime::GuestContext;
using runtime::GuestContextScope;

extern thread_local runtime::GuestTeb* g_thread_teb;
extern thread_local std::uint32_t g_thread_last_error;

inline void set_last_error(const std::uint32_t error) noexcept {
    g_thread_last_error = error;
    if (g_thread_teb != nullptr) {
        g_thread_teb->last_error_value = error;
    }
}

#define g_last_error (::tradutorlinux::g_thread_last_error)

template <typename T>
inline bool read_guest_value(const void* const source, T& destination) noexcept {
    return runtime::read_guest_memory(source, &destination, sizeof(destination)).status ==
           runtime::GuestMemoryAccessStatus::Success;
}

template <typename T>
inline bool write_guest_value(void* const destination, const T& source) noexcept {
    return runtime::write_guest_memory(destination, &source, sizeof(source)).status ==
           runtime::GuestMemoryAccessStatus::Success;
}

void runtime_trace(const char* event, const std::array<diagnostics::TraceField, 4>& fields,
                   std::size_t field_count) noexcept;
void trace_guest_failure(const char* symbol, const char* operation, const char* detail) noexcept;
void trace_linux_failure(const char* symbol, const char* operation, int error,
                         std::uint32_t win32_error) noexcept;
void trace_stub(const char* symbol) noexcept;

bool translate_windows_path(const char* win_path, char* linux_out, std::size_t out_size) noexcept;
bool wide_path_to_string(const std::uint16_t* path, std::string& result) noexcept;
bool normalized_wide_path(const std::uint16_t* path, char (&buffer)[4096]) noexcept;

}  // namespace tradutorlinux
