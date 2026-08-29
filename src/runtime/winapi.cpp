#include "tradutorlinux/runtime/winapi.hpp"
#include "runtime_context.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/gui/x11.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/process.hpp"
#include "tradutorlinux/pe/pe_reader.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/runtime/error_map.hpp"
#include "tradutorlinux/runtime/environment.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/runtime/msvcrt.hpp"
#include "tradutorlinux/runtime/security.hpp"
#include "tradutorlinux/runtime/teb.hpp"
#include "tradutorlinux/util/basics.hpp"
#include "tradutorlinux/util/unicode.hpp"
#include "gui_controls.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csetjmp>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tradutorlinux {

// ---------------------------------------------------------------------------
// Definições de estado compartilhado (declarados extern em runtime_context.hpp)
// ---------------------------------------------------------------------------

thread_local std::jmp_buf g_guest_exit_context;
thread_local bool g_guest_execution_active = false;
thread_local std::uint32_t g_guest_exit_code = 0;
thread_local std::uint32_t g_last_error = abi::kErrorSuccess;
thread_local runtime::GuestTeb* g_current_teb = nullptr;
runtime::GuestPeb g_guest_peb{};
runtime::GuestProcessParameters g_guest_process_params{};

std::string g_module_file_name;
std::filesystem::path g_guest_prefix_path;

char kStdInputToken = 0;
char kStdOutputToken = 0;
char kStdErrorToken = 0;
char kStockObjectTokens[24]{};
std::mutex g_process_context_mutex;
std::array<void*, 3> g_standard_handles{
    &kStdInputToken,
    &kStdOutputToken,
    &kStdErrorToken,
};
std::atomic<std::uintptr_t> g_pointer_cookie{0};

std::mutex g_files_mutex;
std::array<FileSlot, 256> g_files{};

std::mutex g_mapping_mutex;
std::array<FileMappingSlot, 64> g_mappings{};

std::mutex g_allocations_mutex;
std::array<AllocationSlot, 256> g_allocations{};
std::mutex g_global_memory_mutex;
std::array<GlobalMemorySlot, 256> g_global_memory{};
std::mutex g_local_free_mutex;
std::array<void*, 512> g_local_free_blocks{};

const std::byte* g_guest_image_base{nullptr};
std::size_t g_guest_image_size{0};
std::uint32_t g_guest_resource_rva{0};
std::uint32_t g_guest_resource_size{0};

std::mutex g_resource_mutex;
std::array<ResourceSlot, 256> g_resources{};

std::mutex g_sync_mutex;
std::array<SyncSlot, 256> g_syncs{};

thread_local std::uint32_t g_current_thread_id = kMainThreadId;
std::atomic<std::uint32_t> g_next_thread_id{kMainThreadId + 1};

std::mutex g_threads_mutex;
std::array<ThreadSlot, 256> g_threads{};

std::array<bool, kMaxTlsSlots> g_tls_indices_used{};
std::mutex g_tls_mutex;
thread_local std::array<void*, 64> g_guest_tls_slots{};
std::atomic<std::uintptr_t> g_unhandled_exception_filter{0};

std::array<FlsSlot, kMaxFlsSlots> g_fls_slots{};
std::mutex g_fls_mutex;
std::vector<std::weak_ptr<FlsThreadValues>> g_fls_threads{};
thread_local std::shared_ptr<FlsThreadValues> g_current_fls_values{};

std::mutex g_cs_mutex;
std::array<CriticalSectionEntry, 256> g_critical_sections{};

std::array<ClassSlot, 32> g_classes{};
std::array<WindowSlot, 32> g_windows{};
WindowSlot* g_focused_control = nullptr;
WindowSlot* g_active_dialog = nullptr;
bool g_modal_done = false;
std::intptr_t g_modal_result = 0;
WindowSlot* g_modal_parent = nullptr;
bool g_modal_parent_was_enabled = true;
bool g_quit_requested = false;
std::uint32_t g_quit_code = 0;

std::array<MenuSlot, 16> g_menus{};
std::array<FindSlot, 16> g_find_slots{};
std::array<SnapshotSlot, 16> g_snapshots{};
std::mutex g_snapshot_mutex;

extern "C" void tl_call_guest_on_stack(std::uintptr_t entry,
                                       std::uintptr_t stack_top) noexcept;

// ---------------------------------------------------------------------------
// Helpers compartilhados do runtime
// ---------------------------------------------------------------------------

bool register_local_free_block(void* const address) noexcept {
    if (address == nullptr) {
        return false;
    }
    std::lock_guard lock(g_local_free_mutex);
    for (void*& slot : g_local_free_blocks) {
        if (slot == nullptr) {
            slot = address;
            return true;
        }
    }
    return false;
}

bool take_local_free_block(void* const address) noexcept {
    if (address == nullptr) {
        return false;
    }
    std::lock_guard lock(g_local_free_mutex);
    for (void*& slot : g_local_free_blocks) {
        if (slot == address) {
            slot = nullptr;
            return true;
        }
    }
    return false;
}

void bump_guest_allocation_generation() noexcept {
    runtime::invalidate_memory_map_cache();
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

void trace_stub(const char* symbol) noexcept {
    trace_guest_failure(symbol, "argument-validation", "ponteiro ou parâmetro inválido");
}

bool translate_windows_path(const char* win_path,
                            char* linux_out,
                            std::size_t out_size) noexcept {
    if (win_path == nullptr || win_path[0] == '\0') {
        return false;
    }

    std::string_view view{win_path};
    // Caminhos absolutos do hospedeiro ("/...") não são caminhos Windows válidos
    // para o convidado: rejeitar para o chamador reportar ERROR_INVALID_PARAMETER.
    if (view.starts_with('/')) {
        return false;
    }
    if ((view.size() >= 2 && std::isalpha(static_cast<unsigned char>(view[0])) && view[1] == ':') ||
        view.starts_with('\\')) {
        const std::filesystem::path resolved =
            prefix::resolve_windows_path(view, guest_prefix_root());
        const std::string s = resolved.string();
        if (s.size() + 1 > out_size) {
            return false;
        }
        std::memcpy(linux_out, s.c_str(), s.size() + 1);
        return true;
    }

    std::size_t length = 0;
    for (; win_path[length] != '\0'; ++length) {
        if (length + 1 >= out_size) {
            return false;
        }
        linux_out[length] = (win_path[length] == '\\') ? '/' : win_path[length];
    }
    linux_out[length] = '\0';
    return true;
}

bool wide_path_to_string(const std::uint16_t* path,
                         std::string& result) noexcept {
    if (!mapped_guest_wstring(path) || path == nullptr || path[0] == 0) {
        return false;
    }
    result = util::wide_to_utf8(path);
    return !result.empty();
}

bool normalized_wide_path(const std::uint16_t* path,
                          char (&buffer)[4096]) noexcept {
    std::string utf8;
    return wide_path_to_string(path, utf8) &&
           translate_windows_path(utf8.c_str(), buffer, sizeof(buffer));
}

FileSlot* find_file_slot(const void* handle) noexcept {
    std::lock_guard<std::mutex> lock(g_files_mutex);
    const auto found = std::find_if(g_files.begin(), g_files.end(), [handle](const FileSlot& slot) {
        return slot.used && handle == &slot;
    });
    if (found != g_files.end()) {
        return &*found;
    }
    return nullptr;
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

ThreadSlot* find_thread_slot(const void* handle) noexcept {
    if (handle == nullptr) {
        return nullptr;
    }
    const auto addr = std::bit_cast<std::uintptr_t>(handle);
    if (addr >= kThreadHandleBase && addr < kThreadHandleBase + g_threads.size()) {
        std::lock_guard<std::mutex> lock(g_threads_mutex);
        const auto index = static_cast<std::size_t>(addr - kThreadHandleBase);
        ThreadSlot& slot = g_threads[index];
        return slot.used ? &slot : nullptr;
    }
    return nullptr;
}

void* thread_slot_to_handle(ThreadSlot& slot) noexcept {
    const auto index = static_cast<std::size_t>(&slot - g_threads.data());
    return std::bit_cast<void*>(kThreadHandleBase + index);
}

SyncSlot* find_sync_slot(const void* handle) noexcept {
    if (handle == nullptr) {
        return nullptr;
    }
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(handle);
    if (value < kSyncHandleBase || value >= kSyncHandleBase + g_syncs.size()) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_sync_mutex);
    SyncSlot& slot = g_syncs[value - kSyncHandleBase];
    return slot.used ? &slot : nullptr;
}

void* sync_slot_handle(const SyncSlot& slot) noexcept {
    return reinterpret_cast<void*>(kSyncHandleBase +
                                   static_cast<std::uintptr_t>(&slot - g_syncs.data()));
}

void clear_sync_slot(SyncSlot& slot) noexcept {
    if (slot.child_result_fd >= 0) {
        ::close(slot.child_result_fd);
    }
    slot.used = false;
    slot.signaled = false;
    slot.manual_reset = false;
    slot.owner_valid = false;
    slot.owner = {};
    slot.recursion = 0;
    slot.count = 0;
    slot.maximum = 0;
    slot.child_pid = -1;
    slot.child_result_fd = -1;
    slot.process_running = false;
    slot.process_exit_code = 259U;
}

std::uint32_t wait_process_slot(SyncSlot& slot, const std::uint32_t milliseconds) noexcept {
    if (!slot.used || slot.kind != SyncKind::Process) {
        return abi::kWaitFailed;
    }
    if (!slot.process_running) {
        return abi::kWaitObject0;
    }
    const auto started = std::chrono::steady_clock::now();
    while (slot.process_running) {
        int status = 0;
        const pid_t result = ::waitpid(slot.child_pid, &status, WNOHANG);
        if (result == slot.child_pid) {
            slot.process_running = false;
            slot.process_exit_code = WIFEXITED(status) ? static_cast<std::uint32_t>(WEXITSTATUS(status)) : 1U;
            if (slot.child_result_fd >= 0) {
                // O filho reporta o resultado real do convidado pelo pipe:
                // [flag explícito][exit_code LE32]. Sem mensagem completa,
                // mantém o código derivado do waitpid.
                constexpr std::size_t kChildResultMessageSize = 5;
                std::array<std::byte, kChildResultMessageSize> message{};
                std::size_t received = 0;
                while (received < message.size()) {
                    const ssize_t count = ::read(slot.child_result_fd,
                                                 message.data() + received,
                                                 message.size() - received);
                    if (count < 0 && errno == EINTR) {
                        continue;
                    }
                    if (count <= 0) {
                        break;
                    }
                    received += static_cast<std::size_t>(count);
                }
                if (received == message.size()) {
                    slot.process_exit_code =
                        static_cast<std::uint32_t>(message[1]) |
                        (static_cast<std::uint32_t>(message[2]) << 8U) |
                        (static_cast<std::uint32_t>(message[3]) << 16U) |
                        (static_cast<std::uint32_t>(message[4]) << 24U);
                }
                ::close(slot.child_result_fd);
                slot.child_result_fd = -1;
            }
            return abi::kWaitObject0;
        }
        if (milliseconds == 0) {
            return abi::kWaitTimeout;
        }
        int remaining = -1;
        if (milliseconds != abi::kInfinite) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (elapsed >= milliseconds) {
                return abi::kWaitTimeout;
            }
            remaining = static_cast<int>(milliseconds - elapsed);
        }
        if (slot.child_result_fd >= 0) {
            // Aguarda de forma bloqueante o filho no pipe de resultado, em vez
            // de poll/sleep de 1ms que queimaria CPU num timeout longo. O write
            // do filho (POLLIN) e o fechamento do write-end (POLLHUP) despertam
            // o poll; o waitpid na próxima iteração coleta o status.
            struct ::pollfd descriptor {};
            descriptor.fd = slot.child_result_fd;
            descriptor.events = POLLIN | POLLHUP;
            const int poll_result = ::poll(&descriptor, 1, remaining);
            if (poll_result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                // Erro persistente no fd (ex.: EBADF): evitar busy-spin.
                std::this_thread::yield();
            }
        } else {
            // Sem pipe de resultado (ex.: terminado antes de associar o fd),
            // não há como bloquear; cede a execução em vez de busy-spin.
            std::this_thread::yield();
        }
    }
    return abi::kWaitObject0;
}

std::uint32_t wait_sync_slot(SyncSlot& slot, const std::uint32_t milliseconds) noexcept {
    if (slot.kind == SyncKind::Process) {
        return wait_process_slot(slot, milliseconds);
    }
    std::unique_lock<std::mutex> lock(slot.mutex);
    const auto predicate = [&]() {
        if (!slot.used) return true;
        if (slot.kind == SyncKind::Event) return slot.signaled;
        if (slot.kind == SyncKind::Semaphore) return slot.count > 0;
        return !slot.owner_valid || slot.owner == std::this_thread::get_id();
    };
    if (milliseconds == abi::kInfinite) {
        slot.condition.wait(lock, predicate);
    } else if (!slot.condition.wait_for(lock, std::chrono::milliseconds(milliseconds), predicate)) {
        return abi::kWaitTimeout;
    }
    if (!slot.used) {
        return abi::kWaitFailed;
    }
    if (slot.kind == SyncKind::Event) {
        if (!slot.manual_reset) {
            slot.signaled = false;
        }
    } else if (slot.kind == SyncKind::Semaphore) {
        --slot.count;
    } else if (slot.owner_valid && slot.owner == std::this_thread::get_id()) {
        ++slot.recursion;
    } else {
        slot.owner_valid = true;
        slot.owner = std::this_thread::get_id();
        slot.recursion = 1;
    }
    return abi::kWaitObject0;
}

CriticalSectionEntry* find_cs_entry(void* cs) noexcept {
    if (cs == nullptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_cs_mutex);
    auto it = std::find_if(g_critical_sections.begin(), g_critical_sections.end(),
                           [cs](const CriticalSectionEntry& e) {
                               return e.used && e.guest_address == cs;
                           });
    return it != g_critical_sections.end() ? &*it : nullptr;
}

CriticalSectionEntry* alloc_cs_entry(void* cs) noexcept {
    std::lock_guard<std::mutex> lock(g_cs_mutex);
    auto it = std::find_if(g_critical_sections.begin(), g_critical_sections.end(),
                           [](const CriticalSectionEntry& e) { return !e.used; });
    if (it == g_critical_sections.end()) {
        return nullptr;
    }
    it->guest_address = cs;
    it->used = true;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&it->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    return &*it;
}

ClassSlot* find_class_slot(const char* const name) noexcept {
    if (name == nullptr) {
        return nullptr;
    }
    const auto found = std::find_if(g_classes.begin(), g_classes.end(),
                                    [name](const ClassSlot& slot) {
                                        return slot.used && util::ascii_iequals(slot.name, name);
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

FindSlot* find_slot_for_handle(const void* handle) noexcept {
    const auto idx = reinterpret_cast<std::uintptr_t>(handle) - kFindHandleBase;
    if (idx >= g_find_slots.size()) {
        return nullptr;
    }
    return &g_find_slots[idx];
}

int stock_object_index(const void* const token) noexcept {
    if (token == nullptr) {
        return -1;
    }
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(kStockObjectTokens);
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(token);
    if (value < base) {
        return -1;
    }
    const std::size_t offset = static_cast<std::size_t>(value - base);
    if (offset >= sizeof(kStockObjectTokens)) {
        return -1;
    }
    return static_cast<int>(offset);
}

std::uint32_t stat_to_win32_attributes(const char* path, const struct stat& st) noexcept {
    if (S_ISDIR(st.st_mode)) {
        return abi::kFileAttributeDirectory | abi::kFileAttributeArchive;
    }
    (void)path;
    std::uint32_t attrs = abi::kFileAttributeArchive;
    if ((st.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) == 0) {
        attrs |= abi::kFileAttributeReadOnly;
    }
    return attrs;
}

std::uint32_t apply_win32_file_attributes(const char* const path,
                                          const std::uint32_t attributes) noexcept {
    constexpr std::uint32_t kSupported = abi::kFileAttributeReadOnly |
                                         abi::kFileAttributeDirectory |
                                         abi::kFileAttributeArchive |
                                         abi::kFileAttributeNormal;
    if (path == nullptr || attributes == 0 || (attributes & ~kSupported) != 0 ||
        ((attributes & abi::kFileAttributeNormal) != 0 &&
         attributes != abi::kFileAttributeNormal)) {
        return abi::kErrorInvalidParameter;
    }
    struct stat st{};
    if (::stat(path, &st) != 0) {
        return runtime::errno_to_win32(errno);
    }
    if ((attributes & abi::kFileAttributeDirectory) != 0 && !S_ISDIR(st.st_mode)) {
        return abi::kErrorInvalidParameter;
    }
    mode_t mode = st.st_mode;
    if ((attributes & abi::kFileAttributeReadOnly) != 0) {
        mode &= static_cast<mode_t>(~(S_IWUSR | S_IWGRP | S_IWOTH));
    } else {
        mode |= S_IWUSR;
    }
    if (::chmod(path, mode) != 0) {
        return runtime::errno_to_win32(errno);
    }
    return abi::kErrorSuccess;
}

std::uint32_t decode_multibyte(const std::uint32_t code_page,
                               const std::uint8_t* const bytes,
                               const std::size_t length,
                               std::size_t& pos) noexcept {
    const std::uint8_t first = bytes[pos];
    if (first < 0x80U) {
        pos += 1;
        return static_cast<std::uint32_t>(first);
    }
    if (code_page == abi::kCpAcp || code_page == abi::kCp1252) {
        pos += 1;
        return util::cp1252_to_unicode(first);
    }
    if (code_page == abi::kCpOem || code_page == abi::kCp437) {
        pos += 1;
        return util::cp437_to_unicode(first);
    }
    return util::decode_utf8(reinterpret_cast<const char*>(bytes), length, pos);
}

bool set_guest_gs_base(const void* const base) noexcept {
    constexpr long kArchSetGs = 0x1001;  // ARCH_SET_GS
    return ::syscall(SYS_arch_prctl, kArchSetGs,
                     static_cast<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(base))) == 0;
}

void* allocate_guest_teb(const std::uintptr_t stack_top,
                         const std::uintptr_t stack_size,
                         const std::uint32_t thread_id) noexcept {
    constexpr std::size_t kTebSize = sizeof(runtime::GuestTeb);
    void* const teb = mmap(nullptr, kTebSize, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (teb == MAP_FAILED) {
        return nullptr;
    }
    auto* const fields = static_cast<runtime::GuestTeb*>(teb);
    runtime::initialize_guest_teb(fields, &g_guest_peb, stack_top, stack_top - stack_size, thread_id);
    g_current_teb = fields;
    return teb;
}

void free_guest_teb(void* const teb) noexcept {
    if (teb != nullptr) {
        if (g_current_teb == teb) {
            g_current_teb = nullptr;
        }
        static_cast<void>(munmap(teb, sizeof(runtime::GuestTeb)));
    }
}

// ---------------------------------------------------------------------------
// Entry Point Orchestration
// ---------------------------------------------------------------------------

void set_guest_module_path(const char* path) noexcept {
    g_module_file_name = path != nullptr ? path : "";
}

void set_guest_prefix_path(const std::filesystem::path& path) {
    g_guest_prefix_path = path;
    runtime::security::reset_prefix_cache();
}

std::filesystem::path guest_prefix_root() {
    if (!g_guest_prefix_path.empty()) {
        return g_guest_prefix_path;
    }
    return prefix::default_prefix_root();
}

void set_guest_image_view(const void* image_base, const std::size_t image_size,
                          const std::uint32_t resource_rva,
                          const std::uint32_t resource_size) noexcept {
    g_guest_image_base = static_cast<const std::byte*>(image_base);
    g_guest_image_size = image_size;
    g_guest_resource_rva = resource_rva;
    g_guest_resource_size = resource_size;
    for (ResourceSlot& slot : g_resources) {
        slot = {};
    }
}

static std::uint64_t g_guest_tls_start_raw = 0;
static std::uint64_t g_guest_tls_end_raw = 0;
static std::uint64_t g_guest_tls_index_addr = 0;
static std::vector<std::uint64_t> g_guest_tls_callbacks;

void set_guest_tls_directory(std::uint64_t start_raw, std::uint64_t end_raw,
                             std::uint64_t index_addr, const std::vector<std::uint64_t>& callbacks) noexcept {
    g_guest_tls_start_raw = start_raw;
    g_guest_tls_end_raw = end_raw;
    g_guest_tls_index_addr = index_addr;
    g_guest_tls_callbacks = callbacks;
}

void initialize_thread_tls(void* teb_ptr) noexcept {
    auto* teb = static_cast<runtime::GuestTeb*>(teb_ptr);
    if (teb != nullptr && g_guest_tls_start_raw != 0 && g_guest_tls_end_raw > g_guest_tls_start_raw) {
        const std::size_t template_size = static_cast<std::size_t>(g_guest_tls_end_raw - g_guest_tls_start_raw);
        const auto* src = reinterpret_cast<const std::uint8_t*>(g_guest_tls_start_raw);
        const std::size_t copy_size = std::min(template_size, teb->tls_module0_data.size());
        std::memcpy(teb->tls_module0_data.data(), src, copy_size);
    }
}

void invoke_thread_tls_callbacks(const std::uint32_t reason) noexcept {
    using TlsCallbackFn = TL_MSABI void (*)(void* dll_handle, std::uint32_t reason, void* reserved);
    for (const std::uint64_t cb_addr : g_guest_tls_callbacks) {
        if (cb_addr != 0) {
            auto cb = reinterpret_cast<TlsCallbackFn>(cb_addr);
            cb(const_cast<std::byte*>(g_guest_image_base), reason, nullptr);
        }
    }
}

void reset_process_console_state() noexcept {
    std::lock_guard lock(g_process_context_mutex);
    g_standard_handles = {&kStdInputToken, &kStdOutputToken, &kStdErrorToken};
    g_pointer_cookie.store(0, std::memory_order_release);
}

GuestExecutionResult execute_guest_entry(const std::uintptr_t entry_point,
                                         const std::uintptr_t stack_top) noexcept {
    using EntryPoint = TL_MSABI void (*)();
    const auto entry = std::bit_cast<EntryPoint>(entry_point);
    if (entry == nullptr || stack_top == 0) {
        return {};
    }
    if (!runtime::guest_environment_is_initialized()) {
        runtime::initialize_guest_environment(guest_prefix_root());
    }
    reset_process_console_state();
    g_guest_peb.image_base_address = reinterpret_cast<std::uint64_t>(g_guest_image_base);
    g_guest_peb.process_heap = reinterpret_cast<std::uint64_t>(tl_GetProcessHeap());
    g_guest_peb.process_parameters = reinterpret_cast<std::uint64_t>(&g_guest_process_params);
    g_guest_peb.number_of_processors = 4;
    g_guest_peb.being_debugged = 0;
    constexpr std::uintptr_t kGuestStackSize = 0x2000000U;  // 32 MiB
    void* const teb = allocate_guest_teb(stack_top, kGuestStackSize, kMainThreadId);
    if (teb == nullptr) {
        runtime::clear_guest_environment();
        return {};
    }
    const bool gs_configured = set_guest_gs_base(teb);
    if (!gs_configured) {
        free_guest_teb(teb);
        runtime::clear_guest_environment();
        return {};
    }

    // Inicializa template TLS e índice
    initialize_thread_tls(g_current_teb);
    if (g_guest_tls_index_addr != 0) {
        *reinterpret_cast<std::uint32_t*>(g_guest_tls_index_addr) = 0;
    }

    // Executa TLS callbacks antes do entry point principal (PROCESS_ATTACH e THREAD_ATTACH para thread 1)
    invoke_thread_tls_callbacks(1U /* DLL_PROCESS_ATTACH */);
    invoke_thread_tls_callbacks(2U /* DLL_THREAD_ATTACH */);

    g_quit_requested = false;
    g_quit_code = 0;
    g_guest_execution_active = true;
    g_current_thread_id = kMainThreadId;
    static_cast<void>(ensure_fls_thread_values());
    if (setjmp(g_guest_exit_context) == 0) {
        tl_call_guest_on_stack(std::bit_cast<std::uintptr_t>(entry), stack_top);
        cleanup_current_fls_values();
        g_guest_execution_active = false;
        static_cast<void>(set_guest_gs_base(nullptr));
        free_guest_teb(teb);
        reset_fls_process_state();
        reset_process_console_state();
        runtime::clear_guest_environment();
        return {};
    }
    cleanup_current_fls_values();
    g_guest_execution_active = false;
    static_cast<void>(set_guest_gs_base(nullptr));
    free_guest_teb(teb);
    const GuestExecutionResult result{.exited_explicitly = true, .exit_code = g_guest_exit_code};
    reset_fls_process_state();
    reset_process_console_state();
    runtime::clear_guest_environment();
    return result;
}

}  // namespace tradutorlinux
