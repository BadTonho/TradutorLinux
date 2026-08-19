#include "tradutorlinux/runtime/winapi.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/gui/x11.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/process.hpp"
#include "tradutorlinux/pe/pe_reader.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/runtime/msvcrt.hpp"
#include "tradutorlinux/util/basics.hpp"
#include "gui_controls.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <cerrno>
#include <charconv>
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
#include <optional>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tradutorlinux {
namespace {

thread_local std::jmp_buf g_guest_exit_context;
thread_local bool g_guest_execution_active = false;
thread_local std::uint32_t g_guest_exit_code = 0;
thread_local std::uint32_t g_last_error = abi::kErrorSuccess;

// Caminho do executável convidado, definido antes da execução.
std::string g_module_file_name;

extern "C" void tl_call_guest_on_stack(std::uintptr_t entry,
                                         std::uintptr_t stack_top) noexcept;

char kStdInputToken = 0;
char kStdOutputToken = 0;
char kStdErrorToken = 0;

// Tokens opacos para stock objects do GDI: o próprio endereço serve de handle
// e o deslocamento identifica o objeto. Stock objects não são liberados.
char kStockObjectTokens[24]{};

struct FileSlot {
    int fd{-1};
    bool used{false};
    std::uint64_t file_size{0};
    std::int64_t position{0};
    std::string path;
};

struct AllocationSlot {
    void* address{nullptr};
    std::size_t size{0};
};

// Uma faixa contígua do espaço de endereçamento com permissões uniformes,
// conforme /proc/self/maps.
struct GuestMapRegion {
    std::uintptr_t start{};
    std::uintptr_t end{};
    bool readable{false};
    bool writable{false};
};

std::mutex g_files_mutex;
std::array<FileSlot, 256> g_files{};

std::mutex g_allocations_mutex;
std::array<AllocationSlot, 256> g_allocations{};

// Visão somente-leitura da imagem convidada corrente, usada pelas APIs de
// recursos. O loader configura estes valores antes de transferir o controle
// ao entry point; nenhum recurso é acessado sem validação contra essa faixa.
const std::byte* g_guest_image_base{nullptr};
std::size_t g_guest_image_size{0};
std::uint32_t g_guest_resource_rva{0};
std::uint32_t g_guest_resource_size{0};

constexpr std::uintptr_t kResourceHandleBase = 0x0000A00000000000ULL;
struct ResourceSlot {
    bool used{false};
    std::uint32_t data_rva{0};
    std::uint32_t data_size{0};
};
std::mutex g_resource_mutex;
std::array<ResourceSlot, 256> g_resources{};

constexpr std::uintptr_t kSyncHandleBase = 0x0000600000000000ULL;
enum class SyncKind { Mutex, Event, Semaphore, Process };
struct SyncSlot {
    bool used{false};
    SyncKind kind{SyncKind::Event};
    std::mutex mutex;
    std::condition_variable condition;
    bool signaled{false};
    bool manual_reset{false};
    bool owner_valid{false};
    std::thread::id owner{};
    std::uint32_t recursion{0};
    std::int32_t count{0};
    std::int32_t maximum{0};
    pid_t child_pid{-1};
    int child_result_fd{-1};
    bool process_running{false};
    std::uint32_t process_exit_code{259U};  // STILL_ACTIVE
};
std::mutex g_sync_mutex;
std::array<SyncSlot, 256> g_syncs{};

[[nodiscard]] std::uint32_t wait_process_slot(SyncSlot& slot,
                                              std::uint32_t milliseconds) noexcept;

// --- Fase 11: Concorrência ---

// Identificador único da thread principal do convidado.
constexpr std::uint32_t kMainThreadId = 1;
thread_local std::uint32_t g_current_thread_id = kMainThreadId;
std::atomic<std::uint32_t> g_next_thread_id{kMainThreadId + 1};

// Slot de thread convidada.
struct ThreadSlot {
    bool used{false};
    std::uint32_t thread_id{};
    void* teb{nullptr};                    // mmap'd TEB page
    std::byte* stack{nullptr};             // mmap'd guest stack
    std::uintptr_t stack_top{};
    std::function<void()> thread_func;     // lambda que executa o guest code
    std::thread host_thread;               // thread hospedeira
    std::mutex join_mutex;                 // protege joined/cancelled
    bool finished{false};                  // thread concluiu
    bool joined{false};                    // WaitForSingleObject retornou
    int exit_code{0};                      // código de saída da thread
    std::condition_variable finish_cv;     // sinaliza quando finished==true
};
std::mutex g_threads_mutex;
std::array<ThreadSlot, 256> g_threads{};

// Handle de thread: ponteiro para um slot de thread, para manter compatibilidade
// com a existente file-handle scheme.
constexpr std::uintptr_t kThreadHandleBase = 0x0000400000000000ULL;

void* thread_slot_to_handle(ThreadSlot& slot) noexcept {
    const auto index = static_cast<std::size_t>(&slot - g_threads.data());
    return std::bit_cast<void*>(kThreadHandleBase + index);
}

ThreadSlot* find_thread_slot(const void* handle) noexcept {
    if (handle == nullptr) {
        return nullptr;
    }
    const auto addr = std::bit_cast<std::uintptr_t>(handle);
    if (addr >= kThreadHandleBase && addr < kThreadHandleBase + g_threads.size()) {
        const auto index = static_cast<std::size_t>(addr - kThreadHandleBase);
        ThreadSlot& slot = g_threads[index];
        return slot.used ? &slot : nullptr;
    }
    return nullptr;
}

// Índices TLS globais (máximo 256 slots, como g_guest_tls_slots).
constexpr std::uint32_t kMaxTlsSlots = 256;
std::array<bool, kMaxTlsSlots> g_tls_indices_used{};

// --- CRITICAL_SECTION: side-table com pthread_mutex_t ---
struct CriticalSectionEntry {
    void* guest_address{nullptr};
    pthread_mutex_t mutex{};
    bool used{false};
};
std::mutex g_cs_mutex;
std::array<CriticalSectionEntry, 256> g_critical_sections{};

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
    pthread_mutex_init(&it->mutex, nullptr);
    return &*it;
}

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
        case EEXIST:
            return abi::kErrorAlreadyExists;
        case EPIPE:
            // Escrever em um pipe sem leitor (ex.: stdout para `head -c0`) é
            // um erro controlado de I/O, não uma morte por sinal.
            return abi::kErrorBrokenPipe;
        default:
            return abi::kErrorInvalidParameter;
    }
}

void bump_guest_allocation_generation() noexcept {
    runtime::invalidate_memory_map_cache();
}

[[nodiscard]] bool mapped_guest_range(const void* address, const std::size_t size,
                                      const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
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

[[nodiscard]] ResourceSlot* find_resource_slot(const void* handle) noexcept {
    if (handle == nullptr) {
        return nullptr;
    }
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(handle);
    if (value < kResourceHandleBase || value >= kResourceHandleBase + g_resources.size()) {
        return nullptr;
    }
    ResourceSlot& slot = g_resources[value - kResourceHandleBase];
    return slot.used ? &slot : nullptr;
}

[[nodiscard]] void* resource_slot_handle(ResourceSlot& slot) noexcept {
    return reinterpret_cast<void*>(kResourceHandleBase +
                                   static_cast<std::uintptr_t>(&slot - g_resources.data()));
}

[[nodiscard]] SyncSlot* find_sync_slot(const void* handle) noexcept {
    if (handle == nullptr) {
        return nullptr;
    }
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(handle);
    if (value < kSyncHandleBase || value >= kSyncHandleBase + g_syncs.size()) {
        return nullptr;
    }
    SyncSlot& slot = g_syncs[value - kSyncHandleBase];
    return slot.used ? &slot : nullptr;
}

[[nodiscard]] void* sync_slot_handle(SyncSlot& slot) noexcept {
    return reinterpret_cast<void*>(kSyncHandleBase +
                                   static_cast<std::uintptr_t>(&slot - g_syncs.data()));
}

void clear_sync_slot(SyncSlot& slot) noexcept {
    if (slot.child_result_fd >= 0) {
        close(slot.child_result_fd);
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

[[nodiscard]] bool sync_is_signaled(SyncSlot& slot) noexcept {
    if (slot.kind == SyncKind::Event) {
        return slot.signaled;
    }
    if (slot.kind == SyncKind::Semaphore) {
        return slot.count > 0;
    }
    if (slot.kind == SyncKind::Process) {
        return !slot.process_running;
    }
    return !slot.owner_valid || slot.owner == std::this_thread::get_id();
}

void consume_sync_signal(SyncSlot& slot) noexcept {
    if (slot.kind == SyncKind::Event) {
        if (!slot.manual_reset) {
            slot.signaled = false;
        }
    } else if (slot.kind == SyncKind::Semaphore) {
        --slot.count;
    } else if (slot.kind == SyncKind::Process) {
        // Process handles remain signaled after completion.
    } else if (slot.owner_valid && slot.owner == std::this_thread::get_id()) {
        ++slot.recursion;
    } else {
        slot.owner_valid = true;
        slot.owner = std::this_thread::get_id();
        slot.recursion = 1;
    }
}

[[nodiscard]] std::uint32_t wait_sync_slot(SyncSlot& slot,
                                           const std::uint32_t milliseconds) noexcept {
    if (slot.kind == SyncKind::Process) {
        return wait_process_slot(slot, milliseconds);
    }
    std::unique_lock<std::mutex> lock(slot.mutex);
    const auto predicate = [&]() { return !slot.used || sync_is_signaled(slot); };
    if (milliseconds == abi::kInfinite) {
        slot.condition.wait(lock, predicate);
    } else if (!slot.condition.wait_for(lock, std::chrono::milliseconds(milliseconds), predicate)) {
        return abi::kWaitTimeout;
    }
    if (!slot.used) {
        return abi::kWaitFailed;
    }
    consume_sync_signal(slot);
    return abi::kWaitObject0;
}

[[nodiscard]] bool mapped_guest_cstring(const char* value) noexcept {
    if (value == nullptr) {
        return true;
    }
    return runtime::validate_mapped_cstring(value);
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

// Tradução de caminho Windows → Linux. Converte barras invertidas para
// normais, rejeita letras de drive e caminhos absolutos. Retorna false se o
// caminho for inválido.
[[nodiscard]] bool translate_windows_path(const char* win_path,
                                          char* linux_out,
                                          std::size_t out_size) noexcept {
    if (win_path == nullptr || win_path[0] == '\0' || win_path[0] == '/' ||
        win_path[0] == '\\') {
        return false;
    }
    std::size_t length = 0;
    for (; win_path[length] != '\0'; ++length) {
        if (length + 1U >= out_size || win_path[length] == ':') {
            return false;
        }
        linux_out[length] = win_path[length] == '\\' ? '/' : win_path[length];
    }
    linux_out[length] = '\0';
    return true;
}

// WIN32_FIND_DATAA (layout exato do mingw: FILETIME = 2 DWORDs, align 4;
// cFileName em 44, tamanho total 320).
struct Win32FindDataA {
    std::uint32_t dw_file_attributes{0};
    std::uint32_t ft_creation_time_lo{0};
    std::uint32_t ft_creation_time_hi{0};
    std::uint32_t ft_last_access_time_lo{0};
    std::uint32_t ft_last_access_time_hi{0};
    std::uint32_t ft_last_write_time_lo{0};
    std::uint32_t ft_last_write_time_hi{0};
    std::uint32_t n_file_size_high{0};
    std::uint32_t n_file_size_low{0};
    std::uint32_t dw_reserved0{0};
    std::uint32_t dw_reserved1{0};
    char c_file_name[260]{};
    char c_alternate_file_name[14]{};
};
static_assert(sizeof(Win32FindDataA) == 320);
static_assert(offsetof(Win32FindDataA, c_file_name) == 44);

// Atributos de arquivo Win32.
constexpr std::uint32_t kFileAttributeReadOnly = 0x00000001U;
constexpr std::uint32_t kFileAttributeHidden = 0x00000002U;
constexpr std::uint32_t kFileAttributeSystem = 0x00000004U;
constexpr std::uint32_t kFileAttributeDirectory = 0x00000010U;
constexpr std::uint32_t kFileAttributeArchive = 0x00000020U;
constexpr std::uint32_t kFileAttributeNormal = 0x00000080U;

// Handle sentinela para FindFirstFile/FindNextFile: usa um intervalo alto do
// espaço de ponteiros para distinguir de handles de arquivo normais.
constexpr std::uintptr_t kFindHandleBase = 0x0000800000000000ULL;

struct FindSlot {
    bool used{false};
    DIR* dir{nullptr};
    std::string pattern;
    std::string directory;
};
std::array<FindSlot, 16> g_find_slots{};

[[nodiscard]] FindSlot* find_slot_for_handle(const void* handle) noexcept {
    const auto idx = reinterpret_cast<std::uintptr_t>(handle) - kFindHandleBase;
    if (idx >= g_find_slots.size()) {
        return nullptr;
    }
    return &g_find_slots[idx];
}

// Retorna os atributos Win32 a partir de stat(). Bit somente-leitura é
// derivado da permissão de escrita.
[[nodiscard]] std::uint32_t stat_to_win32_attributes(const char* path,
                                                     const struct stat& st) noexcept {
    if (S_ISDIR(st.st_mode)) {
        return kFileAttributeDirectory | kFileAttributeArchive;
    }
    std::uint32_t attrs = kFileAttributeArchive;
    if (access(path, W_OK) != 0) {
        attrs |= kFileAttributeReadOnly;
    }
    return attrs;
}

struct ClassSlot {
    bool used{false};
    std::string name;
    std::uintptr_t wndproc{0};
};

using runtime_gui::ControlKind;
using runtime_gui::GuestTimer;
using runtime_gui::ListViewRow;
using runtime_gui::WindowSlot;
using runtime_gui::control_kind_for;
using runtime_gui::is_builtin_control;
using runtime_gui::queue_window_message;

std::array<ClassSlot, 32> g_classes{};
std::array<WindowSlot, 32> g_windows{};
WindowSlot* g_focused_control = nullptr;
bool g_quit_requested = false;
std::uint32_t g_quit_code = 0;

struct MenuSlot {
    bool used{false};
    std::vector<gui::PopupMenuItem> items;
};
std::array<MenuSlot, 16> g_menus{};

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

void render_controls(WindowSlot& parent) noexcept {
    runtime_gui::render_controls(parent, std::span<WindowSlot>{g_windows});
}

void handle_control_key(WindowSlot& parent, const gui::WindowEvent& event) noexcept {
    runtime_gui::handle_control_key(parent, std::span<WindowSlot>{g_windows}, g_focused_control,
                                    event);
}

void handle_control_mouse(WindowSlot& parent, const gui::WindowEvent& event) noexcept {
    runtime_gui::handle_control_mouse(parent, std::span<WindowSlot>{g_windows}, g_focused_control,
                                      event);
}

void set_focus_control(WindowSlot* control) noexcept {
    runtime_gui::set_focus_control(control, g_focused_control);
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

// KeySyms X11 usados na fronteira (valores estáveis do protocolo X11). A
// camada gui entrega o keysym em WindowEvent; este mapeamento mantém o X11
// fora do módulo de runtime.
constexpr unsigned long kKeysymBackspace = 0xFF08;
constexpr unsigned long kKeysymTab = 0xFF09;
constexpr unsigned long kKeysymReturn = 0xFF0D;
constexpr unsigned long kKeysymEscape = 0xFF1B;
constexpr unsigned long kKeysymLeft = 0xFF51;
constexpr unsigned long kKeysymUp = 0xFF52;
constexpr unsigned long kKeysymRight = 0xFF53;
constexpr unsigned long kKeysymDown = 0xFF54;
constexpr unsigned long kKeysymDelete = 0xFFFF;

// Índice do stock object correspondente a um token opaco (o endereço dentro da
// tabela estática de tokens), ou -1 quando o ponteiro não pertence à tabela.
[[nodiscard]] int stock_object_index(const void* const token) noexcept {
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

// Região lida do /proc/self/maps: intervalo, permissões e presença de arquivo.
struct MapsRegion {
    std::uintptr_t start{0};
    std::uintptr_t end{0};
    char perms[4]{};  // r w x p
    bool has_path{false};
};

[[nodiscard]] bool find_maps_region(const void* const address, MapsRegion& region) noexcept {
    if (address == nullptr) {
        return false;
    }
    const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(address);
    std::ifstream maps{"/proc/self/maps"};
    std::string line;
    while (std::getline(maps, line)) {
        const std::size_t dash = line.find('-');
        const std::size_t space = line.find(' ', dash == std::string::npos ? 0 : dash);
        if (dash == std::string::npos || space == std::string::npos || dash == 0) {
            continue;
        }
        std::uintptr_t start{0};
        std::uintptr_t end{0};
        if (std::from_chars(line.data(), line.data() + dash, start, 16).ec != std::errc{} ||
            std::from_chars(line.data() + dash + 1, line.data() + space, end, 16).ec != std::errc{} ||
            start > end || target < start || target >= end) {
            continue;
        }
        const std::size_t permissions_offset = space + 1;
        if (line.size() < permissions_offset + 4) {
            return false;
        }
        region.start = start;
        region.end = end;
        region.perms[0] = line[permissions_offset];
        region.perms[1] = line[permissions_offset + 1];
        region.perms[2] = line[permissions_offset + 2];
        region.perms[3] = line[permissions_offset + 3];
        region.has_path = line.find('/', permissions_offset + 4) != std::string::npos;
        return true;
    }
    return false;
}

[[nodiscard]] std::uint32_t win32_protection(const char perms[4]) noexcept {
    const bool readable = perms[0] == 'r';
    const bool writable = perms[1] == 'w';
    const bool executable = perms[2] == 'x';
    if (writable) {
        return executable ? abi::kPageExecuteReadWrite : abi::kPageReadWrite;
    }
    if (executable) {
        return readable ? abi::kPageExecuteRead : abi::kPageExecute;
    }
    return readable ? abi::kPageReadOnly : abi::kPageNoAccess;
}

[[nodiscard]] int host_protection(const std::uint32_t protection) noexcept {
    switch (protection) {
        case abi::kPageNoAccess:
            return PROT_NONE;
        case abi::kPageReadOnly:
            return PROT_READ;
        case abi::kPageReadWrite:
        case abi::kPageWriteCopy:
            return PROT_READ | PROT_WRITE;
        case abi::kPageExecute:
            return PROT_EXEC;
        case abi::kPageExecuteRead:
            return PROT_READ | PROT_EXEC;
        case abi::kPageExecuteReadWrite:
        case abi::kPageExecuteWriteCopy:
            return PROT_READ | PROT_WRITE | PROT_EXEC;
        default:
            return -1;
    }
}

// Handler registrado pelo convidado via SetUnhandledExceptionFilter. O runtime
// não invoca o handler (o convidado é de console e a falha é reportada pelo
// trace), mas o valor é armazenado para preservar a semântica da API.
std::uintptr_t g_unhandled_exception_filter = 0;

// Slots TLS por thread hospedeira. O convidado roda em uma única thread e o
// índice vem do __tls_index de seu módulo; nenhum slot é inicializado nesta
// fase, então TlsGetValue retorna null como no Windows para índice não usado.
thread_local std::array<void*, 64> g_guest_tls_slots{};

// Sequência inválida de entrada nas conversões de codepage (fora do intervalo
// Unicode). Utilizado como sentinela interno das conversões.
constexpr std::uint32_t kInvalidCodepoint = 0x110000U;

// CP1252: mapeamento dos bytes de controle 0x80-0x9F para Unicode. Os demais
// bytes são iguais ao Latin-1 (0xA0-0xFF -> U+00A0-U+00FF).
constexpr std::array<std::uint32_t, 32> kCp1252Control{
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

[[nodiscard]] std::uint32_t cp1252_to_unicode(const std::uint8_t byte) noexcept {
    if (byte >= 0x80U && byte <= 0x9FU) {
        return kCp1252Control[static_cast<std::size_t>(byte - 0x80U)];
    }
    return static_cast<std::uint32_t>(byte);
}

[[nodiscard]] bool unicode_to_cp1252(const std::uint32_t codepoint, std::uint8_t& byte) noexcept {
    if (codepoint <= 0xFFU) {
        byte = static_cast<std::uint8_t>(codepoint);
        return true;
    }
    const auto found = std::find(kCp1252Control.begin(), kCp1252Control.end(), codepoint);
    if (found == kCp1252Control.end()) {
        return false;
    }
    byte = static_cast<std::uint8_t>(0x80U + static_cast<std::size_t>(found - kCp1252Control.begin()));
    return true;
}

// Decodifica um caractere de UTF-8 ou CP1252 a partir de `bytes`; avança `pos`.
// Retorna kInvalidCodepoint para sequência inválida (consumindo 1 byte).
[[nodiscard]] std::uint32_t decode_multibyte(const std::uint32_t code_page,
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
        return cp1252_to_unicode(first);
    }
    std::size_t needed = 0;
    std::uint32_t value = 0;
    if ((first & 0xE0U) == 0xC0U) {
        needed = 2;
        value = static_cast<std::uint32_t>(first & 0x1FU);
    } else if ((first & 0xF0U) == 0xE0U) {
        needed = 3;
        value = static_cast<std::uint32_t>(first & 0x0FU);
    } else if ((first & 0xF8U) == 0xF0U) {
        needed = 4;
        value = static_cast<std::uint32_t>(first & 0x07U);
    } else {
        pos += 1;
        return kInvalidCodepoint;
    }
    if (pos + needed > length) {
        pos += 1;
        return kInvalidCodepoint;
    }
    for (std::size_t index = 1; index < needed; ++index) {
        const std::uint8_t continuation = bytes[pos + index];
        if ((continuation & 0xC0U) != 0x80U) {
            pos += 1;
            return kInvalidCodepoint;
        }
        value = (value << 6U) | static_cast<std::uint32_t>(continuation & 0x3FU);
    }
    pos += needed;
    const bool overlong = (needed == 2 && value < 0x80U) ||
                          (needed == 3 && value < 0x800U) ||
                          (needed == 4 && value < 0x10000U);
    if (overlong || value > 0x10FFFFU || (value >= 0xD800U && value <= 0xDFFFU)) {
        return kInvalidCodepoint;
    }
    return value;
}

[[nodiscard]] std::size_t utf16_units_for(const std::uint32_t codepoint,
                                          std::uint16_t out[2]) noexcept {
    if (codepoint < 0x10000U) {
        out[0] = static_cast<std::uint16_t>(codepoint);
        return 1;
    }
    const std::uint32_t value = codepoint - 0x10000U;
    out[0] = static_cast<std::uint16_t>(0xD800U | (value >> 10U));
    out[1] = static_cast<std::uint16_t>(0xDC00U | (value & 0x3FFU));
    return 2;
}

[[nodiscard]] std::uint32_t decode_utf16(const std::uint16_t* const units,
                                         const std::size_t length,
                                         std::size_t& pos) noexcept {
    const std::uint16_t first = units[pos];
    if (first >= 0xD800U && first <= 0xDBFFU) {
        if (pos + 1 < length && units[pos + 1] >= 0xDC00U && units[pos + 1] <= 0xDFFFU) {
            const std::uint32_t value =
                (static_cast<std::uint32_t>(first - 0xD800U) << 10U) |
                static_cast<std::uint32_t>(units[pos + 1] - 0xDC00U);
            pos += 2;
            return value + 0x10000U;
        }
        pos += 1;
        return kInvalidCodepoint;
    }
    if (first >= 0xDC00U && first <= 0xDFFFU) {
        pos += 1;
        return kInvalidCodepoint;
    }
    pos += 1;
    return static_cast<std::uint32_t>(first);
}

[[nodiscard]] std::size_t utf8_bytes_for(const std::uint32_t codepoint, char out[4]) noexcept {
    if (codepoint < 0x80U) {
        out[0] = static_cast<char>(codepoint);
        return 1;
    }
    if (codepoint < 0x800U) {
        out[0] = static_cast<char>(0xC0U | (codepoint >> 6U));
        out[1] = static_cast<char>(0x80U | (codepoint & 0x3FU));
        return 2;
    }
    if (codepoint < 0x10000U) {
        out[0] = static_cast<char>(0xE0U | (codepoint >> 12U));
        out[1] = static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
        out[2] = static_cast<char>(0x80U | (codepoint & 0x3FU));
        return 3;
    }
    out[0] = static_cast<char>(0xF0U | (codepoint >> 18U));
    out[1] = static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU));
    out[2] = static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
    out[3] = static_cast<char>(0x80U | (codepoint & 0x3FU));
    return 4;
}

// CRITICAL_SECTION é um token opaco para a fronteira: o convidado roda em uma
// única thread, então a exclusão mútua é trivialmente satisfeita e a estrutura
// interna do convidado nunca é tocada. As funções validam apenas o ponteiro.
bool critical_section_valid(void* const critical_section) noexcept {
    if (critical_section == nullptr ||
        !mapped_guest_range(critical_section, 8, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CriticalSection", "critical-section", "ponteiro inválido");
        return false;
    }
    set_last_error(abi::kErrorSuccess);
    return true;
}

// TEB (Thread Environment Block) mínimo Microsoft x64. O convidado mingw lê o
// endereço do próprio TEB via %gs:[0x30] e campos como StackBase (offset 0x8).
// Em Linux x86-64 o segmento GS é livre, então a fronteira aloca um TEB de uma
// página, o aponta via arch_prctl(ARCH_SET_GS) durante a execução do convidado
// e restaura o GS após o retorno.
struct GuestTeb {
    void* exception_list{nullptr};                // 0x00
    void* stack_base{nullptr};                    // 0x08
    void* stack_limit{nullptr};                   // 0x10
    void* sub_system_tib{nullptr};                // 0x18
    void* fiber_data{nullptr};                    // 0x20
    void* arbitrary_user_pointer{nullptr};        // 0x28
    void* self{nullptr};                          // 0x30
    void* environment_pointer{nullptr};           // 0x38
    std::uint64_t client_id[2]{0, 0};             // 0x40
    void* active_rpc_handle{nullptr};             // 0x50
    void* thread_local_storage_pointer{nullptr};  // 0x58
    void* peb{nullptr};                           // 0x60
    std::uint8_t reserved[0x110]{};               // até 0x178
    std::uint64_t tls_slots[64]{};                // 0x178
    std::uint8_t tail[0xC88]{};                   // até 0x1000 (tamanho da página)
};
static_assert(sizeof(GuestTeb) == 0x1000);

// Configura a base do segmento GS da thread atual (Linux x86-64). Em Linux o
// FS é usado pelo TLS do hospedeiro; GS fica livre para a fronteira de ABI.
bool set_guest_gs_base(const void* const base) noexcept {
    constexpr long kArchSetGs = 0x1001;  // ARCH_SET_GS
    return ::syscall(SYS_arch_prctl, kArchSetGs,
                     static_cast<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(base))) == 0;
}

// Aloca um TEB de uma página e configura os campos essenciais.
[[nodiscard]] void* allocate_guest_teb(const std::uintptr_t stack_top,
                                        const std::uintptr_t stack_size) noexcept {
    constexpr std::size_t kTebSize = sizeof(GuestTeb);
    void* const teb = mmap(nullptr, kTebSize, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (teb == MAP_FAILED) {
        return nullptr;
    }
    auto* const fields = static_cast<GuestTeb*>(teb);
    fields->self = teb;
    fields->stack_base = std::bit_cast<void*>(stack_top);
    fields->stack_limit = std::bit_cast<void*>(stack_top - stack_size);
    return teb;
}

void free_guest_teb(void* const teb) noexcept {
    if (teb != nullptr) {
        static_cast<void>(munmap(teb, sizeof(GuestTeb)));
    }
}

// Virtual key do subconjunto suportado. Teclas especiais são mapeadas pelo
// keysym; letras usam a maiúscula (como VK_A), demais caracteres ASCII
// imprimíveis usam o próprio valor. O keysym de letras/dígitos já reflete o
// estado de Shift (XLookupString).
[[nodiscard]] abi::Wparam keydown_vkey(const unsigned long keysym,  // NOLINT(bugprone-easily-swappable-parameters)
                                       const char character) noexcept {
    switch (keysym) {
        case kKeysymBackspace:
            return abi::kVkBack;
        case kKeysymTab:
            return abi::kVkTab;
        case kKeysymReturn:
            return abi::kVkReturn;
        case kKeysymEscape:
            return abi::kVkEscape;
        case kKeysymLeft:
            return abi::kVkLeft;
        case kKeysymUp:
            return abi::kVkUp;
        case kKeysymRight:
            return abi::kVkRight;
        case kKeysymDown:
            return abi::kVkDown;
        case kKeysymDelete:
            return abi::kVkDelete;
        default:
            break;
    }
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
    if (FileSlot* slot = find_file_slot(handle); slot != nullptr) {
        slot->position += total;
        if (static_cast<std::uint64_t>(slot->position) > slot->file_size) {
            slot->file_size = static_cast<std::uint64_t>(slot->position);
        }
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
    if (FileSlot* slot = find_file_slot(handle); slot != nullptr) {
        slot->position += result;
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
    const std::size_t page = util::host_page_size();
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
    bump_guest_allocation_generation();
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
            bump_guest_allocation_generation();
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
    if (!mapped_guest_cstring(path) || path == nullptr || path[0] == '\0' ||
        share_mode != 0 || security_attributes != nullptr || flags != 0 ||
        template_file != nullptr ||
        (desired_access & ~(abi::kGenericRead | abi::kGenericWrite)) != 0 ||
        (creation_disposition != abi::kCreateAlways &&
         creation_disposition != abi::kOpenExisting)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    char normalized[4096]{};
    if (!translate_windows_path(path, normalized, sizeof(normalized))) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
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
        slot.position = 0;
        slot.path = normalized;
        struct stat st{};
        if (fstat(fd, &st) == 0) {
            slot.file_size = static_cast<std::uint64_t>(st.st_size);
        }
        set_last_error(abi::kErrorSuccess);
        return &slot;
    }
    close(fd);
    set_last_error(abi::kErrorNotEnoughMemory);
    return nullptr;
}

TL_MSABI int tl_CloseHandle(const void* const handle) noexcept {
    // Tenta como handle de thread primeiro.
    ThreadSlot* thread = find_thread_slot(handle);
    if (thread != nullptr) {
        if (!thread->used) {
            set_last_error(abi::kErrorInvalidHandle);
            trace_guest_failure("CloseHandle", "handle-validation", "handle de thread inválido");
            return 0;
        }
        // Faz join se necessário.
        if (thread->host_thread.joinable() && !thread->joined) {
            thread->host_thread.join();
            thread->joined = true;
        }
        // Libera a pilha convidada.
        if (thread->stack != nullptr) {
            const std::uintptr_t total_size = (thread->stack_top -
                std::bit_cast<std::uintptr_t>(thread->stack));
            static_cast<void>(mprotect(thread->stack, 0x1000, PROT_READ | PROT_WRITE));
            static_cast<void>(munmap(thread->stack, static_cast<std::size_t>(total_size)));
            thread->stack = nullptr;
        }
        thread->used = false;
        thread->finished = false;
        thread->joined = false;
        set_last_error(abi::kErrorSuccess);
        return 1;
    }

    SyncSlot* sync = find_sync_slot(handle);
    if (sync != nullptr) {
        if (sync->kind == SyncKind::Process && sync->process_running) {
            const std::uint32_t result = wait_process_slot(*sync, 5000U);
            if (result == abi::kWaitTimeout && sync->child_pid > 0) {
                static_cast<void>(::kill(sync->child_pid, SIGKILL));
                static_cast<void>(wait_process_slot(*sync, abi::kInfinite));
            }
        }
        {
            std::lock_guard<std::mutex> lock(sync->mutex);
            clear_sync_slot(*sync);
        }
        sync->condition.notify_all();
        set_last_error(abi::kErrorSuccess);
        return 1;
    }

    // Tenta como handle de arquivo.
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
                                      const std::uint32_t, const int x, const int y,
                                      const int width, const int height, const void* const parent,
                                      const void* const menu, const void* const,
                                      const void* const) noexcept {
    if (!mapped_guest_cstring(class_name) || class_name == nullptr ||
        (window_name != nullptr && !mapped_guest_cstring(window_name))) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateWindowExA", "strings", "nome de classe ou janela inválido");
        return nullptr;
    }
    ClassSlot* const cls = find_class_slot(class_name);
    if (cls == nullptr && !is_builtin_control(class_name)) {
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
    if (is_builtin_control(class_name)) {
        WindowSlot& slot = *free_it;
        WindowSlot* parent_slot = find_window_slot(parent);
        if (parent_slot == nullptr || parent_slot->is_control) {
            set_last_error(abi::kErrorInvalidHandle);
            return nullptr;
        }
        slot = {};
        slot.used = true;
        slot.class_name = class_name;
        slot.is_control = true;
        slot.control_kind = control_kind_for(class_name);
        slot.parent = parent_slot;
        slot.control_id = reinterpret_cast<std::uintptr_t>(menu);
        slot.x = x;
        slot.y = y;
        slot.width = width > 0 ? width : 1;
        slot.height = height > 0 ? height : 1;
        slot.text = window_name != nullptr ? window_name : "";
        slot.visible = true;
        slot.enabled = true;
        slot.combo_selection = -1;
        set_last_error(abi::kErrorSuccess);
        return &slot;
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
    slot.window_title = caption;
    slot.native = native;
    slot.x = x;
    slot.y = y;
    slot.width = width;
    slot.height = height;
    const abi::Lresult create_result = call_wndproc(slot.wndproc, &slot, abi::kWmCreate, 0, 0);
    if (create_result == -1) {
        gui::destroy_window(slot.native);
        slot = {};
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateWindowExA", "wm-create", "WM_CREATE rejeitou a criação");
        return nullptr;
    }
    render_controls(slot);
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
    const bool was_visible = slot->visible;
    if (cmd_show == 0) {
        // A bandeja é uma janela X11 emulada: mantemos o cliente mapeado para
        // que o botão secundário continue abrindo o menu mesmo quando a
        // visibilidade lógica da janela principal é FALSE.
        slot->visible = false;
    } else if (slot->native != nullptr) {
        slot->mapped = gui::map_window(slot->native);
        slot->visible = true;
    } else {
        slot->visible = true;
    }
    if (slot->is_control && slot->parent != nullptr) {
        render_controls(*slot->parent);
    }
    set_last_error(abi::kErrorSuccess);
    return was_visible ? 1 : 0;
}

TL_MSABI int tl_UpdateWindow(const void* const window) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("UpdateWindow", "handle-validation", "handle inválido");
        return 0;
    }
    if (slot->wndproc != 0) {
        call_wndproc(slot->wndproc, const_cast<abi::HWnd>(window), abi::kWmPaint, 0, 0);
    }
    render_controls(*slot);
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
        if (!slot.queued_messages.empty()) {
            const abi::GuestMsg queued = slot.queued_messages.front();
            slot.queued_messages.pop_front();
            write_guest_msg(msg, queued.hwnd, queued.message, queued.wparam, queued.lparam);
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
                // Expose is o redesenho real do client area. O convidado pode
                // não implementar WM_PAINT (os controles são desenhados pelo
                // renderer Linux), então repintamos a composição completa
                // antes de entregar a mensagem equivalente ao convidado.
                render_controls(slot);
                write_guest_msg(msg, &slot, abi::kWmPaint, 0, 0);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::Press) {
                handle_control_mouse(slot, event);
                slot.left_button_down = true;
                const abi::Lparam lparam =
                    (static_cast<std::intptr_t>(event.y & 0xFFFF) << 16) |
                    static_cast<std::intptr_t>(event.x & 0xFFFF);
                write_guest_msg(msg, &slot, abi::kWmLButtonDown, abi::kMkLButton, lparam);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::Release) {
                handle_control_mouse(slot, event);
                slot.left_button_down = false;
                const abi::Lparam lparam =
                    (static_cast<std::intptr_t>(event.y & 0xFFFF) << 16) |
                    static_cast<std::intptr_t>(event.x & 0xFFFF);
                write_guest_msg(msg, &slot, abi::kWmLButtonUp, 0, lparam);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::MouseMove) {
                const abi::Lparam lparam =
                    (static_cast<std::intptr_t>(event.y & 0xFFFF) << 16) |
                    static_cast<std::intptr_t>(event.x & 0xFFFF);
                const abi::Wparam wparam = slot.left_button_down ? abi::kMkLButton : 0;
                write_guest_msg(msg, &slot, abi::kWmMouseMove, wparam, lparam);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::KeyDown) {
                slot.last_key = event.character;
                handle_control_key(slot, event);
                write_guest_msg(msg, &slot, abi::kWmKeyDown,
                                keydown_vkey(event.keysym, event.character), 0);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::RightPress) {
                write_guest_msg(msg, &slot, abi::kWmTrayIcon, 0, 0x0205);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::KeyUp) {
                write_guest_msg(msg, &slot, abi::kWmKeyUp,
                                keydown_vkey(event.keysym, event.character), 0);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::CloseRequested) {
                write_guest_msg(msg, &slot, abi::kWmClose, 0, 0);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        for (WindowSlot& slot : g_windows) {
            if (!slot.used || (window != nullptr && window != &slot)) {
                continue;
            }
            for (GuestTimer& timer : slot.timers) {
                if (now >= timer.deadline) {
                    write_guest_msg(msg, &slot, abi::kWmTimer, timer.id, 0);
                    timer.deadline = std::chrono::steady_clock::now() + timer.interval;
                    set_last_error(abi::kErrorSuccess);
                    const std::array<diagnostics::TraceField, 4> fields{
                        diagnostics::TraceField{"symbol", "GetMessageA"},
                        diagnostics::TraceField{"message", "WM_TIMER"},
                        diagnostics::TraceField{"id", std::to_string(timer.id)},
                        diagnostics::TraceField{"status", "delivered"},
                    };
                    runtime_trace("GetMessageA", fields, 4);
                    return 1;
                }
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
    WindowSlot* const parent = slot->parent;
    if (g_focused_control == slot) {
        g_focused_control = nullptr;
    }
    slot->native = nullptr;
    slot->mapped = false;
    const abi::HWnd hwnd = const_cast<abi::HWnd>(window);
    const std::uintptr_t wndproc = slot->wndproc;
    *slot = {};
    if (wndproc != 0) {
        call_wndproc(wndproc, hwnd, abi::kWmDestroy, 0, 0);
    }
    if (parent != nullptr) {
        render_controls(*parent);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void tl_PostQuitMessage(const int exit_code) noexcept {
    g_quit_code = static_cast<std::uint32_t>(exit_code);
    g_quit_requested = true;
}

TL_MSABI std::uintptr_t tl_SetTimer(const void* const window,  // NOLINT(bugprone-easily-swappable-parameters)
                                    const std::uintptr_t id,
                                    const std::uint32_t elapsed_ms,
                                    const void* const timer_proc) noexcept {
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr || elapsed_ms == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("SetTimer", "argument-validation",
                            "hwnd inválido ou intervalo zero");
        return 0;
    }
    if (timer_proc != nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("SetTimer", "timer-proc",
                            "TIMERPROC ainda não suportado; use WM_TIMER");
        return 0;
    }
    const auto interval = std::chrono::milliseconds{elapsed_ms};
    GuestTimer* timer = nullptr;
    const auto found = std::find_if(slot->timers.begin(), slot->timers.end(),
                                    [id](const GuestTimer& entry) { return entry.id == id; });
    if (found != slot->timers.end()) {
        timer = &*found;
    } else {
        slot->timers.push_back(GuestTimer{});
        timer = &slot->timers.back();
        timer->id = id;
    }
    timer->interval = interval;
    timer->deadline = std::chrono::steady_clock::now() + interval;
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "SetTimer"},
        diagnostics::TraceField{"id", std::to_string(id)},
        diagnostics::TraceField{"elapsed-ms", std::to_string(elapsed_ms)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("SetTimer", fields, 4);
    return id;
}

TL_MSABI int tl_KillTimer(const void* const window, const std::uintptr_t id) noexcept {
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("KillTimer", "handle-validation", "hwnd inválido");
        return 0;
    }
    const auto found = std::find_if(slot->timers.begin(), slot->timers.end(),
                                    [id](const GuestTimer& timer) { return timer.id == id; });
    if (found == slot->timers.end()) {
        set_last_error(abi::kErrorSuccess);
        return 0;
    }
    slot->timers.erase(found);
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "KillTimer"},
        diagnostics::TraceField{"id", std::to_string(id)},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"result", "killed"},
    };
    runtime_trace("KillTimer", fields, 4);
    return 1;
}

// Tokens opacos para stock objects do GDI: o próprio endereço serve de handle
// e o deslocamento identifica o objeto. Stock objects não são liberados.
// (Definido no namespace anônimo; o deslocamento do token é o índice do objeto.)
TL_MSABI void* tl_GetStockObject(const int object) noexcept {
    if (object < 0 || object >= 24) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("GetStockObject", "object", "stock object fora da faixa suportada");
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "GetStockObject"},
        diagnostics::TraceField{"object", std::to_string(object)},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"mechanism", "token"},
    };
    runtime_trace("GetStockObject", fields, 4);
    return kStockObjectTokens + object;
}

// Inicia a pintura de uma janela: preenche o PAINTSTRUCT convidado e devolve
// um HDC (o próprio handle de janela, que identifica o destino de desenho).
TL_MSABI void* tl_BeginPaint(const void* const window,  // NOLINT(bugprone-easily-swappable-parameters)
                             void* const paint_struct) noexcept {
    if (paint_struct == nullptr ||
        !mapped_guest_range(paint_struct, sizeof(abi::GuestPaintStruct), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("BeginPaint", "paint-struct", "ponteiro sem permissão de escrita");
        return nullptr;
    }
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr || slot->native == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("BeginPaint", "handle-validation", "hwnd inválido");
        return nullptr;
    }
    auto* const ps = static_cast<abi::GuestPaintStruct*>(paint_struct);
    *ps = {};
    ps->hdc = const_cast<void*>(window);
    ps->f_erase = 1;
    ps->rc_paint = {0, 0, slot->width, slot->height};
    slot->painting = true;
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "BeginPaint"},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"mechanism", "hdc=hwnd"},
        diagnostics::TraceField{"result", "painting"},
    };
    runtime_trace("BeginPaint", fields, 4);
    return ps->hdc;
}

TL_MSABI int tl_EndPaint(const void* const window,  // NOLINT(bugprone-easily-swappable-parameters)
                         const void* const paint_struct) noexcept {
    if (paint_struct == nullptr ||
        !mapped_guest_range(paint_struct, sizeof(abi::GuestPaintStruct), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("EndPaint", "paint-struct", "ponteiro inválido");
        return 0;
    }
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("EndPaint", "handle-validation", "hwnd inválido");
        return 0;
    }
    slot->painting = false;
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "EndPaint"},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"result", "painted"},
        diagnostics::TraceField{"mechanism", "hdc=hwnd"},
    };
    runtime_trace("EndPaint", fields, 4);
    return 1;
}

TL_MSABI int tl_TextOut(const void* const dc, const int x, const int y,  // NOLINT(bugprone-easily-swappable-parameters)
                        const char* const text, const int length) noexcept {
    if (text == nullptr || length < 0 ||
        (length > 0 &&
         !mapped_guest_range(text, static_cast<std::size_t>(length), false))) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("TextOut", "text", "ponteiro ou comprimento inválido");
        return 0;
    }
    WindowSlot* const slot = find_window_slot(dc);
    if (slot == nullptr || slot->native == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("TextOut", "dc", "HDC inválido");
        return 0;
    }
    if (length > 0) {
        gui::draw_text_len(slot->native, text, length, x, y);
        gui::flush_window(slot->native);
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "TextOut"},
        diagnostics::TraceField{"x", std::to_string(x)},
        diagnostics::TraceField{"y", std::to_string(y)},
        diagnostics::TraceField{"length", std::to_string(length)},
    };
    runtime_trace("TextOut", fields, 4);
    return 1;
}

TL_MSABI int tl_FillRect(const void* const dc,  // NOLINT(bugprone-easily-swappable-parameters)
                         const void* const rect, const void* const brush) noexcept {
    if (rect == nullptr || !mapped_guest_range(rect, sizeof(abi::GuestRect), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("FillRect", "rect", "ponteiro RECT inválido");
        return 0;
    }
    WindowSlot* const slot = find_window_slot(dc);
    if (slot == nullptr || slot->native == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("FillRect", "dc", "HDC inválido");
        return 0;
    }
    const int brush_index = stock_object_index(brush);
    if (brush_index < 0 || brush_index >= 6) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("FillRect", "brush", "brush deve ser um stock object WHITE..NULL");
        return 0;
    }
    const auto* const rc = static_cast<const abi::GuestRect*>(rect);
    const int width = rc->right - rc->left;
    const int height = rc->bottom - rc->top;
    if (width > 0 && height > 0 && brush_index != 5) {
        gui::fill_rectangle(slot->native, rc->left, rc->top, width, height, brush_index);
        gui::flush_window(slot->native);
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "FillRect"},
        diagnostics::TraceField{"brush", std::to_string(brush_index)},
        diagnostics::TraceField{"rect", std::to_string(rc->left) + "," + std::to_string(rc->top) +
                                     "-" + std::to_string(rc->right) + "," +
                                     std::to_string(rc->bottom)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("FillRect", fields, 4);
    return 1;
}

TL_MSABI int tl_Rectangle(const void* const dc,  // NOLINT(bugprone-easily-swappable-parameters)
                          const int left, const int top, const int right,
                          const int bottom) noexcept {
    WindowSlot* const slot = find_window_slot(dc);
    if (slot == nullptr || slot->native == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("Rectangle", "dc", "HDC inválido");
        return 0;
    }
    const int width = right - left;
    const int height = bottom - top;
    if (width > 0 && height > 0) {
        gui::draw_rectangle(slot->native, left, top, width, height);
        gui::flush_window(slot->native);
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "Rectangle"},
        diagnostics::TraceField{"rect", std::to_string(left) + "," + std::to_string(top) + "-" +
                                     std::to_string(right) + "," + std::to_string(bottom)},
        diagnostics::TraceField{"mechanism", "contorno"},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("Rectangle", fields, 4);
    return 1;
}

TL_MSABI void* tl_GetDC(const void* const window) noexcept {
    const WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr || slot->native == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("GetDC", "handle-validation", "hwnd inválido");
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "GetDC"},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"mechanism", "hdc=hwnd"},
        diagnostics::TraceField{"result", "window-dc"},
    };
    runtime_trace("GetDC", fields, 4);
    return const_cast<void*>(window);
}

TL_MSABI int tl_ReleaseDC(const void* const window, const void* const dc) noexcept {
    if (window == nullptr || dc != window) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("ReleaseDC", "dc", "HDC não pertence à janela");
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "ReleaseDC"},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"mechanism", "hdc=hwnd"},
        diagnostics::TraceField{"result", "released"},
    };
    runtime_trace("ReleaseDC", fields, 4);
    return 1;
}

TL_MSABI void tl_InitializeCriticalSection(void* const critical_section) noexcept {
    if (!critical_section_valid(critical_section)) {
        return;
    }
    CriticalSectionEntry* entry = find_cs_entry(critical_section);
    if (entry == nullptr) {
        entry = alloc_cs_entry(critical_section);
        if (entry == nullptr) {
            set_last_error(abi::kErrorNotEnoughMemory);
            trace_guest_failure("InitializeCriticalSection", "side-table",
                                "exaustão de slots de critical section");
        }
    }
}

TL_MSABI void tl_DeleteCriticalSection(void* const critical_section) noexcept {
    if (!critical_section_valid(critical_section)) {
        return;
    }
    CriticalSectionEntry* entry = find_cs_entry(critical_section);
    if (entry != nullptr) {
        pthread_mutex_destroy(&entry->mutex);
        entry->used = false;
        entry->guest_address = nullptr;
    }
}

TL_MSABI void tl_EnterCriticalSection(void* const critical_section) noexcept {
    if (!critical_section_valid(critical_section)) {
        return;
    }
    CriticalSectionEntry* entry = find_cs_entry(critical_section);
    if (entry != nullptr) {
        pthread_mutex_lock(&entry->mutex);
    }
}

TL_MSABI void tl_LeaveCriticalSection(void* const critical_section) noexcept {
    if (!critical_section_valid(critical_section)) {
        return;
    }
    CriticalSectionEntry* entry = find_cs_entry(critical_section);
    if (entry != nullptr) {
        pthread_mutex_unlock(&entry->mutex);
    }
}

TL_MSABI int tl_GetConsoleMode(const void* const handle, std::uint32_t* const mode) noexcept {
    if (mode == nullptr || !mapped_guest_range(mode, sizeof(*mode), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("GetConsoleMode", "mode", "ponteiro sem permissão de escrita");
        return 0;
    }
    const int fd = handle_fd(handle);
    if (fd < 0 || ::isatty(fd) == 0) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    *mode = fd == STDIN_FILENO ? 0x3U : 0x3U;
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "GetConsoleMode"},
        diagnostics::TraceField{"fd", std::to_string(fd)},
        diagnostics::TraceField{"mode", std::to_string(*mode)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("GetConsoleMode", fields, 4);
    return 1;
}

TL_MSABI int tl_SetConsoleMode(const void* const handle, const std::uint32_t mode) noexcept {
    const int fd = handle_fd(handle);
    if (fd < 0) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "SetConsoleMode"},
        diagnostics::TraceField{"fd", std::to_string(fd)},
        diagnostics::TraceField{"mode", std::to_string(mode)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("SetConsoleMode", fields, 4);
    return 1;
}

TL_MSABI int tl_IsDBCSLeadByteEx(const std::uint32_t, const std::uint8_t) noexcept {
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI std::uintptr_t tl_SetUnhandledExceptionFilter(const std::uintptr_t handler) noexcept {
    const std::uintptr_t previous = g_unhandled_exception_filter;
    g_unhandled_exception_filter = handler;
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "SetUnhandledExceptionFilter"},
        diagnostics::TraceField{"handler", std::to_string(handler)},
        diagnostics::TraceField{"previous", std::to_string(previous)},
        diagnostics::TraceField{"mechanism", "registrado-sem-invocacao"},
    };
    runtime_trace("SetUnhandledExceptionFilter", fields, 4);
    return previous;
}

TL_MSABI void tl_Sleep(const std::uint32_t milliseconds) noexcept {
    timespec requested{
        .tv_sec = static_cast<std::time_t>(milliseconds / 1000U),
        .tv_nsec = static_cast<long>((milliseconds % 1000U) * 1000000L),
    };
    timespec remaining{};
    while (nanosleep(&requested, &remaining) != 0 && errno == EINTR) {
        requested = remaining;
    }
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI void* tl_TlsGetValue(const std::uint32_t tls_index) noexcept {
    if (tls_index >= g_guest_tls_slots.size()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return g_guest_tls_slots[tls_index];
}

TL_MSABI int tl_VirtualProtect(void* const address, const std::uintptr_t size, // NOLINT(bugprone-easily-swappable-parameters)
                               const std::uint32_t new_protection,
                               std::uint32_t* const old_protection) noexcept {
    if (old_protection == nullptr ||
        !mapped_guest_range(old_protection, sizeof(*old_protection), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("VirtualProtect", "old-protection", "ponteiro sem permissão de escrita");
        return 0;
    }
    const int prot = host_protection(new_protection);
    if (address == nullptr || size == 0 || prot < 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    MapsRegion region{};
    if (!find_maps_region(address, region)) {
        set_last_error(abi::kErrorInvalidAddress);
        trace_guest_failure("VirtualProtect", "region-lookup", "endereço não mapeado");
        return 0;
    }
    *old_protection = win32_protection(region.perms);
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(address);
    if (start > std::numeric_limits<std::uintptr_t>::max() - size) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uintptr_t end = start + size;
    if (start < region.start || end > region.end) {
        set_last_error(abi::kErrorInvalidAddress);
        return 0;
    }
    constexpr std::uintptr_t kPageSize = 0x1000U;
    const std::uintptr_t rounded_start = start / kPageSize * kPageSize;
    std::uintptr_t rounded_end = (end + kPageSize - 1U) / kPageSize * kPageSize;
    if (rounded_end > region.end) {
        rounded_end = region.end;
    }
    if (rounded_start >= rounded_end ||
        mprotect(std::bit_cast<void*>(rounded_start),
                 static_cast<std::size_t>(rounded_end - rounded_start), prot) != 0) {
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        trace_linux_failure("VirtualProtect", "mprotect", errno, failure_error);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "VirtualProtect"},
        diagnostics::TraceField{"address", std::to_string(start)},
        diagnostics::TraceField{"protection", std::to_string(new_protection)},
        diagnostics::TraceField{"old-protection", std::to_string(*old_protection)},
    };
    runtime_trace("VirtualProtect", fields, 4);
    return 1;
}

TL_MSABI std::uintptr_t tl_VirtualQuery(const void* const address, void* const memory_information, // NOLINT(bugprone-easily-swappable-parameters)
                                        const std::uintptr_t length) noexcept {
    if (memory_information == nullptr ||
        length < sizeof(abi::GuestMemoryBasicInformation) ||
        !mapped_guest_range(memory_information, sizeof(abi::GuestMemoryBasicInformation), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("VirtualQuery", "memory-information", "buffer de saída inválido");
        return 0;
    }
    MapsRegion region{};
    if (!find_maps_region(address, region)) {
        set_last_error(abi::kErrorInvalidAddress);
        return 0;
    }
    const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(address);
    const AllocationSlot* allocation = nullptr;
    for (const AllocationSlot& candidate : g_allocations) {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(candidate.address);
        if (candidate.address != nullptr && target >= base &&
            target - base < candidate.size) {
            allocation = &candidate;
            break;
        }
    }
    auto* const info = static_cast<abi::GuestMemoryBasicInformation*>(memory_information);
    info->base_address = allocation != nullptr ? allocation->address
                                               : std::bit_cast<void*>(region.start);
    info->allocation_base = allocation != nullptr ? allocation->address
                                                   : std::bit_cast<void*>(region.start);
    info->allocation_protect = win32_protection(region.perms);
    info->padding1 = 0;
    info->region_size = allocation != nullptr
                            ? static_cast<std::uintptr_t>(allocation->size)
                            : static_cast<std::uintptr_t>(region.end - region.start);
    info->state = abi::kMemCommit;
    info->protect = win32_protection(region.perms);
    info->type = allocation != nullptr || !region.has_path ? abi::kMemPrivate
                                                           : abi::kMemImage;
    info->padding2 = 0;
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "VirtualQuery"},
        diagnostics::TraceField{"address", std::to_string(reinterpret_cast<std::uintptr_t>(address))},
        diagnostics::TraceField{"region-size", std::to_string(info->region_size)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("VirtualQuery", fields, 4);
    return sizeof(abi::GuestMemoryBasicInformation);
}

TL_MSABI int tl_MultiByteToWideChar(const std::uint32_t code_page, const std::uint32_t flags, // NOLINT(bugprone-easily-swappable-parameters)
                                    const char* const mb_str, const int mb_count,
                                    std::uint16_t* const wide_str, const int wide_count) noexcept {
    const bool supported_page = code_page == abi::kCpAcp || code_page == abi::kCp1252 ||
                                code_page == abi::kCpUtf8;
    if (mb_str == nullptr || mb_count == 0 || !supported_page ||
        (flags & ~(abi::kMbPrecomposed | abi::kMbErrInvalidChars)) != 0 ||
        (wide_count != 0 && wide_str == nullptr)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const bool null_terminated = mb_count == -1;
    if (null_terminated && !mapped_guest_cstring(mb_str)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("MultiByteToWideChar", "source", "string convidada inválida");
        return 0;
    }
    const std::uint32_t byte_count =
        null_terminated ? static_cast<std::uint32_t>(std::strlen(mb_str))
                        : static_cast<std::uint32_t>(mb_count);
    if (!mapped_guest_range(mb_str, byte_count, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("MultiByteToWideChar", "source", "memória convidada inválida");
        return 0;
    }
    std::fprintf(stderr, "[tl][dbg] MultiByteToWideChar cp=%u flags=%u src=\"%s\" (mb_count=%d)\n",
                 code_page, flags, mb_str, mb_count);
    const auto* const bytes = reinterpret_cast<const std::uint8_t*>(mb_str);
    std::size_t index = 0;
    std::size_t needed = 0;
    while (index < byte_count) {
        std::uint32_t codepoint = decode_multibyte(code_page, bytes, byte_count, index);
        if (codepoint > 0x10FFFFU) {
            if ((flags & abi::kMbErrInvalidChars) != 0) {
                set_last_error(abi::kErrorNoUnicodeTranslation);
                return 0;
            }
            codepoint = 0x3FU;
        }
        std::uint16_t units[2]{};
        needed += utf16_units_for(codepoint, units);
    }
    if (null_terminated) {
        needed += 1;
    }
    if (needed > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (wide_str == nullptr) {
        set_last_error(abi::kErrorSuccess);
        return static_cast<int>(needed);
    }
    if (wide_count < 0 || static_cast<std::size_t>(wide_count) < needed ||
        !mapped_guest_range(wide_str, needed * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    index = 0;
    std::size_t written = 0;
    while (index < byte_count) {
        std::uint32_t codepoint = decode_multibyte(code_page, bytes, byte_count, index);
        if (codepoint > 0x10FFFFU) {
            codepoint = 0x3FU;
        }
        std::uint16_t units[2]{};
        const std::size_t count = utf16_units_for(codepoint, units);
        for (std::size_t unit = 0; unit < count; ++unit) {
            wide_str[written] = units[unit];
            written += 1;
        }
    }
    if (null_terminated) {
        wide_str[written] = 0;
        written += 1;
    }
    std::fprintf(stderr, "[tl][dbg]   MultiByteToWideChar dst=%p wrote=%zu units:", static_cast<void*>(wide_str),
                 written);
    for (std::size_t unit = 0; unit < written && unit < 8; ++unit) {
        std::fprintf(stderr, " %04X", static_cast<unsigned>(wide_str[unit]));
    }
    std::fprintf(stderr, "\n");
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(written);
}

TL_MSABI int tl_WideCharToMultiByte(const std::uint32_t code_page, const std::uint32_t flags, // NOLINT(bugprone-easily-swappable-parameters)
                                    const std::uint16_t* const wide_str, const int wide_count,
                                    char* const mb_str, const int mb_count,
                                    const char* const default_char,
                                    int* const used_default_char) noexcept {
    const bool supported_page = code_page == abi::kCpAcp || code_page == abi::kCp1252 ||
                                code_page == abi::kCpUtf8;
    if (wide_str == nullptr || wide_count == 0 || !supported_page ||
        (flags & ~(abi::kWcCompositeCheck | abi::kWcNoBestFitChars)) != 0 ||
        (mb_count != 0 && mb_str == nullptr) ||
        (used_default_char != nullptr &&
         !mapped_guest_range(used_default_char, sizeof(*used_default_char), true))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const bool null_terminated = wide_count == -1;
    std::size_t unit_count = 0;
    if (null_terminated) {
        while (wide_str[unit_count] != 0) {
            unit_count += 1;
        }
    } else {
        unit_count = static_cast<std::size_t>(wide_count);
    }
    if (!mapped_guest_range(wide_str, unit_count * sizeof(std::uint16_t), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("WideCharToMultiByte", "source", "memória convidada inválida");
        return 0;
    }
    const bool utf8 = code_page == abi::kCpUtf8;
    std::size_t index = 0;
    std::size_t needed = 0;
    while (index < unit_count) {
        const std::uint32_t codepoint = decode_utf16(wide_str, unit_count, index);
        if (utf8) {
            char bytes[4]{};
            needed += utf8_bytes_for(codepoint > 0x10FFFFU ? 0x3FU : codepoint, bytes);
        } else {
            needed += 1;
        }
    }
    if (null_terminated) {
        needed += 1;
    }
    if (mb_str == nullptr) {
        set_last_error(abi::kErrorSuccess);
        return static_cast<int>(needed);
    }
    if (mb_count < 0 || static_cast<std::size_t>(mb_count) < needed ||
        !mapped_guest_range(mb_str, needed, true)) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    const char fallback = default_char != nullptr ? *default_char : '?';
    bool used_default = false;
    index = 0;
    std::size_t written = 0;
    while (index < unit_count) {
        std::uint32_t codepoint = decode_utf16(wide_str, unit_count, index);
        if (codepoint > 0x10FFFFU) {
            codepoint = 0x3FU;
        }
        if (utf8) {
            char bytes[4]{};
            const std::size_t count = utf8_bytes_for(codepoint, bytes);
            for (std::size_t byte_index = 0; byte_index < count; ++byte_index) {
                mb_str[written] = bytes[byte_index];
                written += 1;
            }
        } else {
            std::uint8_t byte = 0;
            if (unicode_to_cp1252(codepoint, byte)) {
                mb_str[written] = static_cast<char>(byte);
            } else {
                mb_str[written] = fallback;
                used_default = true;
            }
            written += 1;
        }
    }
    if (null_terminated) {
        mb_str[written] = '\0';
        written += 1;
    }
    if (used_default_char != nullptr) {
        *used_default_char = used_default ? 1 : 0;
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(written);
}

// ---------------------------------------------------------------------------
// GetModuleHandle / GetProcAddress (Fase 9)
// ---------------------------------------------------------------------------

// Módulos conhecidos internamente. O handle é o endereço do próprio
// registrations (token opaco); GetProcAddress consulta o registro interno.
struct KnownModule {
    const char* name;
    std::uintptr_t handle;
};

std::vector<KnownModule>& known_modules() {
    static std::vector<KnownModule> instance;
    return instance;
}

void ensure_known_modules() {
    if (!known_modules().empty()) {
        return;
    }
    known_modules().push_back({"kernel32.dll", 0x1000});
    known_modules().push_back({"user32.dll", 0x2000});
    known_modules().push_back({"gdi32.dll", 0x3000});
    known_modules().push_back({"msvcrt.dll", 0x4000});
}

void* tl_GetModuleHandleA(const char* module_name) noexcept {
    ensure_known_modules();
    if (module_name == nullptr) {
        return reinterpret_cast<void*>(0x1000);
    }
    std::string name_lower(module_name);
    for (auto& c : name_lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const auto& mod : known_modules()) {
        if (name_lower == mod.name) {
            return reinterpret_cast<void*>(mod.handle);
        }
    }
    set_last_error(abi::kErrorFileNotFound);
    return nullptr;
}

void* tl_GetModuleHandleW(const std::uint16_t* module_name) noexcept {
    if (module_name == nullptr) {
        return tl_GetModuleHandleA(nullptr);
    }
    std::string narrow;
    while (*module_name != 0) {
        narrow.push_back(static_cast<char>(*module_name & 0x7F));
        ++module_name;
    }
    return tl_GetModuleHandleA(narrow.c_str());
}

void* tl_GetProcAddress(void* module, const char* name) noexcept {
    (void)module;
    (void)name;
    set_last_error(abi::kErrorFileNotFound);
    return nullptr;
}

// ---------------------------------------------------------------------------
// GetCommandLine / GetEnvironmentVariable (Fase 9)
// ---------------------------------------------------------------------------

static std::string g_command_line_string;

const char* tl_GetCommandLineA() noexcept {
    if (g_command_line_string.empty()) {
        const auto& args = msvcrt_get_guest_arguments();
        if (args.empty()) {
            g_command_line_string = "\"\"";
        } else {
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (i > 0) {
                    g_command_line_string.push_back(' ');
                }
                g_command_line_string.push_back('"');
                g_command_line_string += args[i];
                g_command_line_string.push_back('"');
            }
        }
    }
    return g_command_line_string.c_str();
}

const std::uint16_t* tl_GetCommandLineW() noexcept {
    static std::vector<std::uint16_t> wide_cmdline;
    const char* narrow = tl_GetCommandLineA();
    wide_cmdline.clear();
    while (*narrow != '\0') {
        wide_cmdline.push_back(static_cast<std::uint16_t>(static_cast<unsigned char>(*narrow)));
        ++narrow;
    }
    wide_cmdline.push_back(0);
    return wide_cmdline.data();
}

extern char** environ;

std::uint32_t tl_GetEnvironmentVariableA(const char* name, char* buffer,
                                          std::uint32_t size) noexcept {
    if (name == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const char* value = ::getenv(name);
    if (value == nullptr) {
        set_last_error(abi::kErrorFileNotFound);
        return 0;
    }
    const std::size_t len = std::strlen(value);
    if (buffer == nullptr || size == 0) {
        return static_cast<std::uint32_t>(len);
    }
    if (size <= len) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return static_cast<std::uint32_t>(len);
    }
    std::memcpy(buffer, value, len);
    buffer[len] = '\0';
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(len);
}

std::uint32_t tl_GetEnvironmentVariableW(const std::uint16_t* name, std::uint16_t* buffer,
                                          std::uint32_t size) noexcept {
    if (name == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string narrow_name;
    while (*name != 0) {
        narrow_name.push_back(static_cast<char>(*name & 0x7F));
        ++name;
    }
    const std::uint32_t result = tl_GetEnvironmentVariableA(narrow_name.c_str(), nullptr, 0);
    if (result == 0) {
        return 0;
    }
    if (buffer == nullptr || size == 0) {
        return result;
    }
    if (size <= result) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return result;
    }
    char narrow_buf[1024]{};
    tl_GetEnvironmentVariableA(narrow_name.c_str(), narrow_buf, sizeof(narrow_buf));
    for (std::uint32_t i = 0; i <= result; ++i) {
        buffer[i] = static_cast<std::uint16_t>(static_cast<unsigned char>(narrow_buf[i]));
    }
    set_last_error(abi::kErrorSuccess);
    return result;
}

// ---------------------------------------------------------------------------
// Heap (Fase 9) — wrapper sobre malloc/free do hospedeiro.
// ---------------------------------------------------------------------------

void* tl_GetProcessHeap() noexcept {
    static char g_process_heap_token = 0;
    return &g_process_heap_token;
}

void* tl_HeapAlloc(void* heap, std::uint32_t flags, std::uintptr_t size) noexcept {
    (void)heap;
    if ((flags & 0x0008) != 0) {
        return std::calloc(1, size);
    }
    return std::malloc(size);
}

int tl_HeapFree(void* heap, std::uint32_t flags, void* memory) noexcept {
    (void)heap;
    (void)flags;
    std::free(memory);
    return 1;
}

void* tl_HeapReAlloc(void* heap, std::uint32_t flags, void* memory,
                      std::uintptr_t new_size) noexcept {
    (void)heap;
    (void)flags;
    return std::realloc(memory, new_size);
}

// ---------------------------------------------------------------------------
// Tempo (Fase 9)
// ---------------------------------------------------------------------------

std::uint64_t tl_GetTickCount64() noexcept {
    using namespace std::chrono;
    const auto now = steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(now).count());
}

void tl_GetSystemTimeAsFileTime(void* file_time) noexcept {
    auto* ft = static_cast<std::uint64_t*>(file_time);
    using namespace std::chrono;
    const auto now = system_clock::now().time_since_epoch();
    const auto since_epoch = duration_cast<nanoseconds>(now).count();
    const std::uint64_t ticks_100ns = static_cast<std::uint64_t>(since_epoch) / 100;
    const std::uint64_t epoch_diff = 116444736000000000ULL;
    *ft = ticks_100ns + epoch_diff;
}

// --- Fase 10: Sistema de arquivos e utilitários ---

TL_MSABI std::uint32_t tl_GetFileSize(const void* handle, std::uint32_t* high_size) noexcept {
    const FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0xFFFFFFFFU;
    }
    if (high_size != nullptr && !mapped_guest_range(high_size, sizeof(*high_size), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0xFFFFFFFFU;
    }
    if (high_size != nullptr) {
        *high_size = static_cast<std::uint32_t>(slot->file_size >> 32);
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(slot->file_size & 0xFFFFFFFFU);
}

// Métodos de SeekFilePointer.
constexpr std::uint32_t kFileBegin = 0;
constexpr std::uint32_t kFileCurrent = 1;
constexpr std::uint32_t kFileEnd = 2;

TL_MSABI std::int32_t tl_SetFilePointer(const void* handle, std::int32_t distance,
                                         std::int32_t* high_distance,
                                         std::uint32_t move_method) noexcept {
    FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return -1;
    }
    if (move_method > kFileEnd) {
        set_last_error(abi::kErrorInvalidParameter);
        return -1;
    }
    if (high_distance != nullptr && !mapped_guest_range(high_distance, sizeof(*high_distance), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return -1;
    }

    std::int64_t offset = distance;
    if (high_distance != nullptr) {
        offset |= static_cast<std::int64_t>(*high_distance) << 32;
    }

    std::int64_t new_pos = 0;
    switch (move_method) {
        case kFileBegin:
            new_pos = offset;
            break;
        case kFileCurrent:
            new_pos = slot->position + offset;
            break;
        case kFileEnd:
            new_pos = static_cast<std::int64_t>(slot->file_size) + offset;
            break;
    }
    if (new_pos < 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return -1;
    }
    const off_t result = lseek(slot->fd, static_cast<off_t>(new_pos), SEEK_SET);
    if (result < 0) {
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        return -1;
    }
    slot->position = static_cast<std::int64_t>(result);
    if (high_distance != nullptr) {
        *high_distance = static_cast<std::int32_t>(slot->position >> 32);
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::int32_t>(slot->position & 0xFFFFFFFFU);
}

TL_MSABI std::uint32_t tl_GetFileAttributesA(const char* path) noexcept {
    if (!mapped_guest_cstring(path) || path == nullptr || path[0] == '\0') {
        set_last_error(abi::kErrorInvalidParameter);
        return 0xFFFFFFFF;
    }
    char normalized[4096]{};
    if (!translate_windows_path(path, normalized, sizeof(normalized))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0xFFFFFFFF;
    }
    struct stat st{};
    if (stat(normalized, &st) != 0) {
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        return 0xFFFFFFFF;
    }
    set_last_error(abi::kErrorSuccess);
    return stat_to_win32_attributes(normalized, st);
}

TL_MSABI int tl_DeleteFileA(const char* path) noexcept {
    if (!mapped_guest_cstring(path) || path == nullptr || path[0] == '\0') {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char normalized[4096]{};
    if (!translate_windows_path(path, normalized, sizeof(normalized))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (unlink(normalized) != 0) {
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_MoveFileA(const char* from, const char* to) noexcept {
    if (!mapped_guest_cstring(from) || from == nullptr || from[0] == '\0' ||
        !mapped_guest_cstring(to) || to == nullptr || to[0] == '\0') {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char from_norm[4096]{};
    char to_norm[4096]{};
    if (!translate_windows_path(from, from_norm, sizeof(from_norm)) ||
        !translate_windows_path(to, to_norm, sizeof(to_norm))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (rename(from_norm, to_norm) != 0) {
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_CreateDirectoryA(const char* path, const void* /*security_attributes*/) noexcept {
    if (!mapped_guest_cstring(path) || path == nullptr || path[0] == '\0') {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char normalized[4096]{};
    if (!translate_windows_path(path, normalized, sizeof(normalized))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (mkdir(normalized, 0777) != 0) {
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_FindFirstFileA(const char* path, void* find_data) noexcept {
    if (!mapped_guest_cstring(path) || path == nullptr || path[0] == '\0' ||
        find_data == nullptr || !mapped_guest_range(find_data, sizeof(Win32FindDataA), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    }
    char normalized[4096]{};
    if (!translate_windows_path(path, normalized, sizeof(normalized))) {
        set_last_error(abi::kErrorInvalidParameter);
        return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    }
    // Separar diretório do padrão. Se não há '/', o padrão é ".".
    std::string dir_path;
    std::string pattern;
    const char* last_slash = strrchr(normalized, '/');
    if (last_slash != nullptr) {
        dir_path.assign(normalized, static_cast<std::size_t>(last_slash - normalized));
        pattern = last_slash + 1;
    } else {
        dir_path = ".";
        pattern = normalized;
    }
    DIR* dir = opendir(dir_path.c_str());
    if (dir == nullptr) {
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    }
    // Encontrar um slot livre.
    FindSlot* slot = nullptr;
    for (auto& s : g_find_slots) {
        if (!s.used) {
            slot = &s;
            break;
        }
    }
    if (slot == nullptr) {
        closedir(dir);
        set_last_error(abi::kErrorNotEnoughMemory);
        return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    }
    // Procurar a primeira entrada que combine com o padrão.
    struct dirent* entry = nullptr;
    auto* data = static_cast<Win32FindDataA*>(find_data);
    // Limpar a estrutura.
    *data = {};
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
            continue;
        }
        // Correspondência simples: se o padrão contém '*', aceitar qualquer
        // coisa antes do '*'; caso contrário, correspondência exata.
        bool matches = false;
        if (pattern == "*") {
            matches = true;
        } else if (pattern.find('*') != std::string::npos) {
            const auto star_pos = pattern.find('*');
            const std::string prefix = pattern.substr(0, star_pos);
            matches = std::string_view(entry->d_name).substr(0, prefix.size()) == prefix;
        } else {
            matches = (pattern == entry->d_name);
        }
        if (!matches) {
            continue;
        }
        // Preencher WIN32_FIND_DATAA.
        std::string full_path = dir_path + "/" + entry->d_name;
        struct stat st{};
        if (stat(full_path.c_str(), &st) == 0) {
            data->dw_file_attributes = stat_to_win32_attributes(full_path.c_str(), st);
            data->n_file_size_low = static_cast<std::uint32_t>(st.st_size & 0xFFFFFFFF);
            data->n_file_size_high = static_cast<std::uint32_t>(st.st_size >> 32);
        } else {
            data->dw_file_attributes = kFileAttributeNormal;
        }
        const std::size_t name_len = std::strlen(entry->d_name);
        if (name_len < sizeof(data->c_file_name)) {
            std::memcpy(data->c_file_name, entry->d_name, name_len + 1);
        }
        slot->used = true;
        slot->dir = dir;
        slot->pattern = pattern;
        slot->directory = dir_path;
        set_last_error(abi::kErrorSuccess);
        const auto handle_val = kFindHandleBase +
                               static_cast<std::uintptr_t>(slot - g_find_slots.data());
        return reinterpret_cast<void*>(handle_val);
    }
    // Nenhuma entrada encontrada.
    closedir(dir);
    set_last_error(abi::kErrorFileNotFound);
    return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
}

TL_MSABI int tl_FindNextFileA(const void* handle, void* find_data) noexcept {
    if (find_data == nullptr || !mapped_guest_range(find_data, sizeof(Win32FindDataA), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    FindSlot* slot = find_slot_for_handle(handle);
    if (slot == nullptr || !slot->used || slot->dir == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    auto* data = static_cast<Win32FindDataA*>(find_data);
    struct dirent* entry = nullptr;
    while ((entry = readdir(slot->dir)) != nullptr) {
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
            continue;
        }
        bool matches = false;
        if (slot->pattern == "*") {
            matches = true;
        } else if (slot->pattern.find('*') != std::string::npos) {
            const auto star_pos = slot->pattern.find('*');
            const std::string prefix = slot->pattern.substr(0, star_pos);
            matches = std::string_view(entry->d_name).substr(0, prefix.size()) == prefix;
        } else {
            matches = (slot->pattern == entry->d_name);
        }
        if (!matches) {
            continue;
        }
        *data = {};
        std::string full_path = slot->directory + "/" + entry->d_name;
        struct stat st{};
        if (stat(full_path.c_str(), &st) == 0) {
            data->dw_file_attributes = stat_to_win32_attributes(full_path.c_str(), st);
            data->n_file_size_low = static_cast<std::uint32_t>(st.st_size & 0xFFFFFFFF);
            data->n_file_size_high = static_cast<std::uint32_t>(st.st_size >> 32);
        } else {
            data->dw_file_attributes = kFileAttributeNormal;
        }
        const std::size_t name_len = std::strlen(entry->d_name);
        if (name_len < sizeof(data->c_file_name)) {
            std::memcpy(data->c_file_name, entry->d_name, name_len + 1);
        }
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    set_last_error(abi::kErrorFileNotFound);
    return 0;
}

TL_MSABI int tl_FindClose(const void* handle) noexcept {
    FindSlot* slot = find_slot_for_handle(handle);
    if (slot == nullptr || !slot->used) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (slot->dir != nullptr) {
        closedir(slot->dir);
        slot->dir = nullptr;
    }
    slot->used = false;
    slot->pattern.clear();
    slot->directory.clear();
    set_last_error(abi::kErrorSuccess);
    return 1;
}

// --- Diretório atual e módulo ---

TL_MSABI std::uint32_t tl_GetCurrentDirectoryA(const std::uint32_t buffer_length,
                                                char* buffer) noexcept {
    if (buffer_length == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (buffer != nullptr && !mapped_guest_range(buffer, buffer_length, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char cwd[4096]{};
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        return 0;
    }
    // Traduzir para caminho relativo sem barra inicial (estilo Windows CWD).
    const char* path = cwd;
    if (path[0] == '/') {
        ++path;
    }
    const std::size_t len = std::strlen(path);
    if (len == 0) {
        // Raiz: retornar "\"
        if (buffer != nullptr && buffer_length >= 2) {
            buffer[0] = '\\';
            buffer[1] = '\0';
        }
        set_last_error(abi::kErrorSuccess);
        return 2;
    }
    if (buffer == nullptr || len + 1 > buffer_length) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return static_cast<std::uint32_t>(len + 1);
    }
    // Copiar e converter / para \.
    for (std::size_t i = 0; i < len; ++i) {
        buffer[i] = path[i] == '/' ? '\\' : path[i];
    }
    buffer[len] = '\0';
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(len + 1);
}

TL_MSABI std::uint32_t tl_GetCurrentDirectoryW(const std::uint32_t buffer_length,
                                                std::uint16_t* buffer) noexcept {
    // Delega à versão A e converte para UTF-16.
    char narrow[4096]{};
    const std::uint32_t needed = tl_GetCurrentDirectoryA(sizeof(narrow), narrow);
    if (needed == 0) {
        return 0;
    }
    if (buffer == nullptr || buffer_length < needed) {
        if (buffer != nullptr && buffer_length > 0) {
            buffer[0] = L'\0';
        }
        set_last_error(abi::kErrorInsufficientBuffer);
        return needed;
    }
    // Converter ASCII para UTF-16 (cada byte vira um wchar_t).
    for (std::uint32_t i = 0; i < needed; ++i) {
        buffer[i] = static_cast<std::uint16_t>(narrow[i]);
    }
    set_last_error(abi::kErrorSuccess);
    return needed;
}

TL_MSABI std::uint32_t tl_GetModuleFileNameA(const void* /*module*/, char* buffer,
                                               std::uint32_t size) noexcept {
    if (buffer != nullptr && size > 0 && !mapped_guest_range(buffer, size, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (g_module_file_name.empty()) {
        if (buffer != nullptr && size > 0) {
            buffer[0] = '\0';
        }
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::size_t len = g_module_file_name.size();
    if (buffer == nullptr || len + 1 > size) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return static_cast<std::uint32_t>(len + 1);
    }
    std::memcpy(buffer, g_module_file_name.data(), len + 1);
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(len + 1);
}

// --- Fase 11: Concorrência ---

TL_MSABI std::uint32_t tl_GetCurrentThreadId() noexcept {
    return g_current_thread_id;
}

TL_MSABI std::uint32_t tl_GetCurrentProcessId() noexcept {
    // No isolamento por fork(), o PID do filho é o "processo convidado".
    return static_cast<std::uint32_t>(::getpid());
}

// Aloca um índice TLS disponível. Retorna o índice (>= 0) ou 0xFFFFFFFF em caso
// de erro. O Windows retorna TLS_OUT_OF_INDEXES (0xFFFFFFFF) quando não há
// slots livres.
TL_MSABI std::uint32_t tl_TlsAlloc() noexcept {
    for (std::uint32_t i = 0; i < kMaxTlsSlots; ++i) {
        if (!g_tls_indices_used[i]) {
            g_tls_indices_used[i] = true;
            set_last_error(abi::kErrorSuccess);
            return i;
        }
    }
    set_last_error(abi::kErrorTooManyTlsIndexes);
    trace_guest_failure("TlsAlloc", "tls-index", "exaustão de slots TLS");
    return 0xFFFFFFFFU;
}

TL_MSABI int tl_TlsSetValue(const std::uint32_t tls_index, void* const tls_value) noexcept {
    if (tls_index >= kMaxTlsSlots || !g_tls_indices_used[tls_index]) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    g_guest_tls_slots[tls_index] = tls_value;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_TlsFree(const std::uint32_t tls_index) noexcept {
    if (tls_index >= kMaxTlsSlots || !g_tls_indices_used[tls_index]) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    g_tls_indices_used[tls_index] = false;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

// Estrutura de parâmetros repassada à thread hospedeira.
struct GuestThreadParams {
    std::uintptr_t entry_point{};
    std::uintptr_t stack_top{};
    std::uintptr_t stack_size{};
    std::uint32_t thread_id{};
    void* parameter{};
    ThreadSlot* slot{nullptr};
};

void guest_thread_wrapper(GuestThreadParams params) noexcept {
    g_current_thread_id = params.thread_id;

    // Aloca TEB para esta thread.
    void* const teb = allocate_guest_teb(params.stack_top, params.stack_size);
    if (teb == nullptr) {
        auto* const slot = params.slot;
        {
            const std::lock_guard<std::mutex> lock(slot->join_mutex);
            slot->exit_code = -1;
            slot->finished = true;
        }
        slot->finish_cv.notify_all();
        return;
    }
    auto* const fields = static_cast<GuestTeb*>(teb);
    fields->client_id[0] = params.thread_id;  // ProcessId
    fields->client_id[1] = params.thread_id;  // ThreadId

    if (!set_guest_gs_base(teb)) {
        free_guest_teb(teb);
        auto* const slot = params.slot;
        {
            const std::lock_guard<std::mutex> lock(slot->join_mutex);
            slot->exit_code = -1;
            slot->finished = true;
        }
        slot->finish_cv.notify_all();
        return;
    }

    // Prepara o contexto de saída para ExitThread.
    g_guest_execution_active = true;
    g_guest_exit_code = 0;

    if (setjmp(g_guest_exit_context) == 0) {
        // Chama a função convidada via trampoline.
        using ThreadEntry = TL_MSABI void (*)(void*);
        const auto entry = std::bit_cast<ThreadEntry>(params.entry_point);
        entry(params.parameter);
        // Retorno normal: a thread terminou sem chamar ExitThread.
        g_guest_execution_active = false;
    }
    // ExitThread chegou via longjmp OU retorno normal acima.
    g_guest_execution_active = false;
    static_cast<void>(set_guest_gs_base(nullptr));
    free_guest_teb(teb);

    auto* const slot = params.slot;
    {
        const std::lock_guard<std::mutex> lock(slot->join_mutex);
        slot->exit_code = static_cast<int>(g_guest_exit_code);
        slot->finished = true;
    }
    slot->finish_cv.notify_all();
}

TL_MSABI void* tl_CreateThread(
    const void* /*security_attributes*/,
    const std::uintptr_t stack_size,
    const std::uintptr_t start_address,
    void* const parameter,
    const std::uint32_t creation_flags,
    std::uint32_t* const thread_id_out) noexcept {

    if (start_address == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateThread", "start-address", "ponteiro nulo");
        return nullptr;
    }
    if (thread_id_out != nullptr && !mapped_guest_range(thread_id_out, sizeof(*thread_id_out), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateThread", "thread-id", "ponteiro sem permissão de escrita");
        return nullptr;
    }

    // Encontra um slot de thread livre.
    ThreadSlot* slot = nullptr;
    for (auto& s : g_threads) {
        if (!s.used) {
            slot = &s;
            break;
        }
    }
    if (slot == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        trace_guest_failure("CreateThread", "thread-slot", "exaustão de slots de thread");
        return nullptr;
    }

    const std::uint32_t tid = g_next_thread_id.fetch_add(1);
    const std::uintptr_t actual_stack_size =
        stack_size == 0 ? 0x100000U : stack_size;  // Default 1 MiB

    // Aloca a pilha convidada (mmap anônimo, protegido).
    constexpr std::size_t kGuardPageSize = 0x1000U;
    const std::uintptr_t total_size = actual_stack_size + kGuardPageSize;
    void* const stack_mem = mmap(nullptr, static_cast<std::size_t>(total_size),
                                  PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack_mem == MAP_FAILED) {
        set_last_error(abi::kErrorNotEnoughMemory);
        trace_guest_failure("CreateThread", "stack-alloc", "falha ao alocar pilha");
        return nullptr;
    }
    // Protege a página inferior como guard page.
    static_cast<void>(mprotect(stack_mem, kGuardPageSize, PROT_NONE));

    const std::uintptr_t stack_top =
        std::bit_cast<std::uintptr_t>(stack_mem) + total_size;

    slot->used = true;
    slot->thread_id = tid;
    slot->stack = static_cast<std::byte*>(stack_mem);
    slot->stack_top = stack_top;
    slot->finished = false;
    slot->joined = false;
    slot->exit_code = 0;

    GuestThreadParams params{};
    params.entry_point = start_address;
    params.stack_top = stack_top;
    params.stack_size = actual_stack_size;
    params.thread_id = tid;
    params.parameter = parameter;
    params.slot = slot;

    if (creation_flags & 0x00000001U) {  // CREATE_SUSPENDED
        // Por simplicidade, criamos a thread e a suspendemos imediatamente
        // usando uma variável de condição. Para o escopo atual (fixture de
        // teste), CREATE_SUSPENDED não é usado.
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateThread", "creation-flags",
                            "CREATE_SUSPENDED não suportado");
        // Limpa.
        static_cast<void>(mprotect(stack_mem, kGuardPageSize, PROT_READ | PROT_WRITE));
        static_cast<void>(munmap(stack_mem, static_cast<std::size_t>(total_size)));
        slot->used = false;
        return nullptr;
    }

    slot->host_thread = std::thread([params]() mutable { guest_thread_wrapper(params); });

    if (thread_id_out != nullptr) {
        *thread_id_out = tid;
    }
    set_last_error(abi::kErrorSuccess);

    void* const handle = thread_slot_to_handle(*slot);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "CreateThread"},
        diagnostics::TraceField{"thread-id", std::to_string(tid)},
        diagnostics::TraceField{"stack-size", std::to_string(actual_stack_size)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("CreateThread", fields, 4);
    return handle;
}

TL_MSABI void tl_ExitThread(const std::uint32_t exit_code) noexcept {
    g_guest_exit_code = exit_code;

    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "ExitThread"},
        diagnostics::TraceField{"thread-id", std::to_string(g_current_thread_id)},
        diagnostics::TraceField{"exit-code", std::to_string(exit_code)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("ExitThread", fields, 4);

    if (g_guest_execution_active) {
        std::longjmp(g_guest_exit_context, 1);
    }
    // Se não estamos no contexto de execução convidada, simplesmente retornamos.
    // A thread hospedeira terminará naturalmente.
}

TL_MSABI std::uint32_t tl_WaitForSingleObject(const void* handle,
                                               const std::uint32_t milliseconds) noexcept {
    if (handle == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return abi::kWaitFailed;
    }

    // Verifica se é um handle de thread.
    ThreadSlot* thread = find_thread_slot(handle);
    if (thread != nullptr) {
        if (!thread->used) {
            set_last_error(abi::kErrorInvalidHandle);
            return abi::kWaitFailed;
        }

        // Espera a thread terminar.
        {
            std::unique_lock<std::mutex> lock(thread->join_mutex);
            if (milliseconds == abi::kInfinite) {
                thread->finish_cv.wait(lock, [&]() { return thread->finished; });
            } else {
                const auto status = thread->finish_cv.wait_for(
                    lock, std::chrono::milliseconds(milliseconds),
                    [&]() { return thread->finished; });
                if (!status) {
                    set_last_error(abi::kErrorSuccess);
                    return abi::kWaitTimeout;
                }
            }
        }

        // Faz join na thread host se ainda não foi feito.
        if (!thread->joined && thread->host_thread.joinable()) {
            thread->host_thread.join();
            thread->joined = true;
        }

        set_last_error(abi::kErrorSuccess);
        const std::array<diagnostics::TraceField, 4> fields{
            diagnostics::TraceField{"symbol", "WaitForSingleObject"},
            diagnostics::TraceField{"thread-id", std::to_string(thread->thread_id)},
            diagnostics::TraceField{"result", "wait-completed"},
            diagnostics::TraceField{"status", "success"},
        };
        runtime_trace("WaitForSingleObject", fields, 4);
        return abi::kWaitObject0;
    }

    SyncSlot* sync = find_sync_slot(handle);
    if (sync != nullptr) {
        const std::uint32_t result = wait_sync_slot(*sync, milliseconds);
        set_last_error(result == abi::kWaitFailed ? abi::kErrorInvalidHandle : abi::kErrorSuccess);
        return result;
    }

    // Verifica se é um handle de arquivo (compatibilidade com código existente).
    FileSlot* file = find_file_slot(handle);
    if (file != nullptr) {
        // Arquivos são sempre "sinalizados" (operam de forma síncrona).
        set_last_error(abi::kErrorSuccess);
        return abi::kWaitObject0;
    }

    set_last_error(abi::kErrorInvalidHandle);
    trace_guest_failure("WaitForSingleObject", "handle-validation", "handle inválido");
    return abi::kWaitFailed;
}

}  // extern "C"

// Define o caminho do módulo convidado (chamado antes da execução).
void set_guest_module_path(const char* path) noexcept {
    g_module_file_name = path != nullptr ? path : "";
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

GuestExecutionResult execute_guest_entry(const std::uintptr_t entry_point, // NOLINT(bugprone-easily-swappable-parameters)
                                         const std::uintptr_t stack_top) noexcept {
    using EntryPoint = TL_MSABI void (*)();
    const auto entry = std::bit_cast<EntryPoint>(entry_point);
    if (entry == nullptr || stack_top == 0) {
        return {};
    }
    constexpr std::uintptr_t kGuestStackSize = 0x100000U;  // 1 MiB
    void* const teb = allocate_guest_teb(stack_top, kGuestStackSize);
    if (teb == nullptr) {
        return {};
    }
    const bool gs_configured = set_guest_gs_base(teb);
    if (!gs_configured) {
        free_guest_teb(teb);
        return {};
    }
    g_quit_requested = false;
    g_quit_code = 0;
    g_guest_execution_active = true;
    g_current_thread_id = kMainThreadId;
    if (setjmp(g_guest_exit_context) == 0) {
        tl_call_guest_on_stack(std::bit_cast<std::uintptr_t>(entry), stack_top);
        g_guest_execution_active = false;
        static_cast<void>(set_guest_gs_base(nullptr));
        free_guest_teb(teb);
        return {};
    }
    g_guest_execution_active = false;
    static_cast<void>(set_guest_gs_base(nullptr));
    free_guest_teb(teb);
    return {.exited_explicitly = true, .exit_code = g_guest_exit_code};
}

// ---------------------------------------------------------------------------
// Variantes wide do sistema de arquivos (Fase 10+).
// ---------------------------------------------------------------------------

// Valida uma string wide terminada em zero na memória convidada.
[[nodiscard]] bool mapped_guest_wstring(const std::uint16_t* value) noexcept {
    if (value == nullptr) {
        return true;
    }
    constexpr std::size_t kMaxGuestWideString = 65535;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(value);
    for (std::size_t index = 0; index < kMaxGuestWideString; ++index) {
        if (index > (std::numeric_limits<std::uintptr_t>::max() - address) / sizeof(std::uint16_t) ||
            !mapped_guest_range(reinterpret_cast<const void*>(address + index * sizeof(std::uint16_t)),
                                sizeof(std::uint16_t), false)) {
            return false;
        }
        if (value[index] == 0) {
            return true;
        }
    }
    return false;
}

// Converte uma string wide convidada para UTF-8 (host).
[[nodiscard]] std::string wide_to_utf8(const std::uint16_t* const wide) noexcept {
    std::string out;
    if (wide == nullptr) {
        return out;
    }
    std::size_t index = 0;
    while (wide[index] != 0) {
        const std::uint32_t codepoint = decode_utf16(wide, index + 2, index);
        if (codepoint == kInvalidCodepoint) {
            index += 1;
            continue;
        }
        char bytes[4]{};
        const std::size_t count = utf8_bytes_for(codepoint, bytes);
        for (std::size_t i = 0; i < count; ++i) {
            out.push_back(bytes[i]);
        }
    }
    return out;
}

// Converte uma string UTF-8 (host) para wide, retornando em `out` (unidades
// sem terminador; o chamador adiciona o 0 final se necessário).
[[nodiscard]] std::vector<std::uint16_t> utf8_to_wide(const std::string& text) noexcept {
    std::vector<std::uint16_t> out;
    out.reserve(text.size());
    std::size_t pos = 0;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
    while (pos < text.size()) {
        const std::uint32_t codepoint = decode_multibyte(abi::kCpUtf8, bytes, text.size(), pos);
        if (codepoint == kInvalidCodepoint) {
            out.push_back(0x3FU);
            continue;
        }
        std::uint16_t units[2]{};
        const std::size_t count = utf16_units_for(codepoint, units);
        for (std::size_t i = 0; i < count; ++i) {
            out.push_back(units[i]);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Recursos PE (somente leitura)
// ---------------------------------------------------------------------------

[[nodiscard]] bool resource_directory_range(const std::uint32_t relative,
                                            const std::size_t size) noexcept {
    const std::size_t image_size = g_guest_image_size;
    const std::size_t resource_base = g_guest_resource_rva;
    const std::size_t resource_size = g_guest_resource_size;
    return resource_base <= image_size && relative <= resource_size &&
           size <= resource_size - relative && relative <= image_size - resource_base &&
           size <= image_size - resource_base - relative;
}

[[nodiscard]] bool resource_image_range(const std::uint32_t rva,
                                        const std::size_t size) noexcept {
    return static_cast<std::size_t>(rva) <= g_guest_image_size &&
           size <= g_guest_image_size - static_cast<std::size_t>(rva);
}

[[nodiscard]] const std::byte* resource_directory_address(const std::uint32_t relative,
                                                           const std::size_t size) noexcept {
    if (!resource_directory_range(relative, size) || g_guest_image_base == nullptr) {
        return nullptr;
    }
    return g_guest_image_base + static_cast<std::size_t>(g_guest_resource_rva) + relative;
}

[[nodiscard]] bool read_resource_u16(const std::uint32_t relative,
                                     std::uint16_t& value) noexcept {
    const std::byte* address = resource_directory_address(relative, sizeof(value));
    if (address == nullptr) {
        return false;
    }
    std::memcpy(&value, address, sizeof(value));
    return true;
}

[[nodiscard]] bool read_resource_u32(const std::uint32_t relative,
                                     std::uint32_t& value) noexcept {
    const std::byte* address = resource_directory_address(relative, sizeof(value));
    if (address == nullptr) {
        return false;
    }
    std::memcpy(&value, address, sizeof(value));
    return true;
}

struct ResourceKey {
    bool named{false};
    std::uint16_t id{0};
    std::u16string name;
};

[[nodiscard]] bool guest_resource_key(const std::uint16_t* value,
                                      ResourceKey& key) noexcept {
    if (value == nullptr) {
        return false;
    }
    const std::uintptr_t raw = reinterpret_cast<std::uintptr_t>(value);
    if (raw <= 0xFFFFU) {
        key.named = false;
        key.id = static_cast<std::uint16_t>(raw);
        return true;
    }
    if (!mapped_guest_wstring(value)) {
        return false;
    }
    key.named = true;
    for (std::size_t index = 0; value[index] != 0; ++index) {
        key.name.push_back(static_cast<char16_t>(value[index]));
    }
    return true;
}

[[nodiscard]] bool resource_entry_key(const std::uint32_t raw_name,
                                      ResourceKey& key) noexcept {
    if ((raw_name & 0x80000000U) == 0) {
        key.named = false;
        key.id = static_cast<std::uint16_t>(raw_name & 0xFFFFU);
        return (raw_name & 0xFFFF0000U) == 0;
    }
    const std::uint32_t name_offset = raw_name & 0x7FFFFFFFU;
    std::uint16_t length = 0;
    if (name_offset > std::numeric_limits<std::uint32_t>::max() - 2U ||
        !read_resource_u16(name_offset, length) || length > 4096U ||
        !resource_directory_range(name_offset + 2U,
                                  static_cast<std::size_t>(length) * sizeof(std::uint16_t))) {
        return false;
    }
    key.named = true;
    key.name.clear();
    for (std::uint16_t index = 0; index < length; ++index) {
        std::uint16_t unit = 0;
        const std::uint32_t offset = name_offset + 2U +
                                     static_cast<std::uint32_t>(index) *
                                         static_cast<std::uint32_t>(sizeof(std::uint16_t));
        if (!read_resource_u16(offset, unit)) {
            return false;
        }
        key.name.push_back(static_cast<char16_t>(unit));
    }
    return true;
}

[[nodiscard]] bool resource_keys_equal(const ResourceKey& left,
                                       const ResourceKey& right) noexcept {
    return left.named == right.named &&
           (left.named ? left.name == right.name : left.id == right.id);
}

[[nodiscard]] bool find_resource_entry(const std::uint32_t directory_offset,
                                       const ResourceKey& wanted,
                                       std::uint32_t& child_raw) noexcept {
    std::uint16_t named_count = 0;
    std::uint16_t id_count = 0;
    if (directory_offset > std::numeric_limits<std::uint32_t>::max() - 16U ||
        !read_resource_u16(directory_offset + 12U, named_count) ||
        !read_resource_u16(directory_offset + 14U, id_count)) {
        return false;
    }
    const std::uint32_t entry_count = static_cast<std::uint32_t>(named_count) + id_count;
    if (entry_count > 4096U ||
        !resource_directory_range(directory_offset + 16U,
                                  static_cast<std::size_t>(entry_count) * 8U)) {
        return false;
    }
    for (std::uint32_t index = 0; index < entry_count; ++index) {
        const std::uint32_t entry_offset = directory_offset + 16U + index * 8U;
        std::uint32_t raw_name = 0;
        std::uint32_t raw_child = 0;
        if (!read_resource_u32(entry_offset, raw_name) ||
            !read_resource_u32(entry_offset + 4U, raw_child)) {
            return false;
        }
        ResourceKey actual;
        if (!resource_entry_key(raw_name, actual)) {
            return false;
        }
        if (resource_keys_equal(actual, wanted)) {
            child_raw = raw_child;
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool current_resource_module(const void* module) noexcept {
    if (module == nullptr) {
        return true;
    }
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(module);
    return value == 0x1000U ||
           (g_guest_image_base != nullptr &&
            value == reinterpret_cast<std::uintptr_t>(g_guest_image_base));
}

[[nodiscard]] std::optional<ResourceSlot> resolve_resource(const void* module,
                                                           const std::uint16_t* name,
                                                           const std::uint16_t* type) noexcept {
    if (!current_resource_module(module) || g_guest_image_base == nullptr ||
        g_guest_resource_size < 16U) {
        return std::nullopt;
    }
    ResourceKey name_key;
    ResourceKey type_key;
    if (!guest_resource_key(name, name_key) || !guest_resource_key(type, type_key)) {
        return std::nullopt;
    }
    std::uint32_t name_directory_raw = 0;
    std::uint32_t language_directory_raw = 0;
    if (!find_resource_entry(0, type_key, name_directory_raw) ||
        (name_directory_raw & 0x80000000U) == 0 ||
        !find_resource_entry(name_directory_raw & 0x7FFFFFFFU, name_key,
                             language_directory_raw) ||
        (language_directory_raw & 0x80000000U) == 0) {
        return std::nullopt;
    }

    const std::uint32_t language_offset = language_directory_raw & 0x7FFFFFFFU;
    std::uint16_t named_count = 0;
    std::uint16_t id_count = 0;
    if (!read_resource_u16(language_offset + 12U, named_count) ||
        !read_resource_u16(language_offset + 14U, id_count)) {
        return std::nullopt;
    }
    const std::uint32_t entry_count = static_cast<std::uint32_t>(named_count) + id_count;
    if (entry_count == 0 || entry_count > 4096U ||
        !resource_directory_range(language_offset + 16U,
                                  static_cast<std::size_t>(entry_count) * 8U)) {
        return std::nullopt;
    }
    std::uint32_t raw_data = 0;
    if (!read_resource_u32(language_offset + 20U, raw_data) ||
        (raw_data & 0x80000000U) != 0) {
        return std::nullopt;
    }
    const std::uint32_t data_offset = raw_data & 0x7FFFFFFFU;
    std::uint32_t data_rva = 0;
    std::uint32_t data_size = 0;
    if (!read_resource_u32(data_offset, data_rva) ||
        !read_resource_u32(data_offset + 4U, data_size) ||
        !resource_image_range(data_rva, data_size)) {
        return std::nullopt;
    }
    return ResourceSlot{.used = true, .data_rva = data_rva, .data_size = data_size};
}

struct GuestFileTime {
    std::uint32_t low{};
    std::uint32_t high{};
};
static_assert(sizeof(GuestFileTime) == 8);

struct GuestFileAttributeData {
    std::uint32_t attributes{};
    GuestFileTime creation{};
    GuestFileTime last_access{};
    GuestFileTime last_write{};
    std::uint32_t size_high{};
    std::uint32_t size_low{};
};
static_assert(sizeof(GuestFileAttributeData) == 36);

struct GuestByHandleFileInformation {
    std::uint32_t attributes{};
    GuestFileTime creation{};
    GuestFileTime last_access{};
    GuestFileTime last_write{};
    std::uint32_t volume_serial_number{};
    std::uint32_t size_high{};
    std::uint32_t size_low{};
    std::uint32_t number_of_links{};
    std::uint32_t file_index_high{};
    std::uint32_t file_index_low{};
};
static_assert(sizeof(GuestByHandleFileInformation) == 52);

struct GuestFileBasicInformation {
    std::int64_t creation_time{};
    std::int64_t last_access_time{};
    std::int64_t last_write_time{};
    std::int64_t change_time{};
    std::uint32_t attributes{};
    std::uint32_t reserved{};
};
static_assert(sizeof(GuestFileBasicInformation) == 40);

constexpr std::int64_t kWindowsEpochOffsetSeconds = 11644473600LL;
constexpr std::int64_t kWindowsEpochOffsetTicks = 116444736000000000LL;

[[nodiscard]] std::int64_t unix_time_to_filetime(const timespec& value) noexcept {
    return (static_cast<std::int64_t>(value.tv_sec) + kWindowsEpochOffsetSeconds) * 10000000LL +
           static_cast<std::int64_t>(value.tv_nsec) / 100LL;
}

[[nodiscard]] bool filetime_to_timespec(const GuestFileTime* value,
                                        timespec& result) noexcept {
    if (value == nullptr || !mapped_guest_range(value, sizeof(*value), false)) {
        return false;
    }
    const std::uint64_t raw = static_cast<std::uint64_t>(value->low) |
                              (static_cast<std::uint64_t>(value->high) << 32U);
    if (raw < static_cast<std::uint64_t>(kWindowsEpochOffsetTicks)) {
        return false;
    }
    const std::uint64_t unix_ticks = raw - static_cast<std::uint64_t>(kWindowsEpochOffsetTicks);
    result.tv_sec = static_cast<time_t>(unix_ticks / 10000000ULL);
    result.tv_nsec = static_cast<long>((unix_ticks % 10000000ULL) * 100ULL);
    return true;
}

void write_filetime(const timespec& source, GuestFileTime& target) noexcept {
    const std::int64_t ticks = unix_time_to_filetime(source);
    const auto raw = static_cast<std::uint64_t>(std::max<std::int64_t>(ticks, 0));
    target.low = static_cast<std::uint32_t>(raw & 0xFFFFFFFFU);
    target.high = static_cast<std::uint32_t>(raw >> 32U);
}

[[nodiscard]] bool wide_path_to_string(const std::uint16_t* path,
                                       std::string& result) noexcept {
    if (!mapped_guest_wstring(path) || path == nullptr || path[0] == 0) {
        return false;
    }
    result = wide_to_utf8(path);
    return !result.empty();
}

[[nodiscard]] bool normalized_wide_path(const std::uint16_t* path,
                                        char (&buffer)[4096]) noexcept {
    std::string utf8;
    return wide_path_to_string(path, utf8) &&
           translate_windows_path(utf8.c_str(), buffer, sizeof(buffer));
}

TL_MSABI void* tl_CreateFileW(const std::uint16_t* path, const std::uint32_t desired_access,
                              const std::uint32_t share_mode,
                              const void* security_attributes,
                              const std::uint32_t creation_disposition,
                              const std::uint32_t flags,
                              const void* template_file) noexcept {
    std::string utf8;
    if (!wide_path_to_string(path, utf8)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    return tl_CreateFileA(utf8.c_str(), desired_access, share_mode, security_attributes,
                          creation_disposition, flags, template_file);
}

TL_MSABI int tl_GetFileSizeEx(const void* handle, std::int64_t* size) noexcept {
    const FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (size == nullptr || !mapped_guest_range(size, sizeof(*size), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    *size = static_cast<std::int64_t>(slot->file_size);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetFilePointerEx(const void* handle, const std::int64_t distance,
                                 std::int64_t* new_position,
                                 const std::uint32_t move_method) noexcept {
    FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (move_method > kFileEnd ||
        (new_position != nullptr && !mapped_guest_range(new_position, sizeof(*new_position), true))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::int64_t base = 0;
    if (move_method == kFileCurrent) {
        base = slot->position;
    } else if (move_method == kFileEnd) {
        base = static_cast<std::int64_t>(slot->file_size);
    }
    if ((distance < 0 && base < std::numeric_limits<std::int64_t>::min() - distance) ||
        (distance > 0 && base > std::numeric_limits<std::int64_t>::max() - distance)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::int64_t target = base + distance;
    if (target < 0 || static_cast<std::uint64_t>(target) > std::numeric_limits<off_t>::max()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const off_t result = lseek(slot->fd, static_cast<off_t>(target), SEEK_SET);
    if (result < 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    slot->position = static_cast<std::int64_t>(result);
    if (new_position != nullptr) {
        *new_position = slot->position;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetEndOfFile(const void* handle) noexcept {
    FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (ftruncate(slot->fd, static_cast<off_t>(slot->position)) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    slot->file_size = static_cast<std::uint64_t>(slot->position);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_FlushFileBuffers(const void* handle) noexcept {
    const FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (fsync(slot->fd) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetFileAttributesExW(const std::uint16_t* path, const int info_level,
                                     void* data) noexcept {
    if (info_level != 0 || data == nullptr ||
        !mapped_guest_range(data, sizeof(GuestFileAttributeData), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char normalized[4096]{};
    if (!normalized_wide_path(path, normalized)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    struct stat st{};
    if (stat(normalized, &st) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    auto* result = static_cast<GuestFileAttributeData*>(data);
    *result = {};
    result->attributes = stat_to_win32_attributes(normalized, st);
    write_filetime(st.st_ctim, result->creation);
    write_filetime(st.st_atim, result->last_access);
    write_filetime(st.st_mtim, result->last_write);
    result->size_low = static_cast<std::uint32_t>(st.st_size);
    result->size_high = static_cast<std::uint32_t>(static_cast<std::uint64_t>(st.st_size) >> 32U);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_DeleteFileW(const std::uint16_t* path) noexcept {
    std::string utf8;
    if (!wide_path_to_string(path, utf8)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    return tl_DeleteFileA(utf8.c_str());
}

TL_MSABI int tl_MoveFileW(const std::uint16_t* from, const std::uint16_t* to) noexcept {
    std::string from_utf8;
    std::string to_utf8;
    if (!wide_path_to_string(from, from_utf8) || !wide_path_to_string(to, to_utf8)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    return tl_MoveFileA(from_utf8.c_str(), to_utf8.c_str());
}

TL_MSABI int tl_MoveFileExW(const std::uint16_t* from, const std::uint16_t* to,
                            const std::uint32_t flags) noexcept {
    constexpr std::uint32_t kMoveFileReplaceExisting = 1U;
    if ((flags & ~kMoveFileReplaceExisting) != 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string from_utf8;
    std::string to_utf8;
    if (!wide_path_to_string(from, from_utf8) || !wide_path_to_string(to, to_utf8)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char from_path[4096]{};
    char to_path[4096]{};
    if (!translate_windows_path(from_utf8.c_str(), from_path, sizeof(from_path)) ||
        !translate_windows_path(to_utf8.c_str(), to_path, sizeof(to_path))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if ((flags & kMoveFileReplaceExisting) == 0 && access(to_path, F_OK) == 0) {
        set_last_error(abi::kErrorAlreadyExists);
        return 0;
    }
    if (rename(from_path, to_path) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_CopyFileW(const std::uint16_t* from, const std::uint16_t* to,
                          const int fail_if_exists) noexcept {
    std::string from_utf8;
    std::string to_utf8;
    char from_path[4096]{};
    char to_path[4096]{};
    if (!wide_path_to_string(from, from_utf8) || !wide_path_to_string(to, to_utf8) ||
        !translate_windows_path(from_utf8.c_str(), from_path, sizeof(from_path)) ||
        !translate_windows_path(to_utf8.c_str(), to_path, sizeof(to_path))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const int source = open(from_path, O_RDONLY);
    if (source < 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    const int flags = O_WRONLY | O_CREAT | (fail_if_exists != 0 ? O_EXCL : O_TRUNC);
    const int target = open(to_path, flags, 0666);
    if (target < 0) {
        const int error = errno;
        close(source);
        set_last_error(errno_to_win32(error));
        return 0;
    }
    std::array<std::byte, 64 * 1024> buffer{};
    bool success = true;
    while (true) {
        const ssize_t count = read(source, buffer.data(), buffer.size());
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            success = false;
            break;
        }
        ssize_t written = 0;
        while (written < count) {
            const ssize_t result = write(target, buffer.data() + written,
                                         static_cast<std::size_t>(count - written));
            if (result < 0 && errno == EINTR) {
                continue;
            }
            if (result <= 0) {
                success = false;
                break;
            }
            written += result;
        }
        if (!success) {
            break;
        }
    }
    const int saved_error = errno;
    close(source);
    close(target);
    if (!success) {
        unlink(to_path);
        set_last_error(errno_to_win32(saved_error));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_CreateDirectoryW(const std::uint16_t* path,
                                 const void* security_attributes) noexcept {
    std::string utf8;
    if (!wide_path_to_string(path, utf8)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    return tl_CreateDirectoryA(utf8.c_str(), security_attributes);
}

TL_MSABI int tl_RemoveDirectoryW(const std::uint16_t* path) noexcept {
    char normalized[4096]{};
    if (!normalized_wide_path(path, normalized)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (rmdir(normalized) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetTempPathW(const std::uint32_t buffer_length,
                                       std::uint16_t* buffer) noexcept {
    constexpr char kTempPath[] = ".\\";
    const std::string path{kTempPath};
    const std::vector<std::uint16_t> wide = utf8_to_wide(path);
    const std::size_t required = wide.size() + 1U;
    if (buffer == nullptr || buffer_length < required) {
        if (buffer != nullptr && buffer_length > 0 &&
            mapped_guest_range(buffer, static_cast<std::size_t>(buffer_length) * sizeof(*buffer), true)) {
            buffer[0] = 0;
        }
        set_last_error(abi::kErrorInsufficientBuffer);
        return static_cast<std::uint32_t>(required);
    }
    if (!mapped_guest_range(buffer, static_cast<std::size_t>(buffer_length) * sizeof(*buffer), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::copy(wide.begin(), wide.end(), buffer);
    buffer[wide.size()] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(wide.size());
}

TL_MSABI std::uint32_t tl_GetFullPathNameW(const std::uint16_t* path,
                                           const std::uint32_t buffer_length,
                                           std::uint16_t* buffer,
                                           std::uint16_t** file_part) noexcept {
    std::string utf8;
    char normalized[4096]{};
    if (!wide_path_to_string(path, utf8) ||
        !translate_windows_path(utf8.c_str(), normalized, sizeof(normalized))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char cwd[4096]{};
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    std::string full = cwd;
    full.push_back('/');
    full += normalized;
    for (char& value : full) {
        if (value == '/') {
            value = '\\';
        }
    }
    const std::vector<std::uint16_t> wide = utf8_to_wide(full);
    const std::size_t required = wide.size() + 1U;
    if (file_part != nullptr && !mapped_guest_range(file_part, sizeof(*file_part), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (buffer == nullptr || buffer_length < required) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return static_cast<std::uint32_t>(wide.size());
    }
    if (!mapped_guest_range(buffer, static_cast<std::size_t>(buffer_length) * sizeof(*buffer), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::copy(wide.begin(), wide.end(), buffer);
    buffer[wide.size()] = 0;
    if (file_part != nullptr) {
        const std::size_t slash = full.find_last_of('\\');
        *file_part = slash == std::string::npos ? buffer : buffer + slash + 1U;
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(wide.size());
}

TL_MSABI int tl_GetFileTime(const void* handle, void* creation_time,
                            void* access_time, void* write_time) noexcept {
    const FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if ((creation_time != nullptr && !mapped_guest_range(creation_time, sizeof(GuestFileTime), true)) ||
        (access_time != nullptr && !mapped_guest_range(access_time, sizeof(GuestFileTime), true)) ||
        (write_time != nullptr && !mapped_guest_range(write_time, sizeof(GuestFileTime), true))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    struct stat st{};
    if (fstat(slot->fd, &st) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    if (creation_time != nullptr) {
        write_filetime(st.st_ctim, *static_cast<GuestFileTime*>(creation_time));
    }
    if (access_time != nullptr) {
        write_filetime(st.st_atim, *static_cast<GuestFileTime*>(access_time));
    }
    if (write_time != nullptr) {
        write_filetime(st.st_mtim, *static_cast<GuestFileTime*>(write_time));
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetFileTime(const void* handle, const void* creation_time,
                            const void* access_time, const void* write_time) noexcept {
    FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    timespec access{};
    timespec write{};
    if ((access_time != nullptr && !filetime_to_timespec(static_cast<const GuestFileTime*>(access_time), access)) ||
        (write_time != nullptr && !filetime_to_timespec(static_cast<const GuestFileTime*>(write_time), write)) ||
        (creation_time != nullptr && !mapped_guest_range(creation_time, sizeof(GuestFileTime), false))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    struct stat current{};
    if (fstat(slot->fd, &current) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    timespec times[2]{current.st_atim, current.st_mtim};
    if (access_time != nullptr) {
        times[0] = access;
    }
    if (write_time != nullptr) {
        times[1] = write;
    }
    if (futimens(slot->fd, times) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetFileInformationByHandle(const void* handle, void* information) noexcept {
    const FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (information == nullptr ||
        !mapped_guest_range(information, sizeof(GuestByHandleFileInformation), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    struct stat st{};
    if (fstat(slot->fd, &st) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    auto* result = static_cast<GuestByHandleFileInformation*>(information);
    *result = {};
    result->attributes = stat_to_win32_attributes(slot->path.c_str(), st);
    write_filetime(st.st_ctim, result->creation);
    write_filetime(st.st_atim, result->last_access);
    write_filetime(st.st_mtim, result->last_write);
    result->size_low = static_cast<std::uint32_t>(st.st_size);
    result->volume_serial_number = static_cast<std::uint32_t>(st.st_dev);
    result->size_high = static_cast<std::uint32_t>(static_cast<std::uint64_t>(st.st_size) >> 32U);
    result->number_of_links = static_cast<std::uint32_t>(st.st_nlink);
    result->file_index_low = static_cast<std::uint32_t>(st.st_ino);
    result->file_index_high = static_cast<std::uint32_t>(static_cast<std::uint64_t>(st.st_ino) >> 32U);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetFileInformationByHandleEx(const void* handle, const int info_class,
                                             void* buffer, const std::uint32_t size) noexcept {
    if (info_class != 0 || buffer == nullptr || size < sizeof(GuestFileBasicInformation) ||
        !mapped_guest_range(buffer, sizeof(GuestFileBasicInformation), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    struct stat st{};
    if (fstat(slot->fd, &st) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    auto* result = static_cast<GuestFileBasicInformation*>(buffer);
    *result = {.creation_time = unix_time_to_filetime(st.st_ctim),
               .last_access_time = unix_time_to_filetime(st.st_atim),
               .last_write_time = unix_time_to_filetime(st.st_mtim),
               .change_time = unix_time_to_filetime(st.st_ctim),
               .attributes = stat_to_win32_attributes(slot->path.c_str(), st),
               .reserved = 0};
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetFinalPathNameByHandleW(const void* handle,
                                                    std::uint16_t* buffer,
                                                    const std::uint32_t buffer_length,
                                                    const std::uint32_t flags) noexcept {
    if (flags != 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    const std::vector<std::uint16_t> wide = utf8_to_wide(slot->path);
    if (buffer == nullptr || buffer_length <= wide.size()) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return static_cast<std::uint32_t>(wide.size() + 1U);
    }
    if (!mapped_guest_range(buffer, static_cast<std::size_t>(buffer_length) * sizeof(*buffer), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::copy(wide.begin(), wide.end(), buffer);
    buffer[wide.size()] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(wide.size());
}

TL_MSABI void* tl_FindResourceW(const void* module, const std::uint16_t* name,
                                const std::uint16_t* type) noexcept {
    const std::optional<ResourceSlot> resolved = resolve_resource(module, name, type);
    if (!resolved.has_value()) {
        set_last_error(abi::kErrorResourceDataNotFound);
        return nullptr;
    }
    auto free_it = std::find_if(g_resources.begin(), g_resources.end(),
                                [](const ResourceSlot& slot) { return !slot.used; });
    if (free_it == g_resources.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    *free_it = *resolved;
    set_last_error(abi::kErrorSuccess);
    return resource_slot_handle(*free_it);
}

TL_MSABI void* tl_LoadResource(const void* module, const void* resource) noexcept {
    if (!current_resource_module(module) || find_resource_slot(resource) == nullptr) {
        set_last_error(abi::kErrorResourceDataNotFound);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return const_cast<void*>(resource);
}

TL_MSABI void* tl_LockResource(const void* resource) noexcept {
    const ResourceSlot* slot = find_resource_slot(resource);
    if (slot == nullptr || g_guest_image_base == nullptr ||
        !resource_image_range(slot->data_rva, slot->data_size)) {
        set_last_error(abi::kErrorResourceDataNotFound);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return const_cast<std::byte*>(g_guest_image_base) + slot->data_rva;
}

TL_MSABI std::uint32_t tl_SizeofResource(const void* module, const void* resource) noexcept {
    if (!current_resource_module(module)) {
        set_last_error(abi::kErrorResourceDataNotFound);
        return 0;
    }
    const ResourceSlot* slot = find_resource_slot(resource);
    if (slot == nullptr) {
        set_last_error(abi::kErrorResourceDataNotFound);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return slot->data_size;
}

// WIN32_FIND_DATAW: mesmo layout da versão A, com nomes wide (592 bytes;
// cFileName em 44).
struct Win32FindDataW {
    std::uint32_t dw_file_attributes{0};
    std::uint32_t ft_creation_time_lo{0};
    std::uint32_t ft_creation_time_hi{0};
    std::uint32_t ft_last_access_time_lo{0};
    std::uint32_t ft_last_access_time_hi{0};
    std::uint32_t ft_last_write_time_lo{0};
    std::uint32_t ft_last_write_time_hi{0};
    std::uint32_t n_file_size_high{0};
    std::uint32_t n_file_size_low{0};
    std::uint32_t dw_reserved0{0};
    std::uint32_t dw_reserved1{0};
    std::uint16_t c_file_name[260]{};
    std::uint16_t c_alternate_file_name[14]{};
};
static_assert(sizeof(Win32FindDataW) == 592);
static_assert(offsetof(Win32FindDataW, c_file_name) == 44);

TL_MSABI std::uint32_t tl_GetFileAttributesW(const std::uint16_t* path) noexcept {
    if (!mapped_guest_wstring(path) || path == nullptr || path[0] == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0xFFFFFFFF;
    }
    const std::string utf8 = wide_to_utf8(path);
    if (utf8.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0xFFFFFFFF;
    }
    char normalized[4096]{};
    if (!translate_windows_path(utf8.c_str(), normalized, sizeof(normalized))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0xFFFFFFFF;
    }
    struct stat st{};
    if (stat(normalized, &st) != 0) {
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        return 0xFFFFFFFF;
    }
    set_last_error(abi::kErrorSuccess);
    return stat_to_win32_attributes(normalized, st);
}

TL_MSABI void* tl_FindFirstFileW(const std::uint16_t* path, void* find_data) noexcept {
    if (!mapped_guest_wstring(path) || path == nullptr || path[0] == 0 ||
        find_data == nullptr || !mapped_guest_range(find_data, sizeof(Win32FindDataW), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    }
    const std::string utf8 = wide_to_utf8(path);
    if (utf8.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    }
    char normalized[4096]{};
    if (!translate_windows_path(utf8.c_str(), normalized, sizeof(normalized))) {
        set_last_error(abi::kErrorInvalidParameter);
        return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    }
    std::string dir_path;
    std::string pattern;
    const char* last_slash = strrchr(normalized, '/');
    if (last_slash != nullptr) {
        dir_path.assign(normalized, static_cast<std::size_t>(last_slash - normalized));
        pattern = last_slash + 1;
    } else {
        dir_path = ".";
        pattern = normalized;
    }
    DIR* dir = opendir(dir_path.c_str());
    if (dir == nullptr) {
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    }
    FindSlot* slot = nullptr;
    for (auto& s : g_find_slots) {
        if (!s.used) {
            slot = &s;
            break;
        }
    }
    if (slot == nullptr) {
        closedir(dir);
        set_last_error(abi::kErrorNotEnoughMemory);
        return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    }
    struct dirent* entry = nullptr;
    auto* data = static_cast<Win32FindDataW*>(find_data);
    *data = {};
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
            continue;
        }
        bool matches = false;
        if (pattern == "*") {
            matches = true;
        } else if (pattern.find('*') != std::string::npos) {
            const auto star_pos = pattern.find('*');
            const std::string prefix = pattern.substr(0, star_pos);
            matches = std::string_view(entry->d_name).substr(0, prefix.size()) == prefix;
        } else {
            matches = (pattern == entry->d_name);
        }
        if (!matches) {
            continue;
        }
        std::string full_path = dir_path + "/" + entry->d_name;
        struct stat st{};
        if (stat(full_path.c_str(), &st) == 0) {
            data->dw_file_attributes = stat_to_win32_attributes(full_path.c_str(), st);
            data->n_file_size_low = static_cast<std::uint32_t>(st.st_size & 0xFFFFFFFF);
            data->n_file_size_high = static_cast<std::uint32_t>(st.st_size >> 32);
        } else {
            data->dw_file_attributes = kFileAttributeNormal;
        }
        const std::vector<std::uint16_t> wide_name = utf8_to_wide(entry->d_name);
        if (wide_name.size() < sizeof(data->c_file_name) / sizeof(data->c_file_name[0])) {
            std::copy(wide_name.begin(), wide_name.end(), data->c_file_name);
            data->c_file_name[wide_name.size()] = 0;
        }
        slot->used = true;
        slot->dir = dir;
        slot->pattern = pattern;
        slot->directory = dir_path;
        set_last_error(abi::kErrorSuccess);
        const auto handle_val = kFindHandleBase +
                               static_cast<std::uintptr_t>(slot - g_find_slots.data());
        return reinterpret_cast<void*>(handle_val);
    }
    closedir(dir);
    set_last_error(abi::kErrorFileNotFound);
    return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
}

TL_MSABI int tl_FindNextFileW(const void* handle, void* find_data) noexcept {
    if (find_data == nullptr || !mapped_guest_range(find_data, sizeof(Win32FindDataW), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    FindSlot* slot = find_slot_for_handle(handle);
    if (slot == nullptr || !slot->used || slot->dir == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    auto* data = static_cast<Win32FindDataW*>(find_data);
    struct dirent* entry = nullptr;
    while ((entry = readdir(slot->dir)) != nullptr) {
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
            continue;
        }
        bool matches = false;
        if (slot->pattern == "*") {
            matches = true;
        } else if (slot->pattern.find('*') != std::string::npos) {
            const auto star_pos = slot->pattern.find('*');
            const std::string prefix = slot->pattern.substr(0, star_pos);
            matches = std::string_view(entry->d_name).substr(0, prefix.size()) == prefix;
        } else {
            matches = (slot->pattern == entry->d_name);
        }
        if (!matches) {
            continue;
        }
        *data = {};
        std::string full_path = slot->directory + "/" + entry->d_name;
        struct stat st{};
        if (stat(full_path.c_str(), &st) == 0) {
            data->dw_file_attributes = stat_to_win32_attributes(full_path.c_str(), st);
            data->n_file_size_low = static_cast<std::uint32_t>(st.st_size & 0xFFFFFFFF);
            data->n_file_size_high = static_cast<std::uint32_t>(st.st_size >> 32);
        } else {
            data->dw_file_attributes = kFileAttributeNormal;
        }
        const std::vector<std::uint16_t> wide_name = utf8_to_wide(entry->d_name);
        if (wide_name.size() < sizeof(data->c_file_name) / sizeof(data->c_file_name[0])) {
            std::copy(wide_name.begin(), wide_name.end(), data->c_file_name);
            data->c_file_name[wide_name.size()] = 0;
        }
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    set_last_error(abi::kErrorFileNotFound);
    return 0;
}

// FormatMessageW mínimo: suporta FORMAT_MESSAGE_FROM_SYSTEM com buffer
// alocado (ALLOCATE_BUFFER) ou fornecido. Mensagens conhecidas do runtime;
// códigos desconhecidos viram "Unknown error <n>".
TL_MSABI std::uint32_t tl_FormatMessageW(const std::uint32_t flags, const void* /*source*/,
                                         const std::uint32_t message_id,
                                         const std::uint32_t /*language_id*/, std::uint16_t* buffer,
                                         const std::uint32_t size,
                                         const void* /*arguments*/) noexcept {
    constexpr std::uint32_t kFormatMessageAllocateBuffer = 0x100U;
    constexpr std::uint32_t kFormatMessageFromSystem = 0x1000U;
    constexpr std::uint32_t kFormatMessageIgnoreInserts = 0x200U;
    constexpr std::uint32_t kKnownFlags = kFormatMessageAllocateBuffer | kFormatMessageFromSystem |
                                          kFormatMessageIgnoreInserts;
    if (buffer == nullptr || (flags & ~kKnownFlags) != 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const bool allocate = (flags & kFormatMessageAllocateBuffer) != 0;
    const bool from_system = (flags & kFormatMessageFromSystem) != 0;
    std::string text;
    if (from_system) {
        switch (message_id) {
            case abi::kErrorFileNotFound:
                text = "The system cannot find the file specified.";
                break;
            case abi::kErrorAccessDenied:
                text = "Access is denied.";
                break;
            case abi::kErrorInvalidHandle:
                text = "The handle is invalid.";
                break;
            case abi::kErrorNotEnoughMemory:
                text = "Not enough memory resources are available.";
                break;
            case abi::kErrorInvalidParameter:
                text = "The parameter is incorrect.";
                break;
            case abi::kErrorInsufficientBuffer:
                text = "The data area passed to a system call is too small.";
                break;
            case abi::kErrorNoUnicodeTranslation:
                text = "No mapping for the Unicode character exists in the target multi-byte code page.";
                break;
            default:
                text = "Unknown error " + std::to_string(message_id) + ".";
                break;
        }
    } else {
        text = "Unknown error " + std::to_string(message_id) + ".";
    }
    const std::vector<std::uint16_t> wide_text = utf8_to_wide(text);
    const std::size_t required = wide_text.size() + 1;
    if (allocate) {
        auto* storage = static_cast<std::uint16_t*>(std::malloc(required * sizeof(std::uint16_t)));
        if (storage == nullptr) {
            set_last_error(abi::kErrorNotEnoughMemory);
            return 0;
        }
        std::copy(wide_text.begin(), wide_text.end(), storage);
        storage[wide_text.size()] = 0;
        auto** output = reinterpret_cast<std::uint16_t**>(buffer);
        *output = storage;
        set_last_error(abi::kErrorSuccess);
        return static_cast<std::uint32_t>(wide_text.size());
    }
    if (required > size) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    std::copy(wide_text.begin(), wide_text.end(), buffer);
    buffer[wide_text.size()] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(wide_text.size());
}

TL_MSABI std::uint32_t tl_GetConsoleOutputCP() noexcept {
    return abi::kCpUtf8;
}

TL_MSABI int tl_SetConsoleOutputCP(const std::uint32_t /*code_page*/) noexcept {
    return 1;
}

TL_MSABI void* tl_LocalFree(void* memory) noexcept {
    std::free(memory);
    return nullptr;
}

// GetTempFileNameW: cria um arquivo temporário único no diretório dado e
// preenche o nome completo em temp_file_name (buffer wide de MAX_PATH).
TL_MSABI std::uint32_t tl_GetTempFileNameW(const std::uint16_t* path_name,
                                           const std::uint16_t* prefix_string,
                                           const std::uint32_t unique,
                                           std::uint16_t* temp_file_name) noexcept {
    constexpr std::size_t kMaxTempPath = 260;
    if (!mapped_guest_wstring(path_name) || !mapped_guest_wstring(prefix_string) ||
        path_name == nullptr || prefix_string == nullptr || temp_file_name == nullptr ||
        !mapped_guest_range(temp_file_name, kMaxTempPath * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string dir_utf8 = wide_to_utf8(path_name);
    const std::string prefix_utf8 = wide_to_utf8(prefix_string);
    std::fprintf(stderr, "[tl][dbg] GetTempFileNameW dir=\"%s\" prefix=\"%s\" unique=%u\n",
                 dir_utf8.c_str(), prefix_utf8.c_str(), unique);
    std::fprintf(stderr, "[tl][dbg] raw dir16:");
    for (int i = 0; i < 8; ++i) {
        std::fprintf(stderr, " %04X", path_name[i]);
    }
    std::fprintf(stderr, "\n");
    if (dir_utf8.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string dir_normalized = dir_utf8;
    for (char& c : dir_normalized) {
        if (c == '\\') {
            c = '/';
        }
    }
    while (!dir_normalized.empty() && dir_normalized.back() == '/') {
        dir_normalized.pop_back();
    }
    // O Windows usa os três primeiros caracteres do prefixo.
    const std::string prefix3 = prefix_utf8.substr(0, 3);
    constexpr std::uint32_t kMaxAttempts = 100000;
    for (std::uint32_t attempt = 0; attempt < kMaxAttempts; ++attempt) {
        const std::uint32_t candidate = (unique != 0) ? unique : (1U + (static_cast<std::uint32_t>(std::rand()) % 0xFFFEU));
        char name[512]{};
        std::snprintf(name, sizeof(name), "%s/%s%04X.tmp", dir_normalized.c_str(), prefix3.c_str(),
                      static_cast<unsigned>(candidate));
        const int fd = ::open(name, O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (fd >= 0) {
            ::close(fd);
            const std::vector<std::uint16_t> wide_name = utf8_to_wide(name);
            if (wide_name.size() < kMaxTempPath) {
                std::copy(wide_name.begin(), wide_name.end(), temp_file_name);
                temp_file_name[wide_name.size()] = 0;
            }
            set_last_error(abi::kErrorSuccess);
            return candidate;
        }
        if (unique != 0) {
            set_last_error(errno_to_win32(errno));
            return 0;
        }
    }
    set_last_error(abi::kErrorFileNotFound);
    return 0;
}

// CommandLineToArgvW: parseia a linha de comando no formato Windows e retorna
// um array de wchar_t* terminado em NULL. O resultado é liberado com
// LocalFree (aqui free()). O array e as strings ficam num único bloco.
TL_MSABI std::uint16_t** tl_CommandLineToArgvW(const std::uint16_t* command_line,
                                               int* argument_count) noexcept {
    if (command_line == nullptr || argument_count == nullptr) {
        return nullptr;
    }
    std::vector<std::string> arguments;
    std::string current;
    bool in_quotes = false;
    const std::uint16_t* p = command_line;
    for (;;) {
        const std::uint16_t c = *p;
        if (c == 0) {
            if (!current.empty() || in_quotes) {
                arguments.push_back(current);
            }
            break;
        }
        if (c == L'"') {
            in_quotes = !in_quotes;
            ++p;
            continue;
        }
        if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') {
            if (!in_quotes) {
                if (!current.empty()) {
                    arguments.push_back(current);
                    current.clear();
                }
                ++p;
                continue;
            }
        }
        const std::uint16_t literal[2] = {c, 0};
        current += wide_to_utf8(literal);
        ++p;
    }
    const std::size_t count = arguments.size();
    std::size_t total_units = 0;
    for (const std::string& argument : arguments) {
        total_units += utf8_to_wide(argument).size() + 1;
    }
    total_units += 1;
    auto* block = static_cast<std::uint8_t*>(
        std::calloc((count + 1) * sizeof(std::uint16_t*) + total_units * sizeof(std::uint16_t), 1));
    if (block == nullptr) {
        return nullptr;
    }
    auto** argv = reinterpret_cast<std::uint16_t**>(block);
    auto* strings = reinterpret_cast<std::uint16_t*>(block + (count + 1) * sizeof(std::uint16_t*));
    for (std::size_t i = 0; i < count; ++i) {
        const std::vector<std::uint16_t> units = utf8_to_wide(arguments[i]);
        argv[i] = strings;
        std::copy(units.begin(), units.end(), strings);
        strings += units.size();
        *strings = 0;
        ++strings;
    }
    argv[count] = nullptr;
    *argument_count = static_cast<int>(count);
    return argv;
}

TL_MSABI void* tl_CreateMutexA(const void* security_attributes, const int initial_owner,
                               const char* name) noexcept {
    if (security_attributes != nullptr || (name != nullptr && !mapped_guest_cstring(name)) ||
        (initial_owner != 0 && initial_owner != 1)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    auto free_it = std::find_if(g_syncs.begin(), g_syncs.end(),
                                [](const SyncSlot& slot) { return !slot.used; });
    if (free_it == g_syncs.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    SyncSlot& slot = *free_it;
    slot.used = true;
    slot.kind = SyncKind::Mutex;
    slot.signaled = initial_owner == 0;
    slot.owner_valid = initial_owner != 0;
    slot.owner = initial_owner != 0 ? std::this_thread::get_id() : std::thread::id{};
    slot.recursion = initial_owner != 0 ? 1U : 0U;
    set_last_error(abi::kErrorSuccess);
    return sync_slot_handle(slot);
}

TL_MSABI void* tl_CreateMutexW(const void* security_attributes, const int initial_owner,
                               const std::uint16_t* name) noexcept {
    if (name != nullptr && !mapped_guest_wstring(name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    return tl_CreateMutexA(security_attributes, initial_owner, nullptr);
}

TL_MSABI void* tl_CreateEventA(const void* security_attributes, const int manual_reset,
                               const int initial_state, const char* name) noexcept {
    if (security_attributes != nullptr || (name != nullptr && !mapped_guest_cstring(name)) ||
        (manual_reset != 0 && manual_reset != 1) || (initial_state != 0 && initial_state != 1)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    auto free_it = std::find_if(g_syncs.begin(), g_syncs.end(),
                                [](const SyncSlot& slot) { return !slot.used; });
    if (free_it == g_syncs.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    SyncSlot& slot = *free_it;
    slot.used = true;
    slot.kind = SyncKind::Event;
    slot.signaled = initial_state != 0;
    slot.manual_reset = manual_reset != 0;
    set_last_error(abi::kErrorSuccess);
    return sync_slot_handle(slot);
}

TL_MSABI void* tl_CreateEventW(const void* security_attributes, const int manual_reset,
                               const int initial_state, const std::uint16_t* name) noexcept {
    if (name != nullptr && !mapped_guest_wstring(name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    return tl_CreateEventA(security_attributes, manual_reset, initial_state, nullptr);
}

TL_MSABI int tl_SetEvent(const void* event_handle) noexcept {
    SyncSlot* slot = find_sync_slot(event_handle);
    if (slot == nullptr || slot->kind != SyncKind::Event) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(slot->mutex);
        slot->signaled = true;
    }
    slot->condition.notify_all();
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_ResetEvent(const void* event_handle) noexcept {
    SyncSlot* slot = find_sync_slot(event_handle);
    if (slot == nullptr || slot->kind != SyncKind::Event) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(slot->mutex);
        slot->signaled = false;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_ReleaseMutex(const void* mutex) noexcept {
    SyncSlot* slot = find_sync_slot(mutex);
    if (slot == nullptr || slot->kind != SyncKind::Mutex) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(slot->mutex);
        if (!slot->owner_valid || slot->owner != std::this_thread::get_id() ||
            slot->recursion == 0) {
            set_last_error(abi::kErrorAccessDenied);
            return 0;
        }
        --slot->recursion;
        if (slot->recursion == 0) {
            slot->owner_valid = false;
            slot->owner = {};
        }
    }
    slot->condition.notify_all();
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreateSemaphoreA(const void* security_attributes,
                                   const std::int32_t initial_count,
                                   const std::int32_t maximum_count,
                                   const char* name) noexcept {
    if (security_attributes != nullptr || (name != nullptr && !mapped_guest_cstring(name)) ||
        initial_count < 0 || maximum_count <= 0 || initial_count > maximum_count) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    auto free_it = std::find_if(g_syncs.begin(), g_syncs.end(),
                                [](const SyncSlot& slot) { return !slot.used; });
    if (free_it == g_syncs.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    SyncSlot& slot = *free_it;
    slot.used = true;
    slot.kind = SyncKind::Semaphore;
    slot.count = initial_count;
    slot.maximum = maximum_count;
    set_last_error(abi::kErrorSuccess);
    return sync_slot_handle(slot);
}

TL_MSABI void* tl_CreateSemaphoreW(const void* security_attributes,
                                   const std::int32_t initial_count,
                                   const std::int32_t maximum_count,
                                   const std::uint16_t* name) noexcept {
    if (name != nullptr && !mapped_guest_wstring(name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    return tl_CreateSemaphoreA(security_attributes, initial_count, maximum_count, nullptr);
}

TL_MSABI int tl_ReleaseSemaphore(const void* semaphore, const std::int32_t release_count,
                                  std::int32_t* previous_count) noexcept {
    SyncSlot* slot = find_sync_slot(semaphore);
    if (slot == nullptr || slot->kind != SyncKind::Semaphore || release_count <= 0 ||
        (previous_count != nullptr && !mapped_guest_range(previous_count, sizeof(*previous_count), true))) {
        set_last_error(slot == nullptr ? abi::kErrorInvalidHandle : abi::kErrorInvalidParameter);
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(slot->mutex);
        if (release_count > slot->maximum - slot->count) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        if (previous_count != nullptr) {
            *previous_count = slot->count;
        }
        slot->count += release_count;
    }
    slot->condition.notify_all();
    set_last_error(abi::kErrorSuccess);
    return 1;
}

[[nodiscard]] bool probe_wait_handle(const void* handle) noexcept {
    if (handle == nullptr) {
        return false;
    }
    if (find_file_slot(handle) != nullptr) {
        return true;
    }
    if (ThreadSlot* thread = find_thread_slot(handle); thread != nullptr) {
        std::lock_guard<std::mutex> lock(thread->join_mutex);
        return thread->finished;
    }
    if (SyncSlot* sync = find_sync_slot(handle); sync != nullptr) {
        if (sync->kind == SyncKind::Process) {
            return wait_process_slot(*sync, 0) == abi::kWaitObject0;
        }
        std::lock_guard<std::mutex> lock(sync->mutex);
        return sync_is_signaled(*sync);
    }
    return false;
}

TL_MSABI std::uint32_t tl_WaitForMultipleObjects(const std::uint32_t count,
                                                  const void* const* handles,
                                                  const int wait_all,
                                                  const std::uint32_t milliseconds) noexcept {
    if (count == 0 || count > 64 || handles == nullptr ||
        !mapped_guest_range(handles, static_cast<std::size_t>(count) * sizeof(*handles), false) ||
        (wait_all != 0 && wait_all != 1)) {
        set_last_error(abi::kErrorInvalidParameter);
        return abi::kWaitFailed;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        if (handles[index] == nullptr ||
            (find_file_slot(handles[index]) == nullptr &&
             find_thread_slot(handles[index]) == nullptr &&
             find_sync_slot(handles[index]) == nullptr)) {
            set_last_error(abi::kErrorInvalidHandle);
            return abi::kWaitFailed;
        }
    }
    const auto started = std::chrono::steady_clock::now();
    while (true) {
        if (wait_all != 0) {
            bool ready = true;
            for (std::uint32_t index = 0; index < count; ++index) {
                ready = ready && probe_wait_handle(handles[index]);
            }
            if (ready) {
                for (std::uint32_t index = 0; index < count; ++index) {
                    if (tl_WaitForSingleObject(handles[index], 0) == abi::kWaitFailed) {
                        set_last_error(abi::kErrorInvalidHandle);
                        return abi::kWaitFailed;
                    }
                }
                set_last_error(abi::kErrorSuccess);
                return abi::kWaitObject0;
            }
        } else {
            for (std::uint32_t index = 0; index < count; ++index) {
                if (probe_wait_handle(handles[index])) {
                    const std::uint32_t result = tl_WaitForSingleObject(handles[index], 0);
                    if (result == abi::kWaitObject0) {
                        set_last_error(abi::kErrorSuccess);
                        return abi::kWaitObject0 + index;
                    }
                }
            }
        }
        if (milliseconds == 0) {
            set_last_error(abi::kErrorSuccess);
            return abi::kWaitTimeout;
        }
        if (milliseconds != abi::kInfinite) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (elapsed >= milliseconds) {
                set_last_error(abi::kErrorSuccess);
                return abi::kWaitTimeout;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

TL_MSABI void tl_GetStartupInfoA(void* startup_info) noexcept {
    if (startup_info != nullptr) {
        std::memset(startup_info, 0, 104);
        *static_cast<std::uint32_t*>(startup_info) = 104;
    }
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI int tl_MulDiv(const int number, const int numerator, const int denominator) noexcept {
    if (denominator == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return -1;
    }
    const std::int64_t product = static_cast<std::int64_t>(number) * numerator;
    const std::int64_t divisor = denominator;
    const std::int64_t adjustment = product >= 0 ? divisor / 2 : -(divisor / 2);
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>((product + adjustment) / divisor);
}

TL_MSABI int tl_ShellNotifyIconA(const std::uint32_t message, void* data) noexcept {
    (void)message;
    (void)data;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreateFontA(int height, int width, int escapement, int orientation, int weight,
                              std::uint32_t italic, std::uint32_t underline,
                              std::uint32_t strikeout, std::uint32_t charset,
                              std::uint32_t output_precision, std::uint32_t clip_precision,
                              std::uint32_t quality, std::uint32_t pitch_and_family,
                              const char* face_name) noexcept {
    (void)height;
    (void)width;
    (void)escapement;
    (void)orientation;
    (void)weight;
    (void)italic;
    (void)underline;
    (void)strikeout;
    (void)charset;
    (void)output_precision;
    (void)clip_precision;
    (void)quality;
    (void)pitch_and_family;
    (void)face_name;
    static std::array<char, 16> tokens{};
    for (char& token : tokens) {
        if (token == 0) {
            token = 1;
            set_last_error(abi::kErrorSuccess);
            return &token;
        }
    }
    set_last_error(abi::kErrorNotEnoughMemory);
    return nullptr;
}

TL_MSABI void* tl_CreateSolidBrush(const std::uint32_t color) noexcept {
    (void)color;
    static std::array<char, 16> tokens{};
    for (char& token : tokens) {
        if (token == 0) {
            token = 1;
            set_last_error(abi::kErrorSuccess);
            return &token;
        }
    }
    set_last_error(abi::kErrorNotEnoughMemory);
    return nullptr;
}

TL_MSABI int tl_DeleteObject(const void* object) noexcept {
    (void)object;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_SetBkColor(const void* dc, const std::uint32_t color) noexcept {
    (void)dc;
    set_last_error(abi::kErrorSuccess);
    return color;
}

TL_MSABI std::uint32_t tl_SetTextColor(const void* dc, const std::uint32_t color) noexcept {
    (void)dc;
    set_last_error(abi::kErrorSuccess);
    return color;
}

TL_MSABI abi::Atom tl_RegisterClassA(const void* wnd_class) noexcept {
    if (wnd_class == nullptr || !mapped_guest_range(wnd_class, sizeof(abi::GuestWndClassA), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto* wc = static_cast<const abi::GuestWndClassA*>(wnd_class);
    abi::GuestWndClassExA ex{};
    ex.cb_size = sizeof(ex);
    ex.style = wc->style;
    ex.window_proc = wc->window_proc;
    ex.class_extra = wc->class_extra;
    ex.window_extra = wc->window_extra;
    ex.instance = wc->instance;
    ex.icon = wc->icon;
    ex.cursor = wc->cursor;
    ex.background = wc->background;
    ex.menu_name = wc->menu_name;
    ex.class_name = wc->class_name;
    return tl_RegisterClassExA(&ex);
}

TL_MSABI int tl_GetClientRect(const void* window, void* rect) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr || rect == nullptr || !mapped_guest_range(rect, sizeof(abi::GuestRect), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    auto* out = static_cast<abi::GuestRect*>(rect);
    *out = {0, 0, slot->width, slot->height};
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetCursorPos(void* point) noexcept {
    if (point == nullptr || !mapped_guest_range(point, sizeof(std::int32_t) * 2U, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    auto* coordinates = static_cast<std::int32_t*>(point);
    coordinates[0] = 0;
    coordinates[1] = 0;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_MoveWindow(const void* window, int x, int y, int width, int height,
                           int repaint) noexcept {
    (void)repaint;
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    slot->x = x;
    slot->y = y;
    slot->width = width;
    slot->height = height;
    if (slot->parent != nullptr) {
        render_controls(*slot->parent);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::intptr_t tl_SetWindowPos(const void* window, const void* insert_after, int x, int y,
                                       int width, int height, std::uint32_t flags) noexcept {
    (void)insert_after;
    (void)flags;
    return tl_MoveWindow(window, x, y, width, height, 1);
}

TL_MSABI int tl_SetWindowTextA(const void* window, const char* text) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr || text == nullptr || !mapped_guest_cstring(text)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    slot->text = text;
    if (slot->parent != nullptr) {
        render_controls(*slot->parent);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetWindowTextA(const void* window, char* text, int capacity) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr || text == nullptr || capacity <= 0 ||
        !mapped_guest_range(text, static_cast<std::size_t>(capacity), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    runtime_gui::copy_control_text(*slot, text, capacity);
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI int tl_EnableWindow(const void* window, int enable) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    const bool previous = slot->enabled;
    slot->enabled = enable != 0;
    set_last_error(abi::kErrorSuccess);
    return previous ? 1 : 0;
}

TL_MSABI const void* tl_SetFocus(const void* window) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr || !slot->is_control) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }
    WindowSlot* previous = g_focused_control;
    set_focus_control(slot);
    set_last_error(abi::kErrorSuccess);
    return previous;
}

TL_MSABI int tl_IsWindowVisible(const void* window) noexcept {
    const WindowSlot* slot = find_window_slot(window);
    return slot != nullptr && slot->visible ? 1 : 0;
}

TL_MSABI int tl_InvalidateRect(const void* window, const void* rect, int erase) noexcept {
    (void)rect;
    (void)erase;
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (slot->native != nullptr) {
        gui::flush_window(slot->native);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI const void* tl_FindWindowA(const char* class_name, const char* window_name) noexcept {
    for (const WindowSlot& slot : g_windows) {
        if (!slot.used || slot.native == nullptr ||
            (class_name != nullptr && !util::ascii_iequals(slot.class_name, class_name))) {
            continue;
        }
        if (window_name == nullptr || window_name[0] == '\0' ||
            slot.window_title == window_name) {
            return &slot;
        }
    }
    return nullptr;
}

TL_MSABI std::uintptr_t tl_LoadCursorA(const void* instance, const char* name) noexcept {
    (void)instance;
    (void)name;
    return 1;
}

TL_MSABI std::uintptr_t tl_LoadIconA(const void* instance, const char* name) noexcept {
    (void)instance;
    (void)name;
    return 1;
}

TL_MSABI std::intptr_t tl_SetClassLongPtrA(const void* window, int index,
                                            std::intptr_t value) noexcept {
    (void)window;
    (void)index;
    (void)value;
    return 0;
}

TL_MSABI int tl_SetForegroundWindow(const void* window) noexcept {
    (void)window;
    return 1;
}

TL_MSABI int tl_SendMessageA(const void* window, const std::uint32_t message,
                             const abi::Wparam wparam, const abi::Lparam lparam) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (slot->is_control) {
        if (message == abi::kWmSetFont) {
            return 0;
        }
        if (slot->control_kind == ControlKind::Edit) {
            if (message == 0x000C && lparam != 0 &&
                mapped_guest_cstring(reinterpret_cast<const char*>(lparam))) {
                slot->text = reinterpret_cast<const char*>(lparam);
                if (slot->parent != nullptr) {
                    render_controls(*slot->parent);
                }
                return 1;
            }
        }
        if (slot->control_kind == ControlKind::ComboBox) {
            if (message == abi::kCbAddString && lparam != 0 &&
                mapped_guest_cstring(reinterpret_cast<const char*>(lparam))) {
                slot->combo_items.emplace_back(reinterpret_cast<const char*>(lparam));
                return static_cast<int>(slot->combo_items.size() - 1U);
            }
            if (message == abi::kCbSetCurSel) {
                slot->combo_selection = static_cast<int>(wparam);
                if (slot->parent != nullptr) {
                    render_controls(*slot->parent);
                }
                return slot->combo_selection;
            }
            if (message == abi::kCbGetCurSel) {
                return slot->combo_selection;
            }
        }
        if (slot->control_kind == ControlKind::ListView) {
            if (message == abi::kLvmSetExtendedListViewStyle) {
                return 0;
            }
            if (message == abi::kLvmInsertColumnA) {
                return static_cast<int>(wparam);
            }
            if (message == abi::kLvmDeleteAllItems) {
                slot->list_rows.clear();
                slot->list_selection = -1;
                if (slot->parent != nullptr) {
                    render_controls(*slot->parent);
                }
                return 1;
            }
            if (message == abi::kLvmInsertItemA && lparam != 0 &&
                mapped_guest_range(reinterpret_cast<const void*>(lparam), sizeof(abi::GuestLvItemA),
                                    false)) {
                const auto* item = reinterpret_cast<const abi::GuestLvItemA*>(lparam);
                ListViewRow row;
                row.columns.resize(6);
                row.param = item->param;
                if (item->text != nullptr && mapped_guest_cstring(item->text)) {
                    row.columns[0] = item->text;
                }
                int index = item->item;
                if (index < 0 || index > static_cast<int>(slot->list_rows.size())) {
                    index = static_cast<int>(slot->list_rows.size());
                }
                slot->list_rows.insert(slot->list_rows.begin() + index, std::move(row));
                if (slot->parent != nullptr) {
                    render_controls(*slot->parent);
                }
                return index;
            }
            if (message == abi::kLvmSetItemTextA && lparam != 0 &&
                mapped_guest_range(reinterpret_cast<const void*>(lparam), sizeof(abi::GuestLvItemA),
                                    false)) {
                const int index = static_cast<int>(wparam);
                const auto* item = reinterpret_cast<const abi::GuestLvItemA*>(lparam);
                if (index >= 0 && static_cast<std::size_t>(index) < slot->list_rows.size() &&
                    item->subitem >= 0 && item->subitem < 6 && item->text != nullptr &&
                    mapped_guest_cstring(item->text)) {
                    slot->list_rows[static_cast<std::size_t>(index)].columns[static_cast<std::size_t>(item->subitem)] =
                        item->text;
                    if (slot->parent != nullptr) {
                        render_controls(*slot->parent);
                    }
                    return 1;
                }
                return 0;
            }
            if (message == abi::kLvmGetNextItem) {
                const std::int32_t start = static_cast<std::int32_t>(wparam);
                if (slot->list_selection < 0 ||
                    (start >= 0 && slot->list_selection <= start)) {
                    return -1;
                }
                return slot->list_selection;
            }
            if (message == abi::kLvmGetItemA && lparam != 0 &&
                mapped_guest_range(reinterpret_cast<const void*>(lparam), sizeof(abi::GuestLvItemA),
                                    true)) {
                const int index = static_cast<int>(wparam);
                auto* item = reinterpret_cast<abi::GuestLvItemA*>(lparam);
                if (index >= 0 && static_cast<std::size_t>(index) < slot->list_rows.size()) {
                    item->param = slot->list_rows[static_cast<std::size_t>(index)].param;
                    return 1;
                }
                return 0;
            }
            if (message == abi::kLvmGetItemTextA && lparam != 0 &&
                mapped_guest_range(reinterpret_cast<const void*>(lparam), sizeof(abi::GuestLvItemA),
                                    true)) {
                const int index = static_cast<int>(wparam);
                auto* item = reinterpret_cast<abi::GuestLvItemA*>(lparam);
                if (index >= 0 && static_cast<std::size_t>(index) < slot->list_rows.size() &&
                    item->subitem >= 0 && item->subitem < 6 && item->text != nullptr &&
                    item->text_capacity > 0 &&
                    mapped_guest_range(item->text, static_cast<std::size_t>(item->text_capacity), true)) {
                    const std::string& value = slot->list_rows[static_cast<std::size_t>(index)].columns[
                        static_cast<std::size_t>(item->subitem)];
                    const std::size_t count = std::min<std::size_t>(value.size(),
                                                                     static_cast<std::size_t>(item->text_capacity - 1));
                    std::memcpy(item->text, value.data(), count);
                    item->text[count] = '\0';
                    return static_cast<int>(count);
                }
                return 0;
            }
            if (message == abi::kLvmSortItemsEx) {
                return 1;
            }
        }
        set_last_error(abi::kErrorSuccess);
        return 0;
    }
    if (slot->wndproc != 0) {
        return static_cast<int>(call_wndproc(slot->wndproc, const_cast<abi::HWnd>(window), message,
                                             wparam, lparam));
    }
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI int tl_PostMessageA(const void* window, const std::uint32_t message,
                             const abi::Wparam wparam, const abi::Lparam lparam) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    // PostMessage insere uma mensagem na fila. `pending` é reservado para a
    // mensagem WM_CHAR produzida por TranslateMessage; sobrescrevê-la também
    // descartaria comandos já enfileirados, como os enviados por
    // TrackPopupMenu antes do WM_NULL padrão do aplicativo.
    queue_window_message(*slot, message, wparam, lparam);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreatePopupMenu() noexcept {
    const auto free_it = std::find_if(g_menus.begin(), g_menus.end(),
                                      [](const MenuSlot& menu) { return !menu.used; });
    if (free_it == g_menus.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    free_it->used = true;
    free_it->items.clear();
    set_last_error(abi::kErrorSuccess);
    return &*free_it;
}

TL_MSABI int tl_AppendMenuA(const void* menu, std::uint32_t flags, std::uintptr_t command,
                            const char* text) noexcept {
    const auto it = std::find_if(g_menus.begin(), g_menus.end(),
                                 [menu](const MenuSlot& entry) { return entry.used && &entry == menu; });
    if (it == g_menus.end()) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    it->items.push_back(gui::PopupMenuItem{.command = static_cast<std::uint32_t>(command),
                                           .text = text != nullptr ? text : "",
                                           .separator = (flags & 0x00000800U) != 0});
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_DestroyMenu(const void* menu) noexcept {
    const auto it = std::find_if(g_menus.begin(), g_menus.end(),
                                 [menu](const MenuSlot& entry) { return entry.used && &entry == menu; });
    if (it == g_menus.end()) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    *it = {};
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_TrackPopupMenu(const void* menu, std::uint32_t flags, int x, int y, int reserved,
                               const void* owner, const void* rect) noexcept {
    (void)flags;
    (void)reserved;
    (void)rect;
    const auto it = std::find_if(g_menus.begin(), g_menus.end(),
                                 [menu](const MenuSlot& entry) { return entry.used && &entry == menu; });
    WindowSlot* owner_slot = find_window_slot(owner);
    if (it == g_menus.end() || owner_slot == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uint32_t command = gui::track_popup_menu(it->items, x, y);
    if (command != 0) {
        queue_window_message(*owner_slot, abi::kWmCommand, command,
                             reinterpret_cast<abi::Lparam>(menu));
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

// ---------------------------------------------------------------------------
// Processos convidados (contrato limitado e validado pelo mesmo loader).
// ---------------------------------------------------------------------------

constexpr std::uint32_t kStillActive = 259U;
constexpr std::uint32_t kChildProcessFailure = 0xC0000001U;
constexpr std::size_t kChildProcessProtocolSize = 5;

struct GuestProcessInformation {
    void* process_handle{};
    void* thread_handle{};
    std::uint32_t process_id{};
    std::uint32_t thread_id{};
};
static_assert(sizeof(GuestProcessInformation) == 24);

void write_child_process_result(const int fd, const GuestExecutionResult result) noexcept {
    const std::array<std::byte, kChildProcessProtocolSize> message{
        result.exited_explicitly ? std::byte{1} : std::byte{0},
        std::byte{static_cast<unsigned char>(result.exit_code & 0xFFU)},
        std::byte{static_cast<unsigned char>((result.exit_code >> 8U) & 0xFFU)},
        std::byte{static_cast<unsigned char>((result.exit_code >> 16U) & 0xFFU)},
        std::byte{static_cast<unsigned char>((result.exit_code >> 24U) & 0xFFU)},
    };
    std::size_t written = 0;
    while (written < message.size()) {
        const ssize_t count = ::write(fd, message.data() + written, message.size() - written);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        written += static_cast<std::size_t>(count);
    }
}

bool read_child_process_result(const int fd, GuestExecutionResult& result) noexcept {
    std::array<std::byte, kChildProcessProtocolSize> message{};
    std::size_t read_count = 0;
    while (read_count < message.size()) {
        const ssize_t count = ::read(fd, message.data() + read_count, message.size() - read_count);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        read_count += static_cast<std::size_t>(count);
    }
    if (message[0] != std::byte{0} && message[0] != std::byte{1}) {
        return false;
    }
    result.exited_explicitly = message[0] == std::byte{1};
    result.exit_code = 0;
    for (std::uint32_t shift = 0; shift < 4; ++shift) {
        result.exit_code |= static_cast<std::uint32_t>(
                                static_cast<unsigned char>(message[1 + shift]))
                            << (shift * 8U);
    }
    return true;
}

bool read_guest_file_for_process(const char* path, std::vector<std::byte>& bytes) noexcept {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return false;
    }
    std::istreambuf_iterator<char> iterator{stream};
    const std::istreambuf_iterator<char> end;
    for (; iterator != end; ++iterator) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(*iterator)));
    }
    return !stream.bad();
}

[[noreturn]] void run_created_guest_child(const std::string& path, const int result_fd) noexcept {
    GuestExecutionResult result{};
    std::vector<std::byte> bytes;
    if (!read_guest_file_for_process(path.c_str(), bytes)) {
        result.exit_code = kChildProcessFailure;
        write_child_process_result(result_fd, result);
        ::close(result_fd);
        ::_exit(0);
    }

    const pe::ParseResult parsed = pe::parse_pe(bytes);
    if (parsed.status != pe::ParseStatus::Success) {
        result.exit_code = kChildProcessFailure;
        write_child_process_result(result_fd, result);
        ::close(result_fd);
        ::_exit(0);
    }
    loader::register_builtin_modules();
    loader::PrepareResult prepared = loader::prepare_process(parsed.info, bytes);
    if (prepared.status != loader::PrepareStatus::Success ||
        (prepared.process.image.delta != 0 && !prepared.process.image.has_relocation_directory)) {
        loader::destroy_process(prepared.process);
        result.exit_code = kChildProcessFailure;
        write_child_process_result(result_fd, result);
        ::close(result_fd);
        ::_exit(0);
    }

    loader::GuestProcess& process = prepared.process;
    set_guest_module_path(path.c_str());
    msvcrt_set_guest_command_line(std::vector<std::string>{path});
    set_guest_image_view(process.image.memory, process.image.size,
                         parsed.info.resource_directory_rva,
                         parsed.info.resource_directory_size);
    result = execute_guest_entry(process.thread.entry_point, process.thread.stack_top);
    set_guest_image_view(nullptr, 0, 0, 0);
    loader::destroy_process(process);
    write_child_process_result(result_fd, result);
    ::close(result_fd);
    ::_exit(0);
}

[[nodiscard]] bool first_process_argument(const std::uint16_t* command_line,
                                          std::string& result) noexcept {
    if (!mapped_guest_wstring(command_line) || command_line == nullptr) {
        return false;
    }
    const std::string utf8 = wide_to_utf8(command_line);
    std::size_t begin = 0;
    while (begin < utf8.size() && (utf8[begin] == ' ' || utf8[begin] == '\t')) {
        ++begin;
    }
    if (begin == utf8.size()) {
        return false;
    }
    std::size_t end = begin;
    if (utf8[begin] == '"') {
        ++begin;
        end = utf8.find('"', begin);
        if (end == std::string::npos) {
            return false;
        }
        result = utf8.substr(begin, end - begin);
    } else {
        while (end < utf8.size() && utf8[end] != ' ' && utf8[end] != '\t') {
            ++end;
        }
        result = utf8.substr(begin, end - begin);
    }
    return !result.empty();
}

namespace {

[[nodiscard]] std::uint32_t wait_process_slot(SyncSlot& slot,
                                              const std::uint32_t milliseconds) noexcept {
    if (!slot.used || slot.kind != SyncKind::Process) {
        return abi::kWaitFailed;
    }
    if (!slot.process_running) {
        return abi::kWaitObject0;
    }
    const auto started = std::chrono::steady_clock::now();
    while (true) {
        int status = 0;
        const pid_t waited = ::waitpid(slot.child_pid, &status, WNOHANG);
        if (waited == slot.child_pid) {
            GuestExecutionResult result{};
            if (WIFEXITED(status) && read_child_process_result(slot.child_result_fd, result)) {
                slot.process_exit_code = result.exit_code;
            } else if (WIFSIGNALED(status)) {
                slot.process_exit_code = kChildProcessFailure;
            } else {
                slot.process_exit_code = kChildProcessFailure;
            }
            ::close(slot.child_result_fd);
            slot.child_result_fd = -1;
            slot.child_pid = -1;
            slot.process_running = false;
            return abi::kWaitObject0;
        }
        if (waited < 0 && errno != EINTR) {
            slot.process_exit_code = kChildProcessFailure;
            if (slot.child_result_fd >= 0) {
                ::close(slot.child_result_fd);
                slot.child_result_fd = -1;
            }
            slot.child_pid = -1;
            slot.process_running = false;
            return abi::kWaitFailed;
        }
        if (milliseconds == 0) {
            return abi::kWaitTimeout;
        }
        if (milliseconds != abi::kInfinite &&
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started)
                    .count() >= milliseconds) {
            return abi::kWaitTimeout;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

}  // namespace

TL_MSABI int tl_CreateProcessW(const std::uint16_t* application_name,
                               std::uint16_t* command_line, const void* process_attributes,
                               const void* thread_attributes, const int inherit_handles,
                               const std::uint32_t creation_flags, const void* environment,
                               const std::uint16_t* current_directory, void* startup_info,
                               void* process_information) noexcept {
    (void)startup_info;
    if (process_attributes != nullptr || thread_attributes != nullptr || inherit_handles != 0 ||
        creation_flags != 0 || environment != nullptr || current_directory != nullptr ||
        process_information == nullptr ||
        !mapped_guest_range(process_information, sizeof(GuestProcessInformation), true) ||
        (application_name != nullptr && !mapped_guest_wstring(application_name)) ||
        (application_name == nullptr && !mapped_guest_wstring(command_line))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string guest_path;
    if (application_name != nullptr) {
        guest_path = wide_to_utf8(application_name);
    } else if (!first_process_argument(command_line, guest_path)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char normalized_path[4096]{};
    if (!translate_windows_path(guest_path.c_str(), normalized_path, sizeof(normalized_path))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    auto free_it = std::find_if(g_syncs.begin(), g_syncs.end(),
                                [](const SyncSlot& slot) { return !slot.used; });
    if (free_it == g_syncs.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    int result_pipe[2] = {-1, -1};
    if (::pipe(result_pipe) != 0) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    const pid_t child = ::fork();
    if (child < 0) {
        ::close(result_pipe[0]);
        ::close(result_pipe[1]);
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    if (child == 0) {
        ::close(result_pipe[0]);
        run_created_guest_child(normalized_path, result_pipe[1]);
    }
    ::close(result_pipe[1]);
    SyncSlot& slot = *free_it;
    slot.used = true;
    slot.kind = SyncKind::Process;
    slot.child_pid = child;
    slot.child_result_fd = result_pipe[0];
    slot.process_running = true;
    slot.process_exit_code = kStillActive;
    auto* information = static_cast<GuestProcessInformation*>(process_information);
    *information = {.process_handle = sync_slot_handle(slot),
                    .thread_handle = nullptr,
                    .process_id = static_cast<std::uint32_t>(child),
                    .thread_id = 0};
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetExitCodeProcess(const void* process, std::uint32_t* exit_code) noexcept {
    SyncSlot* slot = find_sync_slot(process);
    if (slot == nullptr || slot->kind != SyncKind::Process || exit_code == nullptr ||
        !mapped_guest_range(exit_code, sizeof(*exit_code), true)) {
        set_last_error(slot == nullptr ? abi::kErrorInvalidHandle : abi::kErrorInvalidParameter);
        return 0;
    }
    static_cast<void>(wait_process_slot(*slot, 0));
    *exit_code = slot->process_running ? kStillActive : slot->process_exit_code;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_TerminateProcess(const void* process, const std::uint32_t exit_code) noexcept {
    SyncSlot* slot = find_sync_slot(process);
    if (slot == nullptr || slot->kind != SyncKind::Process) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (!slot->process_running || slot->child_pid <= 0 || ::kill(slot->child_pid, SIGKILL) != 0) {
        set_last_error(abi::kErrorAccessDenied);
        return 0;
    }
    static_cast<void>(wait_process_slot(*slot, abi::kInfinite));
    slot->process_exit_code = exit_code;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

}  // namespace tradutorlinux
