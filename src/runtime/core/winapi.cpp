#include "tradutorlinux/runtime/winapi.hpp"
#include "runtime_context.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/gui/platform.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
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
#include <cstdio>
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

#include <cstdlib>

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

char kStdInputToken = 0;
char kStdOutputToken = 0;
char kStdErrorToken = 0;
char kStockObjectTokens[24]{};
std::atomic<std::uintptr_t> g_pointer_cookie{0};

std::mutex g_threads_mutex;
std::array<ThreadSlot, 256> g_threads{};

thread_local std::array<void*, 64> g_guest_tls_slots{};
thread_local std::uint32_t g_current_thread_id = kMainThreadId;
thread_local runtime::GuestTeb* g_thread_teb = nullptr;
thread_local std::uint32_t g_thread_last_error = 0;

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
std::mutex g_modal_mutex;
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

extern "C" std::uint32_t tl_call_guest_on_stack(std::uintptr_t entry,
                                                std::uintptr_t stack_top) noexcept;
extern "C" TL_MSABI int dummy_worker_check() noexcept;

// ---------------------------------------------------------------------------
// Helpers compartilhados do runtime
// ---------------------------------------------------------------------------

[[nodiscard]] bool guest_executable_address(const std::uintptr_t address) noexcept {
    if (g_guest_image_base == nullptr || address == 0) {
        return false;
    }
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(g_guest_image_base);
    if (address < base || address - base >= g_guest_image_size) {
        return false;
    }
    std::ifstream maps{"/proc/self/maps"};
    std::string line;
    while (std::getline(maps, line)) {
        unsigned long long start = 0;
        unsigned long long end = 0;
        char permissions[5]{};
        if (std::sscanf(line.c_str(), "%llx-%llx %4s", &start, &end, permissions) == 3 &&
            address >= start && address < end) {
            return permissions[2] == 'x';
        }
    }
    return false;
}

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
    auto& ctx = runtime::guest_context();
    if (handle == &kStdInputToken || handle == &ctx.standard_handle_tokens[0] || handle == ctx.standard_handles[0]) {
        return STDIN_FILENO;
    }
    if (handle == &kStdOutputToken || handle == &ctx.standard_handle_tokens[1] || handle == ctx.standard_handles[1]) {
        return STDOUT_FILENO;
    }
    if (handle == &kStdErrorToken || handle == &ctx.standard_handle_tokens[2] || handle == ctx.standard_handles[2]) {
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
    g_thread_teb = const_cast<runtime::GuestTeb*>(static_cast<const runtime::GuestTeb*>(base));
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
    return teb;
}

void free_guest_teb(void* const teb) noexcept {
    if (teb != nullptr) {
        if (g_thread_teb == teb) {
            g_thread_teb = nullptr;
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
        if (copy_size > 0 && mapped_guest_range(src, copy_size, false)) {
            std::memcpy(teb->tls_module0_data.data(), src, copy_size);
        }
    }
}

void invoke_thread_tls_callbacks(const std::uint32_t reason) noexcept {
    using TlsCallbackFn = TL_MSABI void (*)(void* dll_handle, std::uint32_t reason, void* reserved);
    for (const std::uint64_t cb_addr : g_guest_tls_callbacks) {
        if (guest_executable_address(static_cast<std::uintptr_t>(cb_addr))) {
            auto cb = reinterpret_cast<TlsCallbackFn>(cb_addr);
            cb(const_cast<std::byte*>(g_guest_image_base), reason, nullptr);
        }
    }
}

void reset_process_console_state() noexcept {
    std::lock_guard lock(g_process_context_mutex);
    auto& ctx = runtime::guest_context();
    g_standard_handles = {&ctx.standard_handle_tokens[0], &ctx.standard_handle_tokens[1], &ctx.standard_handle_tokens[2]};
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
    if (g_guest_tls_index_addr != 0 &&
        mapped_guest_range(reinterpret_cast<const void*>(g_guest_tls_index_addr),
                           sizeof(std::uint32_t), true)) {
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
        diagnostics::FunctionTraceScope assembly_scope{"tl_call_guest_on_stack"};
        const std::uint32_t natural_code =
            tl_call_guest_on_stack(std::bit_cast<std::uintptr_t>(entry), stack_top);
        cleanup_current_fls_values();
        g_guest_execution_active = false;
        static_cast<void>(set_guest_gs_base(nullptr));
        free_guest_teb(teb);
        reset_fls_process_state();
        reset_process_console_state();
        runtime::clear_guest_environment();
        return GuestExecutionResult{.exited_explicitly = false, .exit_code = natural_code};
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

extern "C" {

TL_MSABI std::uint32_t tl_NetApiBufferFree(void* const buffer) noexcept {
    (void)buffer;
    if (buffer != nullptr) {
        std::free(buffer);
    }
    set_last_error(abi::kErrorSuccess);
    return 0; // NERR_Success
}

TL_MSABI std::intptr_t tl_LresultFromObject(const void* const riid, const std::uintptr_t w_param, void* const unk) noexcept {
    (void)riid;
    (void)w_param;
    (void)unk;
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI std::uint32_t tl_TdhGetPropertySize(void* const event_record, const std::uint32_t tdh_context_count, void* const tdh_context, const std::uint32_t property_data_count, void* const property_data, std::uint32_t* const property_size) noexcept {
    (void)event_record;
    (void)tdh_context_count;
    (void)tdh_context;
    (void)property_data_count;
    (void)property_data;
    if (property_size != nullptr && mapped_guest_range(property_size, sizeof(*property_size), true)) {
        *property_size = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 0; // ERROR_SUCCESS
}

TL_MSABI int tl_OpenPrinterW(const std::uint16_t* const printer_name, void** const printer_handle, void* const defaults) noexcept {
    (void)defaults;
    if (printer_handle == nullptr || !mapped_guest_range(printer_handle, sizeof(*printer_handle), true) ||
        (printer_name != nullptr && !mapped_guest_wstring(printer_name))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    *printer_handle = nullptr;
    set_last_error(abi::kErrorNotSupported);
    runtime_trace("OpenPrinterW", {
        diagnostics::TraceField{"symbol", "OpenPrinterW"},
        diagnostics::TraceField{"status", "unsupported"},
        diagnostics::TraceField{"mechanism", "stub"},
        diagnostics::TraceField{"detail", "impressão não implementada"}}, 4);
    return 0;
}

TL_MSABI void tl_WTSFreeMemory(void* const memory) noexcept {
    if (memory != nullptr) {
        std::free(memory);
    }
}

// --- RTSSHooks KERNEL32 ---
TL_MSABI void* tl_CreateRemoteThread(void* const process, void* const attr, const std::size_t stack, void* const start, void* const param, const std::uint32_t flags, std::uint32_t* const tid) noexcept {
    (void)process; (void)attr; (void)stack; (void)start; (void)param; (void)flags;
    if (tid != nullptr && !mapped_guest_range(tid, sizeof(*tid), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (tid != nullptr) *tid = 0;
    set_last_error(abi::kErrorNotSupported);
    runtime_trace("CreateRemoteThread", {
        diagnostics::TraceField{"symbol", "CreateRemoteThread"},
        diagnostics::TraceField{"status", "unsupported"},
        diagnostics::TraceField{"mechanism", "stub"},
        diagnostics::TraceField{"detail", "threads remotos não implementados"}}, 4);
    return nullptr;
}
TL_MSABI void* tl_VirtualAllocEx(void* const process, void* const addr, const std::size_t size, const std::uint32_t type, const std::uint32_t protect) noexcept {
    (void)process;
    return tl_VirtualAlloc(addr, size, type, protect);
}
TL_MSABI int tl_VirtualFreeEx(void* const process, void* const addr, const std::size_t size, const std::uint32_t type) noexcept {
    (void)process;
    return tl_VirtualFree(addr, size, type);
}
TL_MSABI int tl_WriteProcessMemory(void* const process, void* const base, const void* const buf, const std::size_t size, std::size_t* const written) noexcept {
    (void)process; (void)base; (void)buf; (void)size;
    if (written != nullptr && !mapped_guest_range(written, sizeof(*written), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (written != nullptr) *written = 0;
    set_last_error(abi::kErrorNotSupported);
    runtime_trace("WriteProcessMemory", {
        diagnostics::TraceField{"symbol", "WriteProcessMemory"},
        diagnostics::TraceField{"status", "unsupported"},
        diagnostics::TraceField{"mechanism", "stub"},
        diagnostics::TraceField{"detail", "memória de outro processo não implementada"}}, 4);
    return 0;
}
TL_MSABI int tl_OpenFile(const char* const file, void* const of_struct, const std::uint32_t style) noexcept {
    (void)of_struct; (void)style;
    if (file == nullptr || !mapped_guest_cstring(file)) {
        set_last_error(abi::kErrorInvalidParameter);
        return -1;
    }
    void* handle = tl_CreateFileA(file, abi::kGenericRead | abi::kGenericWrite, 0, nullptr, abi::kOpenExisting, 0, nullptr);
    if (handle == reinterpret_cast<void*>(~static_cast<std::uintptr_t>(0)) || handle == nullptr) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(g_files_mutex);
    for (std::size_t i = 0; i < g_files.size(); ++i) {
        if (g_files[i].used && &g_files[i] == handle) {
            return static_cast<int>(i + 1); // 1-based handle
        }
    }
    return -1;
}
TL_MSABI void* tl_OpenEventA(const std::uint32_t access, const int inherit, const char* const name) noexcept {
    (void)access; (void)inherit; (void)name;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x45564E54ULL); // 'EVNT'
}
TL_MSABI void* tl_OpenFileMappingA(const std::uint32_t access, const int inherit, const char* const name) noexcept {
    (void)access; (void)inherit; (void)name;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x464D4150ULL); // 'FMAP'
}
TL_MSABI int tl__lclose(const int fd) noexcept {
    if (fd <= 0) {
        set_last_error(abi::kErrorInvalidHandle);
        return -1;
    }
    std::lock_guard<std::mutex> lock(g_files_mutex);
    const std::size_t index = static_cast<std::size_t>(fd - 1);
    if (index < g_files.size() && g_files[index].used) {
        if (g_files[index].fd >= 0) {
            ::close(g_files[index].fd);
        }
        g_files[index].used = false;
        g_files[index].fd = -1;
        g_files[index].path.clear();
        set_last_error(abi::kErrorSuccess);
        return 0;
    }
    set_last_error(abi::kErrorInvalidHandle);
    return -1;
}
TL_MSABI int tl_FlushInstructionCache(void* const process, const void* const base, const std::size_t size) noexcept {
    (void)process; (void)base; (void)size;
    set_last_error(abi::kErrorSuccess);
    return 1;
}
TL_MSABI int tl_SetThreadContext(void* const thread, const void* const ctx) noexcept {
    (void)thread; (void)ctx;
    set_last_error(abi::kErrorSuccess);
    return 1;
}
TL_MSABI int tl_GetThreadContext(void* const thread, void* const ctx) noexcept {
    (void)thread;
    if (ctx != nullptr && mapped_guest_range(ctx, 1232, true)) std::memset(ctx, 0, 1232);
    set_last_error(abi::kErrorSuccess);
    return 1;
}
TL_MSABI std::uint32_t tl_SuspendThread(void* const thread) noexcept {
    (void)thread;
    set_last_error(abi::kErrorSuccess);
    return 0;
}
TL_MSABI int tl_VirtualProtectEx(void* const process, void* const addr, const std::size_t size, const std::uint32_t prot, std::uint32_t* const old) noexcept {
    (void)process;
    return tl_VirtualProtect(addr, size, prot, old);
}
TL_MSABI int tl_lstrcmpA(const char* const s1, const char* const s2) noexcept {
    if (s1 == s2) return 0;
    if (s1 == nullptr) return -1;
    if (s2 == nullptr) return 1;
    return std::strcmp(s1, s2);
}
TL_MSABI int tl_IsThreadAFiber(void) noexcept {
    return 0;
}
TL_MSABI void* tl_InterlockedFlushSList(void* const head) noexcept {
    if (head == nullptr) return nullptr;
    void** h = static_cast<void**>(head);
    void* first = *h;
    *h = nullptr;
    return first;
}

// --- RTSSHooks SETUPAPI ---
TL_MSABI void* tl_SetupDiGetClassDevsA(const void* const guid, const char* const enumerator, void* const parent, const std::uint32_t flags) noexcept {
    (void)guid; (void)enumerator; (void)parent; (void)flags;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x53455455ULL); // 'SETU'
}
TL_MSABI int tl_SetupDiEnumDeviceInfo(void* const dev_info, const std::uint32_t idx, void* const dev_data) noexcept {
    (void)dev_info; (void)idx; (void)dev_data;
    set_last_error(18); // ERROR_NO_MORE_FILES
    return 0;
}
TL_MSABI int tl_SetupDiEnumDeviceInterfaces(void* const dev_info, void* const dev_data, const void* const guid, const std::uint32_t idx, void* const iface_data) noexcept {
    (void)dev_info; (void)dev_data; (void)guid; (void)idx; (void)iface_data;
    set_last_error(18);
    return 0;
}
TL_MSABI int tl_SetupDiGetDeviceInterfaceDetailA(void* const dev_info, void* const iface_data, void* const detail, const std::uint32_t size, std::uint32_t* const needed, void* const dev_data) noexcept {
    (void)dev_info; (void)iface_data; (void)detail; (void)size; (void)dev_data;
    if (needed != nullptr && mapped_guest_range(needed, sizeof(*needed), true)) *needed = 0;
    set_last_error(abi::kErrorSuccess);
    return 0;
}
TL_MSABI int tl_SetupDiGetDeviceRegistryPropertyA(void* const dev_info, void* const dev_data, const std::uint32_t prop, std::uint32_t* const reg_type, std::uint8_t* const buf, const std::uint32_t buf_size, std::uint32_t* const needed) noexcept {
    (void)dev_info; (void)dev_data; (void)prop;
    if (reg_type != nullptr && mapped_guest_range(reg_type, sizeof(*reg_type), true)) *reg_type = 1;
    if (needed != nullptr && mapped_guest_range(needed, sizeof(*needed), true)) *needed = 0;
    if (buf != nullptr && buf_size > 0 && mapped_guest_range(buf, buf_size, true)) buf[0]=0;
    set_last_error(abi::kErrorSuccess);
    return 0;
}
TL_MSABI int tl_SetupDiGetDeviceInstanceIdA(void* const dev_info, void* const dev_data, char* const id, const std::uint32_t size, std::uint32_t* const needed) noexcept {
    (void)dev_info; (void)dev_data;
    if (needed != nullptr && mapped_guest_range(needed, sizeof(*needed), true)) *needed = 0;
    if (id != nullptr && size > 0 && mapped_guest_range(id, size, true)) id[0]='\0';
    set_last_error(abi::kErrorSuccess);
    return 0;
}
TL_MSABI int tl_SetupDiDestroyDeviceInfoList(void* const dev_info) noexcept {
    (void)dev_info;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

// --- RTSSHooks DirectX stubs ---
TL_MSABI int tl_D3DCompile(const void* const src, const std::size_t src_size, const char* const src_name, const void* const defines, void* const include, const char* const entry, const char* const target, const std::uint32_t flags1, const std::uint32_t flags2, void** const code, void** const errors) noexcept {
    (void)src; (void)src_size; (void)src_name; (void)defines; (void)include; (void)entry; (void)target; (void)flags1; (void)flags2;
    if (code != nullptr && mapped_guest_range(code, sizeof(*code), true)) *code = nullptr;
    if (errors != nullptr && mapped_guest_range(errors, sizeof(*errors), true)) *errors = nullptr;
    return static_cast<int>(0x80004005); // E_FAIL
}
TL_MSABI int tl_D3D12SerializeRootSignature(const void* const root_sig, const std::uint32_t version, void** const blob, void** const error) noexcept {
    (void)root_sig; (void)version;
    if (blob != nullptr && mapped_guest_range(blob, sizeof(*blob), true)) *blob = nullptr;
    if (error != nullptr && mapped_guest_range(error, sizeof(*error), true)) *error = nullptr;
    return static_cast<int>(0x80004005);
}
TL_MSABI int tl_CreateDXGIFactory1(const void* const riid, void** const factory) noexcept {
    (void)riid;
    if (factory != nullptr && mapped_guest_range(factory, sizeof(*factory), true)) *factory = nullptr;
    return static_cast<int>(0x887A0004); // DXGI_ERROR_UNSUPPORTED
}
TL_MSABI int tl_DirectDrawCreateEx(const void* const guid, void** const dd, const void* const iid, void* const unk) noexcept {
    (void)guid; (void)iid; (void)unk;
    if (dd != nullptr && mapped_guest_range(dd, sizeof(*dd), true)) *dd = nullptr;
    return static_cast<int>(0x80004002); // E_NOINTERFACE
}
TL_MSABI int tl_Direct3DCreate9(const std::uint32_t version) noexcept {
    (void)version;
    return 0;
}
TL_MSABI int tl_Direct3DCreate9Ex(const std::uint32_t version, void** const d3d) noexcept {
    (void)version;
    if (d3d != nullptr && mapped_guest_range(d3d, sizeof(*d3d), true)) *d3d = nullptr;
    return static_cast<int>(0x8876086A); // D3DERR_NOTAVAILABLE
}
TL_MSABI int tl_D3D10CreateDeviceAndSwapChain(void* const adapter, const std::uint32_t driver, void* const sw, const std::uint32_t flags, const std::uint32_t feature, void* const swap_desc, void** const swap_chain, void** const device) noexcept {
    (void)adapter; (void)driver; (void)sw; (void)flags; (void)feature; (void)swap_desc;
    if (swap_chain != nullptr && mapped_guest_range(swap_chain, sizeof(*swap_chain), true)) *swap_chain = nullptr;
    if (device != nullptr && mapped_guest_range(device, sizeof(*device), true)) *device = nullptr;
    return static_cast<int>(0x80004002); // E_NOINTERFACE
}
TL_MSABI int tl_D3DX10CompileFromMemory(const char* const src, const std::size_t len, const char* const src_name, const void* const defines, void* const include, const char* const entry, const char* const profile, const std::uint32_t flags1, const std::uint32_t flags2, void* const pump, void** const shader, void** const errors, void** const hr) noexcept {
    (void)src; (void)len; (void)src_name; (void)defines; (void)include; (void)entry; (void)profile; (void)flags1; (void)flags2; (void)pump;
    if (shader != nullptr && mapped_guest_range(shader, sizeof(*shader), true)) *shader = nullptr;
    if (errors != nullptr && mapped_guest_range(errors, sizeof(*errors), true)) *errors = nullptr;
    if (hr != nullptr && mapped_guest_range(hr, sizeof(*hr), true)) *hr = nullptr;
    return static_cast<int>(0x80004005);
}
TL_MSABI int tl_D3D11CreateDeviceAndSwapChain(void* const adapter, const std::uint32_t driver, void* const sw, const std::uint32_t flags, const void* const feature_levels, const std::uint32_t levels, const std::uint32_t sdk, void* const swap_desc, void** const swap_chain, void** const device, void* const feature, void* const ctx) noexcept {
    (void)adapter; (void)driver; (void)sw; (void)flags; (void)feature_levels; (void)levels; (void)sdk; (void)swap_desc; (void)feature; (void)ctx;
    if (swap_chain != nullptr && mapped_guest_range(swap_chain, sizeof(*swap_chain), true)) *swap_chain = nullptr;
    if (device != nullptr && mapped_guest_range(device, sizeof(*device), true)) *device = nullptr;
    return static_cast<int>(0x887A0004); // DXGI_ERROR_UNSUPPORTED
}
TL_MSABI int tl_D3DX11CompileFromMemory(const char* const src, const std::size_t len, const char* const src_name, const void* const defines, void* const include, const char* const entry, const char* const target, const std::uint32_t flags1, const std::uint32_t flags2, void* const pump, void** const code, void** const errors, void** const hr) noexcept {
    (void)src; (void)len; (void)src_name; (void)defines; (void)include; (void)entry; (void)target; (void)flags1; (void)flags2; (void)pump;
    if (code != nullptr && mapped_guest_range(code, sizeof(*code), true)) *code = nullptr;
    if (errors != nullptr && mapped_guest_range(errors, sizeof(*errors), true)) *errors = nullptr;
    if (hr != nullptr && mapped_guest_range(hr, sizeof(*hr), true)) *hr = nullptr;
    return static_cast<int>(0x80004005);
}

TL_MSABI int dummy_worker_check() noexcept {
    return 1;
}

}  // extern "C"
}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_winapi_stubs_module() {
    static const ExportedFunction kSensApiExports[] = {
        {"IsDestinationReachableW", 1, reinterpret_cast<std::uintptr_t>(&tl_IsDestinationReachableW)},
        {"IsNetworkAlive", 2, reinterpret_cast<std::uintptr_t>(&tl_IsNetworkAlive)},
    };
    static const InternalModule kSensApiModule{"SensApi.dll", kSensApiExports};
    register_module(kSensApiModule);
    static const ExportedFunction kSetupApiExports[] = {
        {"CM_Get_Child", 1, reinterpret_cast<std::uintptr_t>(&tl_CM_Get_Child)},
        {"SetupDiGetClassDevsA", 2, reinterpret_cast<std::uintptr_t>(&tl_SetupDiGetClassDevsA)},
        {"SetupDiEnumDeviceInfo", 3, reinterpret_cast<std::uintptr_t>(&tl_SetupDiEnumDeviceInfo)},
        {"SetupDiEnumDeviceInterfaces", 4, reinterpret_cast<std::uintptr_t>(&tl_SetupDiEnumDeviceInterfaces)},
        {"SetupDiGetDeviceInterfaceDetailA", 5, reinterpret_cast<std::uintptr_t>(&tl_SetupDiGetDeviceInterfaceDetailA)},
        {"SetupDiGetDeviceRegistryPropertyA", 6, reinterpret_cast<std::uintptr_t>(&tl_SetupDiGetDeviceRegistryPropertyA)},
        {"SetupDiGetDeviceInstanceIdA", 7, reinterpret_cast<std::uintptr_t>(&tl_SetupDiGetDeviceInstanceIdA)},
        {"SetupDiDestroyDeviceInfoList", 8, reinterpret_cast<std::uintptr_t>(&tl_SetupDiDestroyDeviceInfoList)},
    };
    static const InternalModule kSetupApiModule{"SETUPAPI.dll", kSetupApiExports};
    register_module(kSetupApiModule);
    static const InternalModule kCfgmgr32Module{"CFGMGR32.dll", kSetupApiExports};
    register_module(kCfgmgr32Module);
    static const ExportedFunction kNetApi32Exports[] = {
        {"NetApiBufferFree", 1, reinterpret_cast<std::uintptr_t>(&tl_NetApiBufferFree)},
    };
    static const InternalModule kNetApi32Module{"NETAPI32.dll", kNetApi32Exports};
    register_module(kNetApi32Module);
    static const ExportedFunction kOleAccExports[] = {
        {"LresultFromObject", 1, reinterpret_cast<std::uintptr_t>(&tl_LresultFromObject)},
    };
    static const InternalModule kOleAccModule{"OLEACC.dll", kOleAccExports};
    register_module(kOleAccModule);
    static const ExportedFunction kTdhExports[] = {
        {"TdhGetPropertySize", 1, reinterpret_cast<std::uintptr_t>(&tl_TdhGetPropertySize)},
    };
    static const InternalModule kTdhModule{"tdh.dll", kTdhExports};
    register_module(kTdhModule);
    static const ExportedFunction kWinspoolExports[] = {
        {"OpenPrinterW", 1, reinterpret_cast<std::uintptr_t>(&tl_OpenPrinterW)},
        {"ClosePrinter", 2, reinterpret_cast<std::uintptr_t>(&tl_CloseHandle)},
        {"DocumentPropertiesW", 3, reinterpret_cast<std::uintptr_t>(&tl_OpenPrinterW)},
    };
    static const InternalModule kWinspoolModule{"WINSPOOL.DRV", kWinspoolExports};
    register_module(kWinspoolModule);
    static const ExportedFunction kWtsApi32Exports[] = {
        {"WTSFreeMemory", 1, reinterpret_cast<std::uintptr_t>(&tl_WTSFreeMemory)},
        {"WTSEnumerateSessionsW", 2, reinterpret_cast<std::uintptr_t>(&tl_WTSFreeMemory)},
        {"WTSQuerySessionInformationW", 3, reinterpret_cast<std::uintptr_t>(&tl_WTSFreeMemory)},
    };
    static const InternalModule kWtsApi32Module{"WTSAPI32.dll", kWtsApi32Exports};
    register_module(kWtsApi32Module);
    static const ExportedFunction kD3dCompiler47Exports[] = {
        {"D3DCompile", 1, reinterpret_cast<std::uintptr_t>(&tl_D3DCompile)},
    };
    static const InternalModule kD3dCompiler47Module{"D3DCOMPILER_47.dll", kD3dCompiler47Exports};
    register_module(kD3dCompiler47Module);
    static const ExportedFunction kD3d12Exports[] = {
        {"D3D12SerializeRootSignature", 1, reinterpret_cast<std::uintptr_t>(&tl_D3D12SerializeRootSignature)},
        {"", 101, reinterpret_cast<std::uintptr_t>(&tl_D3D12SerializeRootSignature)},
    };
    static const InternalModule kD3d12Module{"d3d12.dll", kD3d12Exports};
    register_module(kD3d12Module);
    static const ExportedFunction kDxgiExports[] = {
        {"CreateDXGIFactory1", 1, reinterpret_cast<std::uintptr_t>(&tl_CreateDXGIFactory1)},
    };
    static const InternalModule kDxgiModule{"dxgi.dll", kDxgiExports};
    register_module(kDxgiModule);
    static const ExportedFunction kDdrawExports[] = {
        {"DirectDrawCreateEx", 1, reinterpret_cast<std::uintptr_t>(&tl_DirectDrawCreateEx)},
    };
    static const InternalModule kDdrawModule{"DDRAW.dll", kDdrawExports};
    register_module(kDdrawModule);
    static const ExportedFunction kD3d9Exports[] = {
        {"Direct3DCreate9", 1, reinterpret_cast<std::uintptr_t>(&tl_Direct3DCreate9)},
        {"Direct3DCreate9Ex", 2, reinterpret_cast<std::uintptr_t>(&tl_Direct3DCreate9Ex)},
    };
    static const InternalModule kD3d9Module{"d3d9.dll", kD3d9Exports};
    register_module(kD3d9Module);
    static const ExportedFunction kD3d10Exports[] = {
        {"D3D10CreateDeviceAndSwapChain", 1, reinterpret_cast<std::uintptr_t>(&tl_D3D10CreateDeviceAndSwapChain)},
    };
    static const InternalModule kD3d10Module{"d3d10.dll", kD3d10Exports};
    register_module(kD3d10Module);
    static const ExportedFunction kD3dx10Exports[] = {
        {"D3DX10CompileFromMemory", 1, reinterpret_cast<std::uintptr_t>(&tl_D3DX10CompileFromMemory)},
    };
    static const InternalModule kD3dx10Module{"d3dx10_42.dll", kD3dx10Exports};
    register_module(kD3dx10Module);
    static const ExportedFunction kD3d11Exports[] = {
        {"D3D11CreateDeviceAndSwapChain", 1, reinterpret_cast<std::uintptr_t>(&tl_D3D11CreateDeviceAndSwapChain)},
    };
    static const InternalModule kD3d11Module{"d3d11.dll", kD3d11Exports};
    register_module(kD3d11Module);
    static const ExportedFunction kD3dx11Exports[] = {
        {"D3DX11CompileFromMemory", 1, reinterpret_cast<std::uintptr_t>(&tl_D3DX11CompileFromMemory)},
    };
    static const InternalModule kD3dx11Module{"d3dx11_42.dll", kD3dx11Exports};
    register_module(kD3dx11Module);
}

}  // namespace tradutorlinux::loader
