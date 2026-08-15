#include "tradutorlinux/runtime/winapi.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/gui/x11.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <thread>
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

extern "C" void tl_call_guest_on_stack(std::uintptr_t entry,
                                         std::uintptr_t stack_top) noexcept;

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

[[nodiscard]] bool mapped_guest_range(const void* address, const std::size_t size,
                                      const bool writable) noexcept {
    if (address == nullptr) {
        return false;
    }
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(address);
    if (size > std::numeric_limits<std::uintptr_t>::max() - start) {
        return false;
    }
    const std::uintptr_t end = start + size;
    std::ifstream maps{ "/proc/self/maps" };
    std::string line;
    while (std::getline(maps, line)) {
        const std::size_t dash = line.find('-');
        const std::size_t space = line.find(' ', dash == std::string::npos ? 0 : dash);
        if (dash == std::string::npos || space == std::string::npos || dash == 0) {
            continue;
        }
        std::uintptr_t region_start{};
        std::uintptr_t region_end{};
        const auto start_result = std::from_chars(line.data(), line.data() + dash,
                                                  region_start, 16);
        const auto end_result = std::from_chars(line.data() + dash + 1, line.data() + space,
                                                region_end, 16);
        if (start_result.ec != std::errc{} || end_result.ec != std::errc{} ||
            region_start > region_end || start < region_start || end > region_end) {
            continue;
        }
        const std::string::size_type permissions_offset = space + 1;
        if (line.size() < permissions_offset + 4 || line[permissions_offset] != 'r' ||
            (writable && line[permissions_offset + 1] != 'w')) {
            return false;
        }
        return true;
    }
    return false;
}

[[nodiscard]] bool mapped_guest_cstring(const char* value) noexcept {
    if (value == nullptr) {
        return true;
    }
    constexpr std::size_t kMaxGuestString = 65535;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(value);
    for (std::size_t index = 0; index < kMaxGuestString; ++index) {
        if (index > std::numeric_limits<std::uintptr_t>::max() - address ||
            !mapped_guest_range(reinterpret_cast<const void*>(address + index), 1, false)) { // NOLINT(performance-no-int-to-ptr)
            return false;
        }
        if (value[index] == '\0') {
            return true;
        }
    }
    return false;
}

void runtime_trace(const char* event, const std::array<diagnostics::TraceField, 4>& fields,
                   const std::size_t field_count) noexcept {
    diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime,
                             diagnostics::TraceLevel::Info, event,
                             std::span<const diagnostics::TraceField>{fields.data(), field_count});
}

void trace_guest_failure(const char* symbol, const char* operation,
                         const char* detail) noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"category", std::string{
                                     diagnostics::failure_category_name(
                                         diagnostics::FailureCategory::GuestMemory)}},
        diagnostics::TraceField{"symbol", symbol},
        diagnostics::TraceField{"operation", operation},
        diagnostics::TraceField{"detail", detail},
    };
    diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime,
                             diagnostics::TraceLevel::Error, "api-failure", fields);
}

void trace_linux_failure(const char* symbol, const char* operation, const int error,
                         const std::uint32_t win32_error) noexcept {
    const std::array<diagnostics::TraceField, 5> fields{
        diagnostics::TraceField{"category", std::string{
                                     diagnostics::failure_category_name(
                                         diagnostics::FailureCategory::LinuxError)}},
        diagnostics::TraceField{"symbol", symbol},
        diagnostics::TraceField{"operation", operation},
        diagnostics::TraceField{"errno", std::to_string(error)},
        diagnostics::TraceField{"win32-error", std::to_string(win32_error)},
    };
    diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime,
                             diagnostics::TraceLevel::Error, "linux-failure", fields);
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
    trace_guest_failure(symbol, "argument-validation", "ponteiro ou parâmetro inválido");
}

struct ClassSlot {
    bool used{false};
    std::string name;
    std::uintptr_t wndproc{0};
};

struct WindowSlot {
    bool used{false};
    std::uintptr_t wndproc{0};
    std::string class_name;
    gui::NativeWindow native{nullptr};
    bool mapped{false};
    abi::GuestMsg pending{};  // mensagem traduzida (ex.: WM_CHAR) aguardando GetMessageA
    bool has_pending{false};
    char last_key{'\0'};  // caractere do WM_KEYDOWN mais recente, para TranslateMessage
};

std::array<ClassSlot, 32> g_classes{};
std::array<WindowSlot, 16> g_windows{};
bool g_quit_requested = false;
std::uint32_t g_quit_code = 0;

using WndProc = TL_MSABI abi::Lresult (*)(abi::HWnd, std::uint32_t, abi::Wparam, abi::Lparam);

// Fronteira host -> convidado: invoca o WNDPROC do convidado pela convenção
// Microsoft x64. A chamada acontece enquanto o hospedeiro já executa sobre a
// pilha convidada (o convidado chamou a API hospedeira), então nenhum
// trampolim de pilha é necessário.
abi::Lresult call_wndproc(const std::uintptr_t wndproc, const abi::HWnd hwnd,
                          const std::uint32_t message, const abi::Wparam wparam,
                          const abi::Lparam lparam) noexcept {
    return std::bit_cast<WndProc>(wndproc)(hwnd, message, wparam, lparam);
}

[[nodiscard]] bool ascii_iequals(const std::string_view a, const std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t index = 0; index < a.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(a[index])) !=
            std::tolower(static_cast<unsigned char>(b[index]))) {
            return false;
        }
    }
    return true;
}

ClassSlot* find_class_slot(const char* const name) noexcept {
    if (name == nullptr) {
        return nullptr;
    }
    const auto found = std::find_if(g_classes.begin(), g_classes.end(),
                                    [name](const ClassSlot& slot) {
                                        return slot.used && ascii_iequals(slot.name, name);
                                    });
    if (found != g_classes.end()) {
        return &*found;
    }
    return nullptr;
}

WindowSlot* find_window_slot(const void* const handle) noexcept {
    const auto found = std::find_if(g_windows.begin(), g_windows.end(),
                                    [handle](const WindowSlot& slot) {
                                        return slot.used && handle == &slot;
                                    });
    if (found != g_windows.end()) {
        return &*found;
    }
    return nullptr;
}

void write_guest_msg(void* const msg, const abi::HWnd hwnd, const std::uint32_t message,  // NOLINT(bugprone-easily-swappable-parameters)
                     const abi::Wparam wparam, const abi::Lparam lparam) noexcept {
    abi::GuestMsg* const out = static_cast<abi::GuestMsg*>(msg);
    out->hwnd = hwnd;
    out->message = message;
    out->padding = 0;
    out->wparam = wparam;
    out->lparam = lparam;
    out->time = 0;
    out->pt_x = 0;
    out->pt_y = 0;
}

// Virtual key do subconjunto suportado: para letras usa a maiúscula (como
// VK_A), demais caracteres ASCII imprimíveis usam o próprio valor.
[[nodiscard]] abi::Wparam keydown_vkey(const char character) noexcept {
    const auto value = static_cast<std::uint32_t>(static_cast<unsigned char>(character));
    if (value >= 'a' && value <= 'z') {
        return static_cast<abi::Wparam>(value - 0x20U);
    }
    return static_cast<abi::Wparam>(value);
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
    if (bytes_written != nullptr && !mapped_guest_range(bytes_written, sizeof(*bytes_written), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("WriteFile", "output-count", "ponteiro sem permissão de escrita");
        return 0;
    }
    if (bytes_written != nullptr) {
        *bytes_written = 0;
    }
    const int fd = handle_fd(handle);
    if (fd < 0 || (bytes_to_write != 0 && !mapped_guest_range(buffer, bytes_to_write, false)) ||
        overlapped != nullptr) {
        set_last_error(fd < 0 ? abi::kErrorInvalidHandle : abi::kErrorInvalidParameter);
        trace_stub("WriteFile");
        return 0;
    }
    std::uint32_t total = 0;
    int failure_errno = EIO;
    while (total < bytes_to_write) {
        const ssize_t result = write(fd, static_cast<const std::byte*>(buffer) + total,
                                     bytes_to_write - total);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0) {
            failure_errno = errno;
        }
        if (result <= 0) {
            break;
        }
        total += static_cast<std::uint32_t>(result);
    }
    if (bytes_written != nullptr) {
        *bytes_written = total;
    }
    const std::uint32_t failure_error = errno_to_win32(failure_errno);
    set_last_error(total == bytes_to_write ? abi::kErrorSuccess : failure_error);
    if (total != bytes_to_write) {
        trace_linux_failure("WriteFile", "write", failure_errno, failure_error);
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

TL_MSABI int tl_ReadFile(const void* const handle, void* const buffer, // NOLINT(bugprone-easily-swappable-parameters)
                         const std::uint32_t bytes_to_read,
                         std::uint32_t* const bytes_read,
                         const void* const overlapped) noexcept {
    if (bytes_read != nullptr && !mapped_guest_range(bytes_read, sizeof(*bytes_read), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("ReadFile", "output-count", "ponteiro sem permissão de escrita");
        return 0;
    }
    if (bytes_read != nullptr) {
        *bytes_read = 0;
    }
    const int fd = handle_fd(handle);
    if (fd < 0 || (bytes_to_read != 0 && !mapped_guest_range(buffer, bytes_to_read, true)) ||
        overlapped != nullptr) {
        set_last_error(fd < 0 ? abi::kErrorInvalidHandle : abi::kErrorInvalidParameter);
        trace_stub("ReadFile");
        return 0;
    }
    ssize_t result = 0;
    do {
        result = read(fd, buffer, bytes_to_read);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        trace_linux_failure("ReadFile", "read", errno, failure_error);
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
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        trace_linux_failure("VirtualAlloc", "mmap", errno, failure_error);
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
                const std::uint32_t failure_error = errno_to_win32(errno);
                set_last_error(failure_error);
                trace_linux_failure("VirtualFree", "munmap", errno, failure_error);
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
    if (!mapped_guest_cstring(path) || path == nullptr || path[0] == '\0' || path[0] == '/' || path[0] == '\\' ||
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
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        trace_linux_failure("CreateFileA", "open", errno, failure_error);
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
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("CloseHandle", "handle-validation", "handle inválido");
        return 0;
    }
    if (close(slot->fd) != 0) {
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        trace_linux_failure("CloseHandle", "close", errno, failure_error);
        return 0;
    }
    *slot = {};
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_MessageBoxA(const void* const, const char* const text,
                                      const char* const caption,
                                      const std::uint32_t type) noexcept {
    if (type != 0 || !mapped_guest_cstring(text) || !mapped_guest_cstring(caption)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uint32_t result = gui::message_box(text, caption);
    set_last_error(result == 0 ? abi::kErrorAccessDenied : abi::kErrorSuccess);
    return result;
}

TL_MSABI abi::Atom tl_RegisterClassExA(const void* const wnd_class) noexcept {
    if (wnd_class == nullptr ||
        !mapped_guest_range(wnd_class, sizeof(abi::GuestWndClassExA), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("RegisterClassExA", "wnd-class", "estrutura WNDCLASSEXA inválida");
        return 0;
    }
    const auto* const wc = static_cast<const abi::GuestWndClassExA*>(wnd_class);
    if (wc->cb_size < sizeof(abi::GuestWndClassExA) || wc->window_proc == 0 ||
        wc->class_name == nullptr || !mapped_guest_cstring(wc->class_name) ||
        !mapped_guest_range(std::bit_cast<const void*>(wc->window_proc), 1, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("RegisterClassExA", "wnd-class", "cbSize, window_proc ou class_name inválido");
        return 0;
    }
    if (find_class_slot(wc->class_name) != nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto free_it = std::find_if(g_classes.begin(), g_classes.end(),
                                      [](const ClassSlot& slot) { return !slot.used; });
    if (free_it == g_classes.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    ClassSlot& slot = *free_it;
    slot.used = true;
    slot.name = wc->class_name;
    slot.wndproc = wc->window_proc;
    const abi::Atom atom =
        static_cast<abi::Atom>(static_cast<std::size_t>(free_it - g_classes.begin()) + 1U);
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "RegisterClassExA"},
        diagnostics::TraceField{"class", slot.name},
        diagnostics::TraceField{"atom", std::to_string(atom)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("RegisterClassExA", fields, 4);
    return atom;
}

TL_MSABI abi::HWnd tl_CreateWindowExA(const std::uint32_t,  // NOLINT(bugprone-easily-swappable-parameters)
                                      const char* const class_name, const char* const window_name,
                                      const std::uint32_t, const int, const int, const int width,
                                      const int height, const void* const, const void* const,
                                      const void* const, const void* const) noexcept {
    if (!mapped_guest_cstring(class_name) || class_name == nullptr ||
        (window_name != nullptr && !mapped_guest_cstring(window_name))) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateWindowExA", "strings", "nome de classe ou janela inválido");
        return nullptr;
    }
    ClassSlot* const cls = find_class_slot(class_name);
    if (cls == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateWindowExA", "class-lookup", "classe não registrada");
        return nullptr;
    }
    const auto free_it = std::find_if(g_windows.begin(), g_windows.end(),
                                      [](const WindowSlot& slot) { return !slot.used; });
    if (free_it == g_windows.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    const char* const caption = window_name != nullptr ? window_name : cls->name.c_str();
    gui::NativeWindow native = gui::create_window(caption, width, height);
    if (native == nullptr) {
        set_last_error(abi::kErrorAccessDenied);
        trace_guest_failure("CreateWindowExA", "x11", "falha ao criar janela X11");
        return nullptr;
    }
    WindowSlot& slot = *free_it;
    slot.used = true;
    slot.wndproc = cls->wndproc;
    slot.class_name = cls->name;
    slot.native = native;
    const abi::Lresult create_result = call_wndproc(slot.wndproc, &slot, abi::kWmCreate, 0, 0);
    if (create_result == -1) {
        gui::destroy_window(slot.native);
        slot = {};
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateWindowExA", "wm-create", "WM_CREATE rejeitou a criação");
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "CreateWindowExA"},
        diagnostics::TraceField{"class", slot.class_name},
        diagnostics::TraceField{"window", caption},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("CreateWindowExA", fields, 4);
    return &slot;
}

TL_MSABI int tl_ShowWindow(const void* const window, const int cmd_show) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("ShowWindow", "handle-validation", "handle inválido");
        return 0;
    }
    const bool was_mapped = slot->mapped;
    if (cmd_show == 0) {
        if (slot->mapped && slot->native != nullptr) {
            gui::unmap_window(slot->native);
        }
        slot->mapped = false;
    } else if (slot->native != nullptr) {
        slot->mapped = gui::map_window(slot->native);
    }
    set_last_error(abi::kErrorSuccess);
    return was_mapped ? 1 : 0;
}

TL_MSABI int tl_UpdateWindow(const void* const window) noexcept {
    const WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("UpdateWindow", "handle-validation", "handle inválido");
        return 0;
    }
    if (slot->wndproc != 0) {
        call_wndproc(slot->wndproc, const_cast<abi::HWnd>(window), abi::kWmPaint, 0, 0);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetMessageA(void* const msg, const void* const window,  // NOLINT(bugprone-easily-swappable-parameters)
                            const std::uint32_t filter_min,  // NOLINT(bugprone-easily-swappable-parameters)
                            const std::uint32_t filter_max) noexcept {
    if (msg == nullptr || !mapped_guest_range(msg, sizeof(abi::GuestMsg), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("GetMessageA", "output-message", "ponteiro sem permissão de escrita");
        return -1;
    }
    (void)filter_min;
    (void)filter_max;
    if (g_quit_requested) {
        g_quit_requested = false;
        write_guest_msg(msg, nullptr, abi::kWmQuit, g_quit_code, 0);
        set_last_error(abi::kErrorSuccess);
        const std::array<diagnostics::TraceField, 4> fields{
            diagnostics::TraceField{"symbol", "GetMessageA"},
            diagnostics::TraceField{"message", "WM_QUIT"},
            diagnostics::TraceField{"exit-code", std::to_string(g_quit_code)},
            diagnostics::TraceField{"result", "quit"},
        };
        runtime_trace("GetMessageA", fields, 4);
        return 0;
    }
    for (WindowSlot& slot : g_windows) {
        if (!slot.used || (window != nullptr && window != &slot)) {
            continue;
        }
        if (slot.has_pending) {
            write_guest_msg(msg, &slot, slot.pending.message, slot.pending.wparam,
                            slot.pending.lparam);
            slot.has_pending = false;
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
    }
    for (;;) {
        for (WindowSlot& slot : g_windows) {
            if (!slot.used || slot.native == nullptr || (window != nullptr && window != &slot)) {
                continue;
            }
            const gui::WindowEvent event = gui::next_window_event(slot.native);
            if (event.type == gui::WindowEventType::Redraw) {
                write_guest_msg(msg, &slot, abi::kWmPaint, 0, 0);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::Press) {
                const abi::Lparam lparam =
                    (static_cast<std::intptr_t>(event.y & 0xFFFF) << 16) |
                    static_cast<std::intptr_t>(event.x & 0xFFFF);
                write_guest_msg(msg, &slot, abi::kWmLButtonDown, 0, lparam);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::KeyDown) {
                slot.last_key = event.character;
                write_guest_msg(msg, &slot, abi::kWmKeyDown, keydown_vkey(event.character), 0);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::CloseRequested) {
                write_guest_msg(msg, &slot, abi::kWmClose, 0, 0);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

TL_MSABI int tl_TranslateMessage(const void* const msg) noexcept {
    if (msg == nullptr || !mapped_guest_range(msg, sizeof(abi::GuestMsg), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("TranslateMessage", "message", "ponteiro de mensagem inválido");
        return 0;
    }
    const auto* const message = static_cast<const abi::GuestMsg*>(msg);
    if (message->message == abi::kWmKeyDown) {
        WindowSlot* const slot = find_window_slot(message->hwnd);
        if (slot != nullptr && slot->last_key != '\0' && !slot->has_pending) {
            slot->pending = {};
            slot->pending.message = abi::kWmChar;
            slot->pending.wparam =
                static_cast<abi::Wparam>(static_cast<unsigned char>(slot->last_key));
            slot->pending.lparam = 0;
            const abi::Wparam char_code = slot->pending.wparam;
            slot->last_key = '\0';
            slot->has_pending = true;
            set_last_error(abi::kErrorSuccess);
            const std::array<diagnostics::TraceField, 4> fields{
                diagnostics::TraceField{"symbol", "TranslateMessage"},
                diagnostics::TraceField{"message", "WM_CHAR"},
                diagnostics::TraceField{"wparam", std::to_string(char_code)},
                diagnostics::TraceField{"status", "translated"},
            };
            runtime_trace("TranslateMessage", fields, 4);
            return 1;
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI abi::Lresult tl_DispatchMessageA(const void* const msg) noexcept {
    if (msg == nullptr || !mapped_guest_range(msg, sizeof(abi::GuestMsg), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("DispatchMessageA", "message", "ponteiro de mensagem inválido");
        return 0;
    }
    const auto* const message = static_cast<const abi::GuestMsg*>(msg);
    WindowSlot* const slot = find_window_slot(message->hwnd);
    if (slot == nullptr || slot->wndproc == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("DispatchMessageA", "window-lookup", "hwnd ou wndproc inválido");
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return call_wndproc(slot->wndproc, message->hwnd, message->message, message->wparam,
                        message->lparam);
}

TL_MSABI abi::Lresult tl_DefWindowProcA(const void* const window,
                                        const std::uint32_t message,  // NOLINT(bugprone-easily-swappable-parameters)
                                        const abi::Wparam wparam, const abi::Lparam lparam) noexcept {
    (void)wparam;
    (void)lparam;
    if (message == abi::kWmClose) {
        tl_DestroyWindow(window);
        return 0;
    }
    return 0;
}

TL_MSABI int tl_DestroyWindow(const void* const window) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("DestroyWindow", "handle-validation", "handle inválido");
        return 0;
    }
    if (slot->native != nullptr) {
        gui::destroy_window(slot->native);
    }
    slot->native = nullptr;
    slot->mapped = false;
    const abi::HWnd hwnd = const_cast<abi::HWnd>(window);
    const std::uintptr_t wndproc = slot->wndproc;
    *slot = {};
    if (wndproc != 0) {
        call_wndproc(wndproc, hwnd, abi::kWmDestroy, 0, 0);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void tl_PostQuitMessage(const int exit_code) noexcept {
    g_quit_code = static_cast<std::uint32_t>(exit_code);
    g_quit_requested = true;
}

}  // extern "C"

GuestExecutionResult execute_guest_entry(const std::uintptr_t entry_point, // NOLINT(bugprone-easily-swappable-parameters)
                                         const std::uintptr_t stack_top) noexcept {
    using EntryPoint = TL_MSABI void (*)();
    const auto entry = std::bit_cast<EntryPoint>(entry_point);
    if (entry == nullptr || stack_top == 0) {
        return {};
    }
    g_quit_requested = false;
    g_quit_code = 0;
    g_guest_execution_active = true;
    if (setjmp(g_guest_exit_context) == 0) {
        tl_call_guest_on_stack(std::bit_cast<std::uintptr_t>(entry), stack_top);
        g_guest_execution_active = false;
        return {};
    }
    g_guest_execution_active = false;
    return {.exited_explicitly = true, .exit_code = g_guest_exit_code};
}

}  // namespace tradutorlinux
