#pragma once

#include "runtime_state_common.hpp"

#include "tradutorlinux/loader/module_graph.hpp"

#include <array>
#include <atomic>
#include <csetjmp>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>

namespace tradutorlinux {

[[noreturn]] void guest_longjmp(std::jmp_buf context, int value) noexcept;

extern char kStdInputToken;
extern char kStdOutputToken;
extern char kStdErrorToken;
extern char kStockObjectTokens[24];
extern std::atomic<std::uintptr_t> g_pointer_cookie;

constexpr std::uintptr_t kResourceHandleBase = 0x0000A00000000000ULL;
using ResourceSlot = runtime::GuestContext::ContextResourceSlot;

constexpr std::uintptr_t kSnapshotHandleBase = 0x0000D00000000000ULL;
struct SnapshotSlot {
    runtime::ObjectHeader header{runtime::HandleObjectType::Snapshot, 0};
    bool used{false};
    std::vector<std::uint32_t> pids;
    std::size_t next_index{0};
    std::uint32_t flags{0};
};

extern std::array<SnapshotSlot, 16> g_snapshots;
extern std::mutex g_snapshot_mutex;

constexpr std::uintptr_t kProcessHandleBase = 0x0000E00000000000ULL;
constexpr std::uintptr_t kProcessHandleRange = 0x100000ULL;

#define g_guest_exit_context (::tradutorlinux::runtime::guest_context().exit_context)
#define g_guest_execution_active (::tradutorlinux::runtime::guest_context().execution_active)
#define g_guest_exit_code (::tradutorlinux::runtime::guest_context().exit_code)
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
#define g_resources (::tradutorlinux::runtime::guest_context().resources)
#define g_resource_mutex (::tradutorlinux::runtime::guest_context().resource_mutex)
#define g_unhandled_exception_filter (::tradutorlinux::runtime::guest_context().unhandled_exception_filter)

void reset_process_console_state() noexcept;

}  // namespace tradutorlinux
