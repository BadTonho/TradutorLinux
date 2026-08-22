#pragma once

#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/gui/x11.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/process.hpp"
#include "tradutorlinux/pe/pe_reader.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/runtime/error_map.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/runtime/msvcrt.hpp"
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

// Estado de execução da thread atual
extern thread_local std::jmp_buf g_guest_exit_context;
extern thread_local bool g_guest_execution_active;
extern thread_local std::uint32_t g_guest_exit_code;
extern thread_local std::uint32_t g_last_error;
extern thread_local runtime::GuestTeb* g_current_teb;
extern runtime::GuestPeb g_guest_peb;

// Caminho do executável convidado
extern std::string g_module_file_name;

// Tokens padrão
extern char kStdInputToken;
extern char kStdOutputToken;
extern char kStdErrorToken;
extern char kStockObjectTokens[24];

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
    bool view{false};  // true: visão de MapViewOfFile; false: NtAllocateVirtualMemory
};

extern std::mutex g_files_mutex;
extern std::array<FileSlot, 256> g_files;

struct FileMappingSlot {
    bool used{false};
    int fd{-1};
    std::uint64_t size{0};
    std::uint32_t protect{0};
    std::string name;
};
extern std::mutex g_mapping_mutex;
extern std::array<FileMappingSlot, 64> g_mappings;

extern std::mutex g_allocations_mutex;
extern std::array<AllocationSlot, 256> g_allocations;

// Imagem do convidado
extern const std::byte* g_guest_image_base;
extern std::size_t g_guest_image_size;
extern std::uint32_t g_guest_resource_rva;
extern std::uint32_t g_guest_resource_size;

constexpr std::uintptr_t kResourceHandleBase = 0x0000A00000000000ULL;
struct ResourceSlot {
    bool used{false};
    std::uint32_t data_rva{0};
    std::uint32_t data_size{0};
};
extern std::mutex g_resource_mutex;
extern std::array<ResourceSlot, 256> g_resources;

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
extern std::mutex g_sync_mutex;
extern std::array<SyncSlot, 256> g_syncs;

constexpr std::uint32_t kMainThreadId = 1;
extern thread_local std::uint32_t g_current_thread_id;
extern std::atomic<std::uint32_t> g_next_thread_id;

struct ThreadSlot {
    bool used{false};
    std::uint32_t thread_id{};
    void* teb{nullptr};
    std::byte* stack{nullptr};
    std::size_t stack_size{0};
    std::uintptr_t stack_top{};
    std::function<void()> thread_func;
    std::thread host_thread;
    std::mutex join_mutex;
    bool finished{false};
    bool joined{false};
    int exit_code{0};
    std::condition_variable finish_cv;
};
extern std::mutex g_threads_mutex;
extern std::array<ThreadSlot, 256> g_threads;

constexpr std::uintptr_t kThreadHandleBase = 0x0000400000000000ULL;

constexpr std::uint32_t kMaxTlsSlots = 256;
extern std::array<bool, kMaxTlsSlots> g_tls_indices_used;
extern std::mutex g_tls_mutex;
extern thread_local std::array<void*, 64> g_guest_tls_slots;
extern std::uintptr_t g_unhandled_exception_filter;

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
};

using runtime_gui::ControlKind;
using runtime_gui::GuestTimer;
using runtime_gui::ListViewRow;
using runtime_gui::WindowSlot;

extern std::array<ClassSlot, 32> g_classes;
extern std::array<WindowSlot, 32> g_windows;
extern WindowSlot* g_focused_control;
extern bool g_quit_requested;
extern std::uint32_t g_quit_code;

struct MenuSlot {
    bool used{false};
    std::vector<gui::PopupMenuItem> items;
};
extern std::array<MenuSlot, 16> g_menus;

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
    g_last_error = error;
    if (g_current_teb != nullptr) {
        g_current_teb->last_error_value = error;
    }
}

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
FindSlot* find_slot_for_handle(const void* handle) noexcept;

int stock_object_index(const void* token) noexcept;
std::uint32_t stat_to_win32_attributes(const char* path, const struct stat& st) noexcept;
std::uint32_t decode_multibyte(std::uint32_t code_page, const std::uint8_t* bytes, std::size_t length, std::size_t& pos) noexcept;

bool set_guest_gs_base(const void* base) noexcept;
void* allocate_guest_teb(std::uintptr_t stack_top, std::uintptr_t stack_size) noexcept;
void free_guest_teb(void* teb) noexcept;

void bump_guest_allocation_generation() noexcept;

}  // namespace tradutorlinux
