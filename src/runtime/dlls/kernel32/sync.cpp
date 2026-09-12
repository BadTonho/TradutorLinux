#include "kernel32_common.hpp"
#include "../../core/runtime_handle_state.hpp"
#include "../../core/runtime_thread_state.hpp"

#include <cstring>
#include <shared_mutex>

namespace tradutorlinux {

namespace {

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

[[nodiscard]] bool probe_wait_handle(const void* handle) noexcept {
    if (handle == nullptr || handle == kInvalidHandleValue) {
        return false;
    }
    FileSlotGuard file_guard(handle);
    if (file_guard.get() != nullptr) {
        return true;
    }
    if (file_guard.is_file_handle()) {
        return false;
    }
    const runtime::ObjectHeader* header = runtime::get_object_header(handle);
    if (header == nullptr) {
        return false;
    }
    switch (header->type) {
        case runtime::HandleObjectType::File:
            return true;
        case runtime::HandleObjectType::Thread: {
            if (ThreadSlot* thread = find_thread_slot(handle); thread != nullptr) {
                std::lock_guard<std::mutex> lock(thread->join_mutex);
                return thread->finished;
            }
            return false;
        }
        case runtime::HandleObjectType::Sync: {
            if (SyncSlot* sync = find_sync_slot(handle); sync != nullptr) {
                if (sync->kind == SyncKind::Process) {
                    return wait_process_slot(*sync, 0) == abi::kWaitObject0;
                }
                std::lock_guard<std::mutex> lock(sync->mutex);
                return sync_is_signaled(*sync);
            }
            return false;
        }
        default:
            return false;
    }
}

std::mutex g_wait_address_mutex;
std::condition_variable g_wait_address_cv;
std::unordered_map<void*, int> g_wait_address_versions;

struct InternalSrwLock {
    std::shared_mutex mutex;
    bool used{false};
};

constexpr std::size_t kMaxSrwLocks = 256;
std::array<InternalSrwLock, kMaxSrwLocks> g_srw_pool{};
std::unordered_map<void*, std::size_t> g_srw_map;
std::mutex g_srw_pool_mutex;

InternalSrwLock* get_or_create_srw(void* ptr) {
    std::lock_guard<std::mutex> lock(g_srw_pool_mutex);
    auto it = g_srw_map.find(ptr);
    if (it != g_srw_map.end()) {
        return &g_srw_pool[it->second];
    }
    for (std::size_t i = 0; i < kMaxSrwLocks; ++i) {
        if (!g_srw_pool[i].used) {
            g_srw_pool[i].used = true;
            g_srw_map[ptr] = i;
            return &g_srw_pool[i];
        }
    }
    return nullptr;
}

struct InternalCondVar {
    std::condition_variable_any cv;
    bool used{false};
};

constexpr std::size_t kMaxCondVars = 256;
std::array<InternalCondVar, kMaxCondVars> g_cond_pool{};
std::unordered_map<void*, std::size_t> g_cond_map;
std::mutex g_cond_pool_mutex;

InternalCondVar* get_or_create_cond(void* ptr) {
    std::lock_guard<std::mutex> lock(g_cond_pool_mutex);
    auto it = g_cond_map.find(ptr);
    if (it != g_cond_map.end()) {
        return &g_cond_pool[it->second];
    }
    for (std::size_t i = 0; i < kMaxCondVars; ++i) {
        if (!g_cond_pool[i].used) {
            g_cond_pool[i].used = true;
            g_cond_map[ptr] = i;
            return &g_cond_pool[i];
        }
    }
    return nullptr;
}

} // namespace

extern "C" {

TL_MSABI void tl_Sleep(const std::uint32_t milliseconds) noexcept {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI std::uint32_t tl_WaitForSingleObject(const void* const handle,
                                              const std::uint32_t milliseconds) noexcept {
    if (handle == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return abi::kWaitFailed;
    }
    FileSlotGuard file_guard(handle);
    if (file_guard.get() != nullptr) {
        set_last_error(abi::kErrorSuccess);
        return abi::kWaitObject0;
    }
    if (file_guard.is_file_handle()) {
        set_last_error(abi::kErrorInvalidHandle);
        return abi::kWaitFailed;
    }
    if (ThreadSlot* thread = find_thread_slot(handle); thread != nullptr) {
        trace_process_console(
            "wait-single-begin", "single-object",
            "kind=thread;handle=" + std::to_string(reinterpret_cast<std::uintptr_t>(handle)) +
                ";timeout-ms=" + std::to_string(milliseconds));
        std::unique_lock<std::mutex> lock(thread->join_mutex);
        const auto predicate = [&]() { return thread->finished; };
        if (milliseconds == abi::kInfinite) {
            thread->finish_cv.wait(lock, predicate);
        } else if (!thread->finish_cv.wait_for(lock, std::chrono::milliseconds(milliseconds), predicate)) {
            set_last_error(abi::kErrorSuccess);
            trace_process_console(
                "wait-single-end", "single-object",
                "kind=thread;handle=" + std::to_string(reinterpret_cast<std::uintptr_t>(handle)) +
                    ";result=timeout");
            return abi::kWaitTimeout;
        }
        set_last_error(abi::kErrorSuccess);
        trace_process_console(
            "wait-single-end", "single-object",
            "kind=thread;handle=" + std::to_string(reinterpret_cast<std::uintptr_t>(handle)) +
                ";result=object-0");
        return abi::kWaitObject0;
    }
    if (SyncSlot* sync = find_sync_slot(handle); sync != nullptr) {
        trace_process_console(
            "wait-single-begin", "single-object",
            "kind=sync;handle=" + std::to_string(reinterpret_cast<std::uintptr_t>(handle)) +
                ";timeout-ms=" + std::to_string(milliseconds));
        const std::uint32_t result = wait_sync_slot(*sync, milliseconds);
        set_last_error(result == abi::kWaitFailed ? abi::kErrorInvalidHandle : abi::kErrorSuccess);
        trace_process_console(
            "wait-single-end", "single-object",
            "kind=sync;handle=" + std::to_string(reinterpret_cast<std::uintptr_t>(handle)) +
                ";result=" + std::to_string(result));
        return result;
    }
    set_last_error(abi::kErrorInvalidHandle);
    return abi::kWaitFailed;
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
        FileSlotGuard file_guard(handles[index]);
        if (handles[index] == nullptr ||
            (file_guard.get() == nullptr &&
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

TL_MSABI void* tl_CreateMutexA(const void* security_attributes, const int initial_owner,
                               const char* name) noexcept {
    if ((security_attributes != nullptr && !mapped_guest_range(security_attributes, sizeof(std::uint32_t), false)) ||
        (name != nullptr && !mapped_guest_cstring(name)) ||
        (initial_owner != 0 && initial_owner != 1)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_sync_mutex);
    auto free_it = std::find_if(g_syncs.begin(), g_syncs.end(),
                                [](const SyncSlot& slot) { return !slot.used; });
    if (free_it == g_syncs.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    SyncSlot& slot = *free_it;
    slot.used = true;
    slot.header = {runtime::HandleObjectType::Sync, 1};
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
    if ((security_attributes != nullptr && !mapped_guest_range(security_attributes, sizeof(std::uint32_t), false)) ||
        (name != nullptr && !mapped_guest_wstring(name)) ||
        (initial_owner != 0 && initial_owner != 1)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    return tl_CreateMutexA(security_attributes, initial_owner, nullptr);
}

TL_MSABI void* tl_CreateEventA(const void* security_attributes, const int manual_reset,
                               const int initial_state, const char* name) noexcept {
    if ((security_attributes != nullptr && !mapped_guest_range(security_attributes, sizeof(std::uint32_t), false)) ||
        (name != nullptr && !mapped_guest_cstring(name)) ||
        (manual_reset != 0 && manual_reset != 1) || (initial_state != 0 && initial_state != 1)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_sync_mutex);
    auto free_it = std::find_if(g_syncs.begin(), g_syncs.end(),
                                [](const SyncSlot& slot) { return !slot.used; });
    if (free_it == g_syncs.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    SyncSlot& slot = *free_it;
    slot.used = true;
    slot.header = {runtime::HandleObjectType::Sync, 1};
    slot.kind = SyncKind::Event;
    slot.signaled = initial_state != 0;
    slot.manual_reset = manual_reset != 0;
    set_last_error(abi::kErrorSuccess);
    void* const handle = sync_slot_handle(slot);
    trace_process_console(
        "event-create", "event-object",
        "handle=" + std::to_string(reinterpret_cast<std::uintptr_t>(handle)) +
            ";manual-reset=" + std::to_string(manual_reset) +
            ";initial-state=" + std::to_string(initial_state));
    return handle;
}

TL_MSABI void* tl_CreateEventW(const void* security_attributes, const int manual_reset,
                               const int initial_state, const std::uint16_t* name) noexcept {
    if ((security_attributes != nullptr && !mapped_guest_range(security_attributes, sizeof(std::uint32_t), false)) ||
        (name != nullptr && !mapped_guest_wstring(name)) ||
        (manual_reset != 0 && manual_reset != 1) || (initial_state != 0 && initial_state != 1)) {
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
    trace_process_console(
        "event-set", "event-object",
        "handle=" + std::to_string(reinterpret_cast<std::uintptr_t>(event_handle)));
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
    if ((security_attributes != nullptr && !mapped_guest_range(security_attributes, sizeof(std::uint32_t), false)) ||
        (name != nullptr && !mapped_guest_cstring(name)) ||
        initial_count < 0 || maximum_count <= 0 || initial_count > maximum_count) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_sync_mutex);
    auto free_it = std::find_if(g_syncs.begin(), g_syncs.end(),
                                [](const SyncSlot& slot) { return !slot.used; });
    if (free_it == g_syncs.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    SyncSlot& slot = *free_it;
    slot.used = true;
    slot.header = {runtime::HandleObjectType::Sync, 1};
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

TL_MSABI void tl_InitializeCriticalSection(void* critical_section) noexcept {
    if (critical_section == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    alloc_cs_entry(critical_section);
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI int tl_InitializeCriticalSectionAndSpinCount(void* critical_section,
                                                      std::uint32_t spin_count) noexcept {
    (void)spin_count;
    if (critical_section == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    tl_InitializeCriticalSection(critical_section);
    return 1;
}

TL_MSABI int tl_InitializeCriticalSectionEx(void* critical_section,
                                             std::uint32_t spin_count,
                                             std::uint32_t flags) noexcept {
    (void)spin_count;
    if (critical_section == nullptr ||
        (flags & ~abi::kCriticalSectionNoDebugInfo) != 0U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    tl_InitializeCriticalSection(critical_section);
    return 1;
}

TL_MSABI void tl_EnterCriticalSection(void* critical_section) noexcept {
    if (critical_section == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    CriticalSectionEntry* entry = find_cs_entry(critical_section);
    if (entry == nullptr) {
        entry = alloc_cs_entry(critical_section);
    }
    if (entry != nullptr) {
        pthread_mutex_lock(&entry->mutex);
        set_last_error(abi::kErrorSuccess);
        return;
    }
    set_last_error(abi::kErrorInvalidParameter);
}

TL_MSABI void tl_LeaveCriticalSection(void* critical_section) noexcept {
    if (critical_section == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    CriticalSectionEntry* entry = find_cs_entry(critical_section);
    if (entry != nullptr) {
        pthread_mutex_unlock(&entry->mutex);
        set_last_error(abi::kErrorSuccess);
        return;
    }
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI void tl_DeleteCriticalSection(void* critical_section) noexcept {
    if (critical_section == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    std::lock_guard<std::mutex> lock(g_cs_mutex);
    auto it = std::find_if(g_critical_sections.begin(), g_critical_sections.end(),
                           [critical_section](const CriticalSectionEntry& e) {
                               return e.used && e.guest_address == critical_section;
                           });
    if (it != g_critical_sections.end()) {
        pthread_mutex_destroy(&it->mutex);
        *it = {};
    }
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI int tl_TryEnterCriticalSection(void* critical_section) noexcept {
    if (critical_section == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    CriticalSectionEntry* entry = find_cs_entry(critical_section);
    if (entry == nullptr) {
        entry = alloc_cs_entry(critical_section);
    }
    if (entry == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const int rc = pthread_mutex_trylock(&entry->mutex);
    set_last_error(abi::kErrorSuccess);
    return rc == 0 ? 1 : 0;
}

TL_MSABI int tl_WaitOnAddress(void* address, void* compare_address, std::size_t address_size,
                              std::uint32_t milliseconds) noexcept {
    if (address == nullptr || compare_address == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (address_size != 1 && address_size != 2 && address_size != 4 && address_size != 8) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if ((reinterpret_cast<std::uintptr_t>(address) % address_size) != 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(address, address_size, false) ||
        !mapped_guest_range(compare_address, address_size, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (std::memcmp(address, compare_address, address_size) != 0) {
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    // Valor igual: precisa esperar
    std::unique_lock<std::mutex> lock(g_wait_address_mutex);
    int& version = g_wait_address_versions[address]; // cria se não existe
    int start_version = version;
    auto pred = [&]() {
        if (version != start_version) return true;
        // Se o valor na memória mudou, também acorda
        // Memcmp dentro do lock pode ler memória guest que outra thread modifica sem lock;
        // mas a condição de versão já cobre Wake.
        return std::memcmp(address, compare_address, address_size) != 0;
    };
    // Checa pred antes para evitar wait desnecessário se já mudou entre primeiro memcmp e lock
    if (pred()) {
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    if (milliseconds == abi::kInfinite) {
        g_wait_address_cv.wait(lock, pred);
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    if (!g_wait_address_cv.wait_for(lock, std::chrono::milliseconds(milliseconds), pred)) {
        set_last_error(abi::kErrorTimeout);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void tl_WakeByAddressSingle(void* address) noexcept {
    if (address == nullptr) return;
    {
        std::lock_guard<std::mutex> lock(g_wait_address_mutex);
        auto it = g_wait_address_versions.find(address);
        if (it != g_wait_address_versions.end()) {
            it->second++;
        } else {
            g_wait_address_versions[address] = 1;
        }
    }
    g_wait_address_cv.notify_one();
}

TL_MSABI void tl_WakeByAddressAll(void* address) noexcept {
    if (address == nullptr) return;
    {
        std::lock_guard<std::mutex> lock(g_wait_address_mutex);
        auto it = g_wait_address_versions.find(address);
        if (it != g_wait_address_versions.end()) {
            it->second++;
        } else {
            g_wait_address_versions[address] = 1;
        }
    }
    g_wait_address_cv.notify_all();
}

TL_MSABI void tl_InitializeSRWLock(void* srw_lock) noexcept {
    if (srw_lock != nullptr && mapped_guest_range(srw_lock, sizeof(void*), true)) {
        *reinterpret_cast<void**>(srw_lock) = nullptr;
    }
}

TL_MSABI void tl_AcquireSRWLockExclusive(void* srw_lock) noexcept {
    if (auto* lock = get_or_create_srw(srw_lock)) {
        lock->mutex.lock();
    }
}

TL_MSABI void tl_ReleaseSRWLockExclusive(void* srw_lock) noexcept {
    if (auto* lock = get_or_create_srw(srw_lock)) {
        lock->mutex.unlock();
    }
}

TL_MSABI void tl_AcquireSRWLockShared(void* srw_lock) noexcept {
    if (auto* lock = get_or_create_srw(srw_lock)) {
        lock->mutex.lock_shared();
    }
}

TL_MSABI void tl_ReleaseSRWLockShared(void* srw_lock) noexcept {
    if (auto* lock = get_or_create_srw(srw_lock)) {
        lock->mutex.unlock_shared();
    }
}

TL_MSABI int tl_SleepConditionVariableSRW(void* cond, void* srw_lock,
                                          const std::uint32_t milliseconds, const std::uint32_t flags) noexcept {
    (void)flags;
    auto* cv = get_or_create_cond(cond);
    auto* lock = get_or_create_srw(srw_lock);
    if (cv == nullptr || lock == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    bool ok = true;
    if (milliseconds == abi::kInfinite) {
        cv->cv.wait(lock->mutex);
    } else {
        auto status = cv->cv.wait_for(lock->mutex, std::chrono::milliseconds(milliseconds));
        if (status == std::cv_status::timeout) {
            set_last_error(abi::kErrorTimeout);
            ok = false;
        }
    }
    if (ok) {
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    return 0;
}

TL_MSABI void tl_WakeConditionVariable(void* cond) noexcept {
    if (auto* cv = get_or_create_cond(cond)) {
        cv->cv.notify_one();
    }
}

TL_MSABI void tl_WakeAllConditionVariable(void* cond) noexcept {
    if (auto* cv = get_or_create_cond(cond)) {
        cv->cv.notify_all();
    }
}

TL_MSABI void* tl_OpenEventW(const std::uint32_t desired_access, const int inherit_handle,
                             const std::uint16_t* const name) noexcept {
    (void)desired_access;
    (void)inherit_handle;
    (void)name;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x1000);
}

TL_MSABI std::uint32_t tl_WaitForSingleObjectEx(void* const handle, const std::uint32_t milliseconds,
                                                const int alertable) noexcept {
    (void)alertable;
    return tl_WaitForSingleObject(handle, milliseconds);
}

TL_MSABI int tl_TryAcquireSRWLockExclusive(void* const srw_lock) noexcept {
    if (srw_lock == nullptr) {
        return 1;
    }
    if (auto* lock = get_or_create_srw(srw_lock)) {
        return lock->mutex.try_lock() ? 1 : 0;
    }
    return 1;
}

TL_MSABI int tl_UnregisterWaitEx(void* const wait_handle, void* const completion_event) noexcept {
    (void)wait_handle;
    (void)completion_event;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_RegisterWaitForSingleObject(void** const ph_new_wait_object, void* const h_object,
                                            void* const callback, void* const context,
                                            const std::uint32_t ms, const std::uint32_t flags) noexcept {
    (void)h_object;
    (void)callback;
    (void)context;
    (void)ms;
    (void)flags;
    if (ph_new_wait_object != nullptr && mapped_guest_range(ph_new_wait_object, sizeof(void*), true)) {
        *ph_new_wait_object = reinterpret_cast<void*>(0x12340001ULL);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_SleepEx(const std::uint32_t milliseconds, const int alertable) noexcept {
    (void)alertable;
    tl_Sleep(milliseconds);
    return 0;
}

TL_MSABI std::uint32_t tl_WaitForMultipleObjectsEx(const std::uint32_t count, const void* const* const handles,
                                                  const int wait_all, const std::uint32_t milliseconds, const int alertable) noexcept {
    (void)alertable;
    return tl_WaitForMultipleObjects(count, handles, wait_all, milliseconds);
}

TL_MSABI void tl_InitializeConditionVariable(void* const condition_variable) noexcept {
    if (condition_variable != nullptr && mapped_guest_range(condition_variable, sizeof(void*), true)) {
        *reinterpret_cast<void**>(condition_variable) = nullptr;
    }
}

TL_MSABI int tl_SleepConditionVariableCS(void* const condition_variable, void* const critical_section, const std::uint32_t milliseconds) noexcept {
    (void)condition_variable;
    if (critical_section != nullptr) {
        tl_LeaveCriticalSection(critical_section);
        if (milliseconds != 0 && milliseconds != abi::kInfinite) {
            tl_Sleep(std::min<std::uint32_t>(milliseconds, 10));
        }
        tl_EnterCriticalSection(critical_section);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreateWaitableTimerA(void* const timer_attributes, const int manual_reset, const char* const timer_name) noexcept {
    (void)timer_attributes;
    (void)manual_reset;
    (void)timer_name;
    return tl_CreateEventW(nullptr, manual_reset, 0, nullptr);
}

TL_MSABI void* tl_CreateWaitableTimerW(void* const timer_attributes, const int manual_reset, const wchar_t* const timer_name) noexcept {
    (void)timer_attributes;
    (void)manual_reset;
    (void)timer_name;
    return tl_CreateEventW(nullptr, manual_reset, 0, nullptr);
}

TL_MSABI int tl_SetWaitableTimer(void* const timer, const std::int64_t* const due_time, const std::int32_t period,
                                 void* const completion_routine, void* const arg_to_completion_routine, const int resume) noexcept {
    (void)timer;
    (void)due_time;
    (void)period;
    (void)completion_routine;
    (void)arg_to_completion_routine;
    (void)resume;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_CancelWaitableTimer(void* const timer) noexcept {
    (void)timer;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreateSemaphoreExW(void* const semaphore_attributes, const std::int32_t initial_count,
                                     const std::int32_t maximum_count, const wchar_t* const name,
                                     const std::uint32_t flags, const std::uint32_t desired_access) noexcept {
    (void)flags;
    (void)desired_access;
    return tl_CreateSemaphoreW(semaphore_attributes, initial_count, maximum_count, reinterpret_cast<const std::uint16_t*>(name));
}

TL_MSABI void* tl_OpenSemaphoreW(const std::uint32_t desired_access, const int inherit_handle, const wchar_t* const name) noexcept {
    (void)desired_access;
    (void)inherit_handle;
    return tl_CreateSemaphoreW(nullptr, 1, 1, reinterpret_cast<const std::uint16_t*>(name));
}

TL_MSABI void* tl_CreateMutexExW(void* const mutex_attributes, const wchar_t* const name,
                                 const std::uint32_t flags, const std::uint32_t desired_access) noexcept {
    (void)flags;
    (void)desired_access;
    return tl_CreateMutexW(mutex_attributes, 0, reinterpret_cast<const std::uint16_t*>(name));
}

TL_MSABI int tl_InitOnceBeginInitialize(void* const init_once, const std::uint32_t flags, int* const pending, void** const context) noexcept {
    if (init_once == nullptr || !mapped_guest_range(init_once, sizeof(void*), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    auto* const ptr = static_cast<std::uintptr_t*>(init_once);
    if ((flags & 1U) != 0U) { // INIT_ONCE_CHECK_ONLY
        if (*ptr == 2U) {
            if (pending != nullptr && mapped_guest_range(pending, sizeof(int), true)) *pending = 0;
            if (context != nullptr && mapped_guest_range(context, sizeof(void*), true)) *context = nullptr;
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
        set_last_error(1067 /* ERROR_GEN_FAILURE */);
        return 0;
    }
    if (*ptr == 2U) {
        if (pending != nullptr && mapped_guest_range(pending, sizeof(int), true)) *pending = 0;
        if (context != nullptr && mapped_guest_range(context, sizeof(void*), true)) *context = nullptr;
    } else {
        if (pending != nullptr && mapped_guest_range(pending, sizeof(int), true)) *pending = 1;
        if (context != nullptr && mapped_guest_range(context, sizeof(void*), true)) *context = nullptr;
        *ptr = 1U;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_InitOnceComplete(void* const init_once, const std::uint32_t flags, void* const context) noexcept {
    (void)flags;
    (void)context;
    if (init_once != nullptr && mapped_guest_range(init_once, sizeof(void*), true)) {
        *static_cast<std::uintptr_t*>(init_once) = 2U;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

}  // extern "C"
}  // namespace tradutorlinux
