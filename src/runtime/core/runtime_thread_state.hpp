#pragma once

#include "runtime_state_common.hpp"

#include "tradutorlinux/runtime/unwind.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <csetjmp>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <pthread.h>

namespace tradutorlinux {

constexpr std::uintptr_t kThreadHandleBase = 0x0000400000000000ULL;
constexpr std::uint32_t kMainThreadId = 1;
constexpr std::uint32_t kMaxTlsSlots = 256;
constexpr std::uint32_t kMaxFlsSlots = 128;

struct ThreadSlot {
    runtime::ObjectHeader header{runtime::HandleObjectType::Thread, 1};
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
extern thread_local std::uint32_t g_current_thread_id;
extern thread_local std::array<void*, 64> g_guest_tls_slots;

#define g_current_teb (::tradutorlinux::g_thread_teb)

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

#define g_tls_indices_used (::tradutorlinux::runtime::guest_context().tls_indices_used)
#define g_tls_mutex (::tradutorlinux::runtime::guest_context().tls_mutex)
#define g_next_thread_id (::tradutorlinux::runtime::guest_context().next_thread_id)

[[nodiscard]] std::shared_ptr<FlsThreadValues> ensure_fls_thread_values();
void set_current_fls_thread_values(std::shared_ptr<FlsThreadValues> values);
void cleanup_current_fls_values() noexcept;
void reset_fls_process_state() noexcept;

struct CriticalSectionEntry {
    void* guest_address{nullptr};
    pthread_mutex_t mutex{};
    bool used{false};
};

extern std::mutex g_cs_mutex;
extern std::array<CriticalSectionEntry, 256> g_critical_sections;

constexpr std::size_t kPointerBackedTlsSlotOffset = 0x430U;
constexpr std::size_t kPointerBackedTlsAllocationSize = 0x1000U;

[[nodiscard]] bool register_tls_dynamic_block(void* owner_teb, void* address) noexcept;
void free_tls_dynamic_blocks(void* owner_teb) noexcept;
void initialize_pointer_backed_tls_slot(void* teb) noexcept;

bool set_guest_gs_base(const void* base) noexcept;
using GuestGsBaseTestHook = bool (*)(const void* base) noexcept;
void set_guest_gs_base_test_hook(GuestGsBaseTestHook hook) noexcept;
void* allocate_guest_teb(std::uintptr_t stack_top, std::uintptr_t stack_size,
                         std::uint32_t thread_id = 1) noexcept;
void free_guest_teb(void* teb) noexcept;

[[nodiscard]] inline bool user32_gui_thread_allowed(const char* const symbol) noexcept {
    if (g_current_thread_id == kMainThreadId) {
        return true;
    }
    set_last_error(abi::kErrorNotSupported);
    trace_guest_failure(symbol, "thread-affinity",
                        "USER32 stateful GUI calls require the primary guest thread");
    return false;
}

}  // namespace tradutorlinux
