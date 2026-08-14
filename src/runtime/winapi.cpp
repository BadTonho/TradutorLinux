#include "tradutorlinux/runtime/winapi.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"

#include <array>
#include <algorithm>
#include <bit>
#include <csetjmp>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <array>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

namespace tradutorlinux {
namespace {

thread_local std::jmp_buf g_guest_exit_context;
thread_local bool g_guest_execution_active = false;
thread_local std::uint32_t g_guest_exit_code = 0;
thread_local std::uint32_t g_last_error = abi::kErrorSuccess;

char kStdInputToken = 0;
char kStdOutputToken = 0;
char kStdErrorToken = 0;

struct FileSlot {
    int fd{-1};
    bool used{false};
};

struct AllocationSlot {
    void* address{nullptr};
    std::size_t size{0};
};

std::array<FileSlot, 64> g_files{};
std::array<AllocationSlot, 64> g_allocations{};

void set_last_error(const std::uint32_t error) noexcept {
    g_last_error = error;
}

[[nodiscard]] std::uint32_t errno_to_win32(const int error) noexcept {
    switch (error) {
        case ENOENT:
            return abi::kErrorFileNotFound;
        case EACCES:
        case EPERM:
            return abi::kErrorAccessDenied;
        case ENOMEM:
            return abi::kErrorNotEnoughMemory;
        default:
            return abi::kErrorInvalidParameter;
    }
}

FileSlot* find_file_slot(const void* handle) noexcept {
    const auto found = std::find_if(g_files.begin(), g_files.end(), [handle](const FileSlot& slot) {
        return slot.used && handle == &slot;
    });
    if (found != g_files.end()) {
        return &*found;
    }
    return nullptr;
}

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
    if (const FileSlot* slot = find_file_slot(handle); slot != nullptr) {
        return slot->fd;
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
    set_last_error(result != nullptr ? abi::kErrorSuccess : abi::kErrorInvalidParameter);
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
        set_last_error(fd < 0 ? abi::kErrorInvalidHandle : abi::kErrorInvalidParameter);
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
    set_last_error(total == bytes_to_write ? abi::kErrorSuccess : errno_to_win32(errno));
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
        set_last_error(fd < 0 ? abi::kErrorInvalidHandle : abi::kErrorInvalidParameter);
        trace_stub("ReadFile");
        return 0;
    }
    const ssize_t result = read(fd, buffer, bytes_to_read);
    if (result < 0) {
        if (errno == EINTR) {
            return tl_ReadFile(handle, buffer, bytes_to_read, bytes_read, overlapped);
        }
        set_last_error(errno_to_win32(errno));
        trace_stub("ReadFile");
        return 0;
    }
    if (bytes_read != nullptr) {
        *bytes_read = static_cast<std::uint32_t>(result);
    }
    set_last_error(abi::kErrorSuccess);
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

TL_MSABI std::uint32_t tl_GetLastError() noexcept {
    return g_last_error;
}

TL_MSABI void tl_SetLastError(const std::uint32_t error) noexcept {
    set_last_error(error);
}

TL_MSABI void* tl_VirtualAlloc(const void* const address, const std::uintptr_t size,
                               const std::uint32_t allocation_type,
                               const std::uint32_t protection) noexcept {
    const long page_value = sysconf(_SC_PAGESIZE);
    const std::size_t page = page_value > 0 ? static_cast<std::size_t>(page_value) : 0x1000U;
    if (address != nullptr || size == 0 || allocation_type != (abi::kMemCommit | abi::kMemReserve) ||
        (protection != abi::kPageReadOnly && protection != abi::kPageReadWrite)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (size > static_cast<std::uintptr_t>(SIZE_MAX - (page - 1U))) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    const std::size_t mapped_size =
        (static_cast<std::size_t>(size) + page - 1U) / page * page;
    const auto free_it = std::find_if(g_allocations.begin(), g_allocations.end(),
                                      [](const AllocationSlot& slot) {
                                          return slot.address == nullptr;
                                      });
    AllocationSlot* free_slot = free_it != g_allocations.end() ? &*free_it : nullptr;
    if (free_slot == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    const int prot = protection == abi::kPageReadOnly ? PROT_READ : (PROT_READ | PROT_WRITE);
    void* mapped = mmap(nullptr, mapped_size, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped == MAP_FAILED) {
        set_last_error(errno_to_win32(errno));
        return nullptr;
    }
    free_slot->address = mapped;
    free_slot->size = mapped_size;
    set_last_error(abi::kErrorSuccess);
    return mapped;
}

TL_MSABI int tl_VirtualFree(const void* const address, const std::uintptr_t size,
                            const std::uint32_t free_type) noexcept {
    if (address == nullptr || size != 0 || free_type != abi::kMemRelease) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    for (AllocationSlot& slot : g_allocations) {
        if (slot.address == address) {
            const bool unmapped = munmap(slot.address, slot.size) == 0;
            if (!unmapped) {
                set_last_error(errno_to_win32(errno));
                return 0;
            }
            slot = {};
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
    }
    set_last_error(abi::kErrorInvalidParameter);
    return 0;
}

TL_MSABI void* tl_CreateFileA(const char* const path, const std::uint32_t desired_access,
                              const std::uint32_t share_mode,
                              const void* const security_attributes,
                              const std::uint32_t creation_disposition,
                              const std::uint32_t flags,
                              const void* const template_file) noexcept {
    if (path == nullptr || path[0] == '\0' || path[0] == '/' || path[0] == '\\' ||
        share_mode != 0 || security_attributes != nullptr || flags != 0 ||
        template_file != nullptr ||
        (desired_access & ~(abi::kGenericRead | abi::kGenericWrite)) != 0 ||
        (creation_disposition != abi::kCreateAlways &&
         creation_disposition != abi::kOpenExisting)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    char normalized[4096]{};
    std::size_t length = 0;
    for (; path[length] != '\0'; ++length) {
        if (length + 1U >= sizeof(normalized) || path[length] == ':') {
            set_last_error(abi::kErrorInvalidParameter);
            return nullptr;
        }
        normalized[length] = path[length] == '\\' ? '/' : path[length];
    }
    normalized[length] = '\0';
    int open_flags = 0;
    const bool can_read = (desired_access & abi::kGenericRead) != 0;
    const bool can_write = (desired_access & abi::kGenericWrite) != 0;
    if (can_read && can_write) {
        open_flags |= O_RDWR;
    } else if (can_write) {
        open_flags |= O_WRONLY;
    } else {
        open_flags |= O_RDONLY;
    }
    if (creation_disposition == abi::kCreateAlways) {
        open_flags |= O_CREAT | O_TRUNC;
    }
    const int fd = open(normalized, open_flags, 0666);
    if (fd < 0) {
        set_last_error(errno_to_win32(errno));
        return nullptr;
    }
    const auto free_it = std::find_if(g_files.begin(), g_files.end(), [](const FileSlot& slot) {
        return !slot.used;
    });
    if (free_it != g_files.end()) {
        FileSlot& slot = *free_it;
        slot.fd = fd;
        slot.used = true;
        set_last_error(abi::kErrorSuccess);
        return &slot;
    }
    close(fd);
    set_last_error(abi::kErrorNotEnoughMemory);
    return nullptr;
}

TL_MSABI int tl_CloseHandle(const void* const handle) noexcept {
    FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr || close(slot->fd) != 0) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    *slot = {};
    set_last_error(abi::kErrorSuccess);
    return 1;
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
