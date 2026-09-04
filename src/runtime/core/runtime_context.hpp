#pragma once

#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/gui/platform.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/process.hpp"
#include "tradutorlinux/pe/pe_reader.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/runtime/error_map.hpp"
#include "tradutorlinux/runtime/guest_context.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/runtime/msvcrt.hpp"
#include "tradutorlinux/runtime/teb.hpp"
#include "tradutorlinux/runtime/unwind.hpp"
#include "tradutorlinux/util/basics.hpp"
#include "tradutorlinux/util/unicode.hpp"
#include "../gui_controls.hpp"

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
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

using runtime::current_guest_context;
using runtime::default_guest_context;
using runtime::guest_context;
using runtime::GuestContext;
using runtime::GuestContextScope;

// Estado de execução da thread atual

// Caminho do executável convidado

// Tokens padrão
extern char kStdInputToken;
extern char kStdOutputToken;
extern char kStdErrorToken;
extern char kStockObjectTokens[24];
extern std::atomic<std::uintptr_t> g_pointer_cookie;

using FileSlot = runtime::GuestContext::ContextFileSlot;

using AllocationSlot = runtime::GuestContext::ContextAllocationSlot;

// Blocos devolvidos por Global/LocalAlloc. O endereço do bloco é usado como
// handle no subconjunto atual; a tabela permite validar GlobalLock/Unlock/Free
// sem liberar um ponteiro arbitrário do convidado.
using GlobalMemorySlot = runtime::GuestContext::ContextGlobalMemorySlot;

using DibSlot = runtime::GuestContext::ContextDibSlot;


using FileMappingSlot = runtime::GuestContext::ContextFileMappingSlot;


[[nodiscard]] bool register_local_free_block(void* address) noexcept;
[[nodiscard]] bool take_local_free_block(void* address) noexcept;
[[nodiscard]] bool register_tls_dynamic_block(void* owner_teb, void* address) noexcept;
void free_tls_dynamic_blocks(void* owner_teb) noexcept;
[[nodiscard]] bool register_wts_allocation(void* address) noexcept;
[[nodiscard]] bool take_wts_allocation(void* address) noexcept;
constexpr std::size_t kPointerBackedTlsSlotOffset = 0x430U;
constexpr std::size_t kPointerBackedTlsAllocationSize = 0x1000U;
void initialize_pointer_backed_tls_slot(void* teb) noexcept;

// Imagem do convidado

constexpr std::uintptr_t kResourceHandleBase = 0x0000A00000000000ULL;
using ResourceSlot = runtime::GuestContext::ContextResourceSlot;

constexpr std::uintptr_t kSyncHandleBase = 0x0000600000000000ULL;
using SyncKind = runtime::ContextSyncKind;
using SyncSlot = runtime::GuestContext::ContextSyncSlot;

constexpr std::uint32_t kMainThreadId = 1;

struct ThreadSlot {
    bool used{false};
    std::uint32_t thread_id{};
    void* teb{nullptr};
    std::byte* stack{nullptr};
    std::size_t stack_size{0};
    std::uintptr_t stack_top{};
    runtime::GuestUnwindView unwind_view{};
    std::shared_ptr<struct FlsThreadValues> fls_values;
    std::function<void()> thread_func;
    std::thread host_thread;
    std::mutex join_mutex;
    bool finished{false};
    bool joined{false};
    bool handle_closed{false};
    int exit_code{0};
    std::condition_variable finish_cv;
};
extern std::mutex g_threads_mutex;
extern std::array<ThreadSlot, 256> g_threads;

extern thread_local runtime::GuestTeb* g_thread_teb;
extern thread_local std::uint32_t g_thread_last_error;

constexpr std::uintptr_t kThreadHandleBase = 0x0000400000000000ULL;

constexpr std::uint32_t kMaxTlsSlots = 256;
extern thread_local std::array<void*, 64> g_guest_tls_slots;
extern thread_local std::uint32_t g_current_thread_id;

constexpr std::uint32_t kMaxFlsSlots = 128;
struct FlsSlot {
    bool used{false};
    std::uintptr_t callback{};
};
struct FlsThreadValues {
    std::array<void*, kMaxFlsSlots> values{};
};
extern std::array<FlsSlot, kMaxFlsSlots> g_fls_slots;
extern std::mutex g_fls_mutex;
extern std::vector<std::weak_ptr<FlsThreadValues>> g_fls_threads;
extern thread_local std::shared_ptr<FlsThreadValues> g_current_fls_values;

[[nodiscard]] std::shared_ptr<FlsThreadValues> ensure_fls_thread_values();
void set_current_fls_thread_values(std::shared_ptr<FlsThreadValues> values);
void cleanup_current_fls_values() noexcept;
void reset_fls_process_state() noexcept;
void reset_process_console_state() noexcept;

struct CriticalSectionEntry {
    void* guest_address{nullptr};
    pthread_mutex_t mutex{};
    bool used{false};
};
extern std::mutex g_cs_mutex;
extern std::array<CriticalSectionEntry, 256> g_critical_sections;

struct ClassSlot {
    bool used{false};
    std::string name;
    std::uintptr_t wndproc{0};
    std::uint16_t atom{0};
    std::uintptr_t menu_name_raw{0};
    std::u16string menu_name_text;
};

using runtime_gui::ControlKind;
using runtime_gui::GuestTimer;
using runtime_gui::ListViewRow;
using runtime_gui::ToolbarButton;
using runtime_gui::WindowSlot;

extern std::array<ClassSlot, 32> g_classes;
extern std::array<WindowSlot, 32> g_windows;
extern WindowSlot* g_focused_control;
extern WindowSlot* g_active_dialog;
extern std::mutex g_modal_mutex;
extern bool g_modal_done;
extern std::intptr_t g_modal_result;
extern WindowSlot* g_modal_parent;
extern bool g_modal_parent_was_enabled;
extern bool g_quit_requested;
extern std::uint32_t g_quit_code;

struct MenuSlot;

struct MenuItem {
    std::uint32_t type{0};
    std::uint32_t state{0};
    std::uint32_t command_id{0};
    std::uint16_t flags{0};
    std::string text;
    MenuSlot* submenu{nullptr};
};

struct MenuSlot {
    bool used{false};
    std::vector<gui::PopupMenuItem> items;
    std::vector<MenuItem> logical_items;
};
extern std::array<MenuSlot, 256> g_menus;

[[nodiscard]] inline const MenuSlot* find_menu_slot(const void* const menu) noexcept {
    if (menu == nullptr) {
        return nullptr;
    }
    const auto it = std::find_if(g_menus.begin(), g_menus.end(),
                                 [menu](const MenuSlot& entry) {
                                     return entry.used && &entry == menu;
                                 });
    return it == g_menus.end() ? nullptr : &*it;
}

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

constexpr std::uintptr_t kFindHandleBase = 0x0000800000000000ULL;
struct FindSlot {
    bool used{false};
    DIR* dir{nullptr};
    std::string pattern;
    std::string directory;
};
extern std::array<FindSlot, 16> g_find_slots;

constexpr std::uintptr_t kSnapshotHandleBase = 0x0000D00000000000ULL;
struct SnapshotSlot {
    bool used{false};
    std::vector<std::uint32_t> pids;
    std::size_t next_index{0};
    std::uint32_t flags{0};
};
extern std::array<SnapshotSlot, 16> g_snapshots;
extern std::mutex g_snapshot_mutex;

constexpr std::uintptr_t kProcessHandleBase = 0x0000E00000000000ULL;
constexpr std::uintptr_t kProcessHandleRange = 0x100000ULL; // pid até ~1M

// Helpers compartilhados
inline void set_last_error(const std::uint32_t error) noexcept {
    g_thread_last_error = error;
    if (g_thread_teb != nullptr) {
        g_thread_teb->last_error_value = error;
    }
}

// Aliases temporários do Marco 1. Os módulos existentes ainda usam os nomes
// antigos, mas o armazenamento agora pertence ao GuestContext ativo.
#define g_last_error (::tradutorlinux::g_thread_last_error)
#define g_guest_exit_context (::tradutorlinux::runtime::guest_context().exit_context)
#define g_guest_execution_active (::tradutorlinux::runtime::guest_context().execution_active)
#define g_guest_exit_code (::tradutorlinux::runtime::guest_context().exit_code)
#define g_current_teb (::tradutorlinux::g_thread_teb)
#define g_guest_peb (::tradutorlinux::runtime::guest_context().peb)
#define g_guest_process_params (::tradutorlinux::runtime::guest_context().process_parameters)
#define g_module_file_name (::tradutorlinux::runtime::guest_context().module_file_name)
#define g_guest_prefix_path (::tradutorlinux::runtime::guest_context().prefix_path)
#define g_guest_image_base (::tradutorlinux::runtime::guest_context().image_base)
#define g_guest_image_size (::tradutorlinux::runtime::guest_context().image_size)
#define g_guest_resource_rva (::tradutorlinux::runtime::guest_context().resource_rva)
#define g_guest_resource_size (::tradutorlinux::runtime::guest_context().resource_size)
#define g_guest_tls_start_raw (::tradutorlinux::runtime::guest_context().tls_start_raw)
#define g_guest_tls_end_raw (::tradutorlinux::runtime::guest_context().tls_end_raw)
#define g_guest_tls_index_addr (::tradutorlinux::runtime::guest_context().tls_index_address)
#define g_guest_tls_callbacks (::tradutorlinux::runtime::guest_context().tls_callbacks)
#define g_standard_handles (::tradutorlinux::runtime::guest_context().standard_handles)
#define g_process_context_mutex (::tradutorlinux::runtime::guest_context().process_context_mutex)
#define g_files (::tradutorlinux::runtime::guest_context().files)
#define g_files_mutex (::tradutorlinux::runtime::guest_context().files_mutex)
#define g_allocations (::tradutorlinux::runtime::guest_context().allocations)
#define g_allocations_mutex (::tradutorlinux::runtime::guest_context().allocations_mutex)
#define g_mappings (::tradutorlinux::runtime::guest_context().mappings)
#define g_mapping_mutex (::tradutorlinux::runtime::guest_context().mapping_mutex)
#define g_global_memory (::tradutorlinux::runtime::guest_context().global_memory)
#define g_global_memory_mutex (::tradutorlinux::runtime::guest_context().global_memory_mutex)
#define g_local_free_blocks (::tradutorlinux::runtime::guest_context().local_free_blocks)
#define g_local_free_mutex (::tradutorlinux::runtime::guest_context().local_free_mutex)
#define g_dibs (::tradutorlinux::runtime::guest_context().dibs)
#define g_dib_mutex (::tradutorlinux::runtime::guest_context().dib_mutex)
#define g_resources (::tradutorlinux::runtime::guest_context().resources)
#define g_resource_mutex (::tradutorlinux::runtime::guest_context().resource_mutex)
#define g_tls_indices_used (::tradutorlinux::runtime::guest_context().tls_indices_used)
#define g_tls_mutex (::tradutorlinux::runtime::guest_context().tls_mutex)
#define g_next_thread_id (::tradutorlinux::runtime::guest_context().next_thread_id)
#define g_unhandled_exception_filter (::tradutorlinux::runtime::guest_context().unhandled_exception_filter)
#define g_syncs (::tradutorlinux::runtime::guest_context().syncs)
#define g_sync_mutex (::tradutorlinux::runtime::guest_context().sync_mutex)

inline bool mapped_guest_range(const void* address, std::size_t size, bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

inline bool mapped_guest_cstring(const char* value) noexcept {
    return runtime::validate_mapped_cstring(value);
}

inline bool mapped_guest_wstring(const std::uint16_t* value) noexcept {
    return runtime::validate_mapped_wstring(value);
}

void runtime_trace(const char* event, const std::array<diagnostics::TraceField, 4>& fields,
                   std::size_t field_count) noexcept;
void trace_guest_failure(const char* symbol, const char* operation, const char* detail) noexcept;
void trace_linux_failure(const char* symbol, const char* operation, int error, std::uint32_t win32_error) noexcept;
void trace_stub(const char* symbol) noexcept;

[[nodiscard]] inline bool user32_gui_thread_allowed(const char* const symbol) noexcept {
    if (g_current_thread_id == kMainThreadId) {
        return true;
    }
    set_last_error(abi::kErrorNotSupported);
    trace_guest_failure(symbol, "thread-affinity",
                        "USER32 stateful GUI calls require the primary guest thread");
    return false;
}

bool translate_windows_path(const char* win_path, char* linux_out, std::size_t out_size) noexcept;
bool wide_path_to_string(const std::uint16_t* path, std::string& result) noexcept;
bool normalized_wide_path(const std::uint16_t* path, char (&buffer)[4096]) noexcept;

FileSlot* find_file_slot(const void* handle) noexcept;
int handle_fd(const void* handle) noexcept;

ThreadSlot* find_thread_slot(const void* handle) noexcept;
void* thread_slot_to_handle(ThreadSlot& slot) noexcept;

SyncSlot* find_sync_slot(const void* handle) noexcept;
void* sync_slot_handle(const SyncSlot& slot) noexcept;
std::uint32_t wait_sync_slot(SyncSlot& slot, std::uint32_t milliseconds) noexcept;
std::uint32_t wait_process_slot(SyncSlot& slot, std::uint32_t milliseconds) noexcept;
void clear_sync_slot(SyncSlot& slot) noexcept;

CriticalSectionEntry* find_cs_entry(void* cs) noexcept;
CriticalSectionEntry* alloc_cs_entry(void* cs) noexcept;

ClassSlot* find_class_slot(const char* name) noexcept;
WindowSlot* find_window_slot(const void* handle) noexcept;

WindowSlot* create_logical_control(WindowSlot& parent, std::string_view class_name,
                                   std::string_view title, std::uint32_t style,
                                   std::uintptr_t control_id, int x, int y, int width,
                                   int height) noexcept;

struct WindowDrawingTarget {
    gui::NativeWindow native{nullptr};
    int offset_x{0};
    int offset_y{0};
};

[[nodiscard]] WindowDrawingTarget window_drawing_target(const void* handle) noexcept;
FindSlot* find_slot_for_handle(const void* handle) noexcept;

int stock_object_index(const void* token) noexcept;
std::uint32_t stat_to_win32_attributes(const char* path, const struct stat& st) noexcept;
std::uint32_t apply_win32_file_attributes(const char* path, std::uint32_t attributes) noexcept;
std::uint32_t decode_multibyte(std::uint32_t code_page, const std::uint8_t* bytes, std::size_t length, std::size_t& pos) noexcept;

bool set_guest_gs_base(const void* base) noexcept;
void* allocate_guest_teb(std::uintptr_t stack_top, std::uintptr_t stack_size,
                         std::uint32_t thread_id = 1) noexcept;
void free_guest_teb(void* teb) noexcept;

void bump_guest_allocation_generation() noexcept;

}  // namespace tradutorlinux
