#include "tradutorlinux/runtime/winapi.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"

#include <array>
#include <bit>
#include <csetjmp>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <unistd.h>

namespace tradutorlinux {
namespace {

thread_local std::jmp_buf g_guest_exit_context;
thread_local bool g_guest_execution_active = false;
thread_local std::uint32_t g_guest_exit_code = 0;

char kStdInputToken = 0;
char kStdOutputToken = 0;
char kStdErrorToken = 0;

void runtime_trace(const char* event, const std::array<diagnostics::TraceField, 4>& fields,
                   const std::size_t field_count) noexcept {
    diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime,
                             diagnostics::TraceLevel::Info, event,
                             std::span<const diagnostics::TraceField>{fields.data(), field_count});
}

int handle_fd(const void* handle) noexcept {
    if (handle == &kStdInputToken) {
            return STDIN_FILENO;
    }
    if (handle == &kStdOutputToken) {
            return STDOUT_FILENO;
    }
    if (handle == &kStdErrorToken) {
            return STDERR_FILENO;
    }
    return -1;
}

void trace_stub(const char* symbol) noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"dll", "KERNEL32.dll"},
        diagnostics::TraceField{"symbol", symbol},
        diagnostics::TraceField{"detail", "símbolo não implementado"},
        diagnostics::TraceField{"status", "not-implemented"},
    };
    diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime,
                             diagnostics::TraceLevel::Warning, "stub",
                             std::span<const diagnostics::TraceField>{fields.data(), 4});
}

}  // namespace

extern "C" {

TL_MSABI void* tl_GetStdHandle(const std::uint32_t standard_handle) noexcept {
    void* result = nullptr;
    if (standard_handle == abi::kStdInputHandle) {
        result = &kStdInputToken;
    } else if (standard_handle == abi::kStdOutputHandle) {
        result = &kStdOutputToken;
    } else if (standard_handle == abi::kStdErrorHandle) {
        result = &kStdErrorToken;
    }
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "GetStdHandle"},
        diagnostics::TraceField{"standard-handle", std::to_string(standard_handle)},
        diagnostics::TraceField{"result", result != nullptr ? "valid" : "invalid"},
        diagnostics::TraceField{"status", result != nullptr ? "success" : "failure"},
    };
    runtime_trace("GetStdHandle", fields, 4);
    return result;
}

TL_MSABI int tl_WriteFile(const void* handle, const void* const buffer,
                          const std::uint32_t bytes_to_write,
                          std::uint32_t* const bytes_written,
                          const void* overlapped) noexcept {
    if (bytes_written != nullptr) {
        *bytes_written = 0;
    }
    const int fd = handle_fd(handle);
    if (fd < 0 || (buffer == nullptr && bytes_to_write != 0) || overlapped != nullptr) {
        trace_stub("WriteFile");
        return 0;
    }
    std::uint32_t total = 0;
    while (total < bytes_to_write) {
        const ssize_t result = write(fd, static_cast<const std::byte*>(buffer) + total,
                                     bytes_to_write - total);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            break;
        }
        total += static_cast<std::uint32_t>(result);
    }
    if (bytes_written != nullptr) {
        *bytes_written = total;
    }
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "WriteFile"},
        diagnostics::TraceField{"bytes-requested", std::to_string(bytes_to_write)},
        diagnostics::TraceField{"bytes-written", std::to_string(total)},
        diagnostics::TraceField{"status", total == bytes_to_write ? "success" : "failure"},
    };
    runtime_trace("WriteFile", fields, 4);
    return total == bytes_to_write ? 1 : 0;
}

TL_MSABI int tl_ReadFile(const void* const handle, void* const buffer,
                         const std::uint32_t bytes_to_read,
                         std::uint32_t* const bytes_read,
                         const void* const overlapped) noexcept {
    if (bytes_read != nullptr) {
        *bytes_read = 0;
    }
    const int fd = handle_fd(handle);
    if (fd < 0 || (buffer == nullptr && bytes_to_read != 0) || overlapped != nullptr) {
        trace_stub("ReadFile");
        return 0;
    }
    const ssize_t result = read(fd, buffer, bytes_to_read);
    if (result < 0) {
        if (errno == EINTR) {
            return tl_ReadFile(handle, buffer, bytes_to_read, bytes_read, overlapped);
        }
        trace_stub("ReadFile");
        return 0;
    }
    if (bytes_read != nullptr) {
        *bytes_read = static_cast<std::uint32_t>(result);
    }
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "ReadFile"},
        diagnostics::TraceField{"bytes-requested", std::to_string(bytes_to_read)},
        diagnostics::TraceField{"bytes-read", std::to_string(result)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("ReadFile", fields, 4);
    return 1;
}

TL_MSABI void tl_ExitProcess(const std::uint32_t exit_code) noexcept {
    g_guest_exit_code = exit_code;
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "ExitProcess"},
        diagnostics::TraceField{"exit-code", std::to_string(exit_code)},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"mechanism", "guest-transfer"},
    };
    runtime_trace("ExitProcess", fields, 4);
    if (g_guest_execution_active) {
        std::longjmp(g_guest_exit_context, 1);
    }
}

}  // extern "C"

GuestExecutionResult execute_guest_entry(const std::uintptr_t entry_point) noexcept {
    using EntryPoint = TL_MSABI void (*)();
    const auto entry = std::bit_cast<EntryPoint>(entry_point);
    if (entry == nullptr) {
        return {};
    }
    g_guest_execution_active = true;
    if (setjmp(g_guest_exit_context) == 0) {
        entry();
        g_guest_execution_active = false;
        return {};
    }
    g_guest_execution_active = false;
    return {.exited_explicitly = true, .exit_code = g_guest_exit_code};
}

}  // namespace tradutorlinux
