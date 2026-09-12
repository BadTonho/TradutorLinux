#include "kernel32_internal.hpp"
namespace tradutorlinux {

extern "C" std::uint32_t tl_call_guest_thread_on_stack(std::uintptr_t entry,
                                                       const void* parameter,
                                                       std::uintptr_t stack_top) noexcept;

namespace {

thread_local std::jmp_buf* t_thread_exit_context = nullptr;
thread_local ThreadSlot* t_thread_exit_slot = nullptr;

void clear_unstarted_thread_slot(ThreadSlot& slot) noexcept {
    if (slot.teb != nullptr) {
        free_guest_teb(slot.teb);
    }
    if (slot.stack != nullptr && slot.stack_size > 0) {
        munmap(slot.stack, slot.stack_size);
    }
    slot.header = {};
    slot.used = false;
    slot.thread_id = 0;
    slot.teb = nullptr;
    slot.stack = nullptr;
    slot.stack_size = 0;
    slot.stack_top = 0;
    slot.unwind_view = {};
    slot.fls_values.reset();
    slot.thread_func = {};
    slot.finished = false;
    slot.joined = false;
    slot.handle_closed = false;
    slot.exit_code = 0;
    runtime::invalidate_memory_map_cache();
}

void trace_fls(const char* const operation, const std::string& detail) noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", operation},
        diagnostics::TraceField{"detail", detail},
        diagnostics::TraceField{"thread", std::to_string(g_current_thread_id)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("fls", fields, 4);
}

constexpr std::uint32_t kTlsMinimumAvailable = 64;
bool tls_index_allocated(const std::uint32_t tls_index) noexcept {
    return tls_index < kTlsMinimumAvailable && g_tls_indices_used[tls_index];
}

struct InternalTpWork {
    bool used{false};
    void* callback{nullptr};
    void* context{nullptr};
};
std::array<InternalTpWork, 32> g_tp_works{};

struct InternalTpTimer {
    bool used{false};
    void* callback{nullptr};
    void* context{nullptr};
};
std::array<InternalTpTimer, 32> g_tp_timers{};

thread_local void* g_current_fiber_data = nullptr;

} // namespace

std::shared_ptr<FlsThreadValues> ensure_fls_thread_values() {
    std::lock_guard lock(g_fls_mutex);
    if (g_current_fls_values == nullptr) {
        g_current_fls_values = std::make_shared<FlsThreadValues>();
        g_fls_threads.push_back(g_current_fls_values);
    }
    return g_current_fls_values;
}

void set_current_fls_thread_values(std::shared_ptr<FlsThreadValues> values) {
    std::lock_guard lock(g_fls_mutex);
    g_current_fls_values = std::move(values);
    if (g_current_fls_values != nullptr) {
        g_fls_threads.push_back(g_current_fls_values);
    }
}

void cleanup_current_fls_values() noexcept {
    std::vector<std::pair<std::uintptr_t, void*>> callbacks;
    {
        std::lock_guard lock(g_fls_mutex);
        if (g_current_fls_values == nullptr) {
            return;
        }
        for (std::size_t index = 0; index < g_fls_slots.size(); ++index) {
            const FlsSlot& slot = g_fls_slots[index];
            void*& value = g_current_fls_values->values[index];
            if (slot.used && slot.callback != 0 && value != nullptr) {
                callbacks.emplace_back(slot.callback, value);
                value = nullptr;
            }
        }
    }
    using FlsCallback = void (TL_MSABI *)(void*);
    for (const auto& [callback_address, value] : callbacks) {
        const auto callback = reinterpret_cast<FlsCallback>(callback_address);
        callback(value);
        trace_fls("callback", "thread-exit");
    }
}

void reset_fls_process_state() noexcept {
    std::lock_guard lock(g_fls_mutex);
    g_fls_slots = {};
    g_fls_threads.clear();
    g_current_fls_values.reset();
}

extern "C" {

TL_MSABI void tl_InitializeSListHead(abi::GuestSListHeader* const list_head) noexcept {
    if (list_head == nullptr ||
        reinterpret_cast<std::uintptr_t>(list_head) % alignof(abi::GuestSListHeader) != 0 ||
        !mapped_guest_range(list_head, sizeof(*list_head), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    *list_head = {};
    set_last_error(abi::kErrorSuccess);
    trace_process_console("process-context", "initialize-slist", "empty");
}

TL_MSABI void* tl_CreateThread(const void* thread_attributes, const std::uintptr_t stack_size,
                                const std::uintptr_t start_address, void* const parameter,
                                const std::uint32_t creation_flags,
                                std::uint32_t* thread_id) noexcept {
    (void)thread_attributes;
    if (start_address == 0 ||
        !mapped_guest_range(reinterpret_cast<const void*>(start_address), 1, false) ||
        (creation_flags != 0 && creation_flags != 0x00000004)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_threads_mutex);
    auto it = std::find_if(g_threads.begin(), g_threads.end(), [](const ThreadSlot& s) { return !s.used; });
    if (it == g_threads.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    const std::size_t real_stack_size = std::max<std::size_t>(
        stack_size > 0 ? static_cast<std::size_t>(stack_size) : 0x1000000U, 0x1000000U);
    void* stack = mmap(nullptr, real_stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    const std::uintptr_t stack_top = reinterpret_cast<std::uintptr_t>(stack) + real_stack_size;
    const std::uint32_t new_tid = g_next_thread_id.fetch_add(1);
    void* teb = allocate_guest_teb(stack_top, real_stack_size, new_tid);
    if (teb == nullptr) {
        munmap(stack, real_stack_size);
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    it->used = true;
    it->header = {runtime::HandleObjectType::Thread, 1};
    it->thread_id = new_tid;
    it->teb = teb;
    it->stack = static_cast<std::byte*>(stack);
    it->stack_size = real_stack_size;
    it->stack_top = stack_top;
    it->unwind_view = runtime::current_guest_unwind_view();
    it->finished = false;
    it->joined = false;
    it->handle_closed = false;
    it->exit_code = 0;
    // A pilha e o TEB novos alteram o mapa de memória visível ao validador.
    runtime::invalidate_memory_map_cache();
    using ThreadProc = TL_MSABI std::uint32_t (*)(const void*);
    auto proc = reinterpret_cast<ThreadProc>(start_address);
    runtime::GuestContext* const guest_context = &runtime::guest_context();
    try {
        it->fls_values = std::make_shared<FlsThreadValues>();
        it->host_thread = std::thread([slot_ptr = &*it, proc, parameter, teb, guest_context,
                                       unwind_view = it->unwind_view]() {
            // O contexto ativo é thread-local. Uma thread host nova começa no
            // contexto padrão, portanto precisa herdar explicitamente o contexto
            // do processo convidado antes de consultar imagem, TLS, handles ou
            // qualquer outra tabela pertencente à execução.
            runtime::GuestContextScope context_scope(*guest_context);
            g_current_thread_id = slot_ptr->thread_id;
            trace_process_console("thread-start", "guest-thread",
                                  "thread-id=" + std::to_string(slot_ptr->thread_id));
            set_guest_gs_base(teb);
            initialize_thread_tls(static_cast<runtime::GuestTeb*>(teb));
            set_current_fls_thread_values(slot_ptr->fls_values);
            runtime::restore_guest_unwind_view(unwind_view);
            invoke_thread_tls_callbacks(2U /* DLL_THREAD_ATTACH */);
            initialize_pointer_backed_tls_slot(static_cast<runtime::GuestTeb*>(teb));
            std::jmp_buf exit_point{};
            t_thread_exit_context = &exit_point;
            t_thread_exit_slot = slot_ptr;
            if (setjmp(exit_point) == 0) {
                diagnostics::FunctionTraceScope assembly_scope{"tl_call_guest_thread_on_stack"};
                slot_ptr->exit_code = static_cast<int>(tl_call_guest_thread_on_stack(
                    reinterpret_cast<std::uintptr_t>(proc), parameter, slot_ptr->stack_top));
            }
            invoke_thread_tls_callbacks(3U /* DLL_THREAD_DETACH */);
            // O bloco pointer-backed pertence à vida útil da thread convidada,
            // não à vida útil posterior do handle retornado por CreateThread.
            free_tls_dynamic_blocks(teb);
            // Após longjmp, ler o slot pelo TLS (não depender de registradores).
            ThreadSlot* const finished_slot = t_thread_exit_slot;
            t_thread_exit_context = nullptr;
            t_thread_exit_slot = nullptr;
            cleanup_current_fls_values();
            set_current_fls_thread_values({});
            runtime::clear_guest_unwind_view();
            set_guest_gs_base(nullptr);
            bool should_cleanup = false;
            std::uint32_t completed_thread_id = 0;
            int completed_exit_code = 0;
            {
                std::lock_guard<std::mutex> join_lock(finished_slot->join_mutex);
                std::lock_guard<std::mutex> threads_lock(g_threads_mutex);
                finished_slot->finished = true;
                completed_thread_id = finished_slot->thread_id;
                completed_exit_code = finished_slot->exit_code;
                if (finished_slot->handle_closed && !finished_slot->joined) {
                    finished_slot->joined = true;
                    should_cleanup = true;
                }
            }
            finished_slot->finish_cv.notify_all();
            trace_process_console(
                "thread-exit", "guest-thread",
                "thread-id=" + std::to_string(completed_thread_id) +
                    ";exit-code=" + std::to_string(completed_exit_code));
            if (should_cleanup) {
                if (finished_slot->host_thread.joinable()) {
                    finished_slot->host_thread.detach();
                }
                std::lock_guard<std::mutex> threads_lock(g_threads_mutex);
                if (finished_slot->teb != nullptr) {
                    free_guest_teb(finished_slot->teb);
                }
                if (finished_slot->stack != nullptr && finished_slot->stack_size > 0) {
                    munmap(finished_slot->stack, finished_slot->stack_size);
                }
                finished_slot->used = false;
                finished_slot->thread_id = 0;
                finished_slot->teb = nullptr;
                finished_slot->stack = nullptr;
                finished_slot->stack_size = 0;
                finished_slot->stack_top = 0;
                finished_slot->thread_func = {};
                finished_slot->finished = false;
                finished_slot->joined = false;
                finished_slot->handle_closed = false;
                finished_slot->exit_code = 0;
                runtime::invalidate_memory_map_cache();
            }
        });
    } catch (...) {
        clear_unstarted_thread_slot(*it);
        trace_guest_failure("CreateThread", "host-resource", "thread creation failed");
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    if (thread_id != nullptr) {
        *thread_id = it->thread_id;
    }
    trace_process_console(
        "thread-create", "guest-thread",
        "thread-id=" + std::to_string(it->thread_id) +
            ";start-address=" + std::to_string(start_address) +
            ";creation-flags=" + std::to_string(creation_flags));
    set_last_error(abi::kErrorSuccess);
    return thread_slot_to_handle(*it);
}

TL_MSABI void tl_ExitThread(std::uint32_t exit_code) noexcept {
    if (std::jmp_buf* context = t_thread_exit_context; context != nullptr) {
        if (ThreadSlot* slot = t_thread_exit_slot; slot != nullptr) {
            slot->exit_code = static_cast<int>(exit_code);
        }
        cleanup_current_fls_values();
        guest_longjmp(*context, 1);
    }
    // Thread primária: encerrar a última thread encerra o processo.
    tl_ExitProcess(exit_code);
}

TL_MSABI std::uint32_t tl_GetCurrentThreadId() noexcept {
    return g_current_thread_id;
}

TL_MSABI std::uint32_t tl_TlsAlloc() noexcept {
    std::lock_guard<std::mutex> lock(g_tls_mutex);
    for (std::uint32_t i = 0; i < kTlsMinimumAvailable; ++i) {
        if (!g_tls_indices_used[i]) {
            g_tls_indices_used[i] = true;
            set_last_error(abi::kErrorSuccess);
            return i;
        }
    }
    set_last_error(abi::kErrorNotEnoughMemory);
    return 0xFFFFFFFFU;
}

TL_MSABI void* tl_TlsGetValue(std::uint32_t tls_index) noexcept {
    if (tls_index >= kTlsMinimumAvailable) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    if (g_current_teb != nullptr && tls_index < 64) {
        return reinterpret_cast<void*>(g_current_teb->tls_slots[tls_index]);
    }
    return g_guest_tls_slots[tls_index];
}

TL_MSABI int tl_TlsSetValue(std::uint32_t tls_index, void* value) noexcept {
    {
        std::lock_guard<std::mutex> lock(g_tls_mutex);
        if (!tls_index_allocated(tls_index)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    g_guest_tls_slots[tls_index] = value;
    if (g_current_teb != nullptr && tls_index < 64) {
        g_current_teb->tls_slots[tls_index] = reinterpret_cast<std::uint64_t>(value);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_TlsFree(std::uint32_t tls_index) noexcept {
    std::lock_guard<std::mutex> lock(g_tls_mutex);
    if (!tls_index_allocated(tls_index)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    g_tls_indices_used[tls_index] = false;
    g_guest_tls_slots[tls_index] = nullptr;
    if (g_current_teb != nullptr && tls_index < 64) {
        g_current_teb->tls_slots[tls_index] = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_FlsAlloc(const std::uintptr_t callback) noexcept {
    if (callback != 0 && !is_guest_executable_address(callback)) {
        set_last_error(abi::kErrorInvalidParameter);
        return abi::kFlsOutOfIndexes;
    }
    std::lock_guard lock(g_fls_mutex);
    for (std::uint32_t index = 0; index < kMaxFlsSlots; ++index) {
        if (!g_fls_slots[index].used) {
            g_fls_slots[index] = {.used = true, .callback = callback};
            set_last_error(abi::kErrorSuccess);
            trace_fls("alloc", std::to_string(index));
            return index;
        }
    }
    set_last_error(abi::kErrorNotEnoughMemory);
    return abi::kFlsOutOfIndexes;
}

TL_MSABI void* tl_FlsGetValue(const std::uint32_t fls_index) noexcept {
    std::lock_guard lock(g_fls_mutex);
    if (fls_index >= kMaxFlsSlots || !g_fls_slots[fls_index].used) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (g_current_fls_values == nullptr) {
        g_current_fls_values = std::make_shared<FlsThreadValues>();
        g_fls_threads.push_back(g_current_fls_values);
    }
    set_last_error(abi::kErrorSuccess);
    return g_current_fls_values->values[fls_index];
}

TL_MSABI int tl_FlsSetValue(const std::uint32_t fls_index, void* const value) noexcept {
    std::lock_guard lock(g_fls_mutex);
    if (fls_index >= kMaxFlsSlots || !g_fls_slots[fls_index].used) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (g_current_fls_values == nullptr) {
        g_current_fls_values = std::make_shared<FlsThreadValues>();
        g_fls_threads.push_back(g_current_fls_values);
    }
    g_current_fls_values->values[fls_index] = value;
    set_last_error(abi::kErrorSuccess);
    trace_fls("set", std::to_string(fls_index));
    return 1;
}

TL_MSABI int tl_FlsFree(const std::uint32_t fls_index) noexcept {
    std::vector<std::pair<std::uintptr_t, void*>> callbacks;
    {
        std::lock_guard lock(g_fls_mutex);
        if (fls_index >= kMaxFlsSlots || !g_fls_slots[fls_index].used) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        const std::uintptr_t callback = g_fls_slots[fls_index].callback;
        g_fls_slots[fls_index] = {};
        std::vector<std::weak_ptr<FlsThreadValues>> live_threads;
        live_threads.reserve(g_fls_threads.size());
        for (const std::weak_ptr<FlsThreadValues>& weak_values : g_fls_threads) {
            const std::shared_ptr<FlsThreadValues> values = weak_values.lock();
            if (values == nullptr) {
                continue;
            }
            live_threads.push_back(values);
            if (callback != 0 && values->values[fls_index] != nullptr) {
                callbacks.emplace_back(callback, values->values[fls_index]);
                values->values[fls_index] = nullptr;
            }
        }
        g_fls_threads = std::move(live_threads);
    }
    using FlsCallback = void (TL_MSABI *)(void*);
    for (const auto& [callback_address, value] : callbacks) {
        reinterpret_cast<FlsCallback>(callback_address)(value);
        trace_fls("callback", "free");
    }
    set_last_error(abi::kErrorSuccess);
    trace_fls("free", std::to_string(fls_index));
    return 1;
}

TL_MSABI std::uintptr_t tl_SetUnhandledExceptionFilter(std::uintptr_t top_level_filter) noexcept {
    const std::uintptr_t previous =
        g_unhandled_exception_filter.exchange(top_level_filter, std::memory_order_acq_rel);
    set_last_error(abi::kErrorSuccess);
    return previous;
}

TL_MSABI void* tl_AddVectoredExceptionHandler(const std::uint32_t first, void* handler) noexcept {
    return runtime::add_vectored_exception_handler(first, handler);
}

TL_MSABI std::uint32_t tl_RemoveVectoredExceptionHandler(void* handle) noexcept {
    return runtime::remove_vectored_exception_handler(handle);
}

TL_MSABI void* tl_CreateThreadpoolWork(void* callback, void* context, void* environment) noexcept {
    (void)environment;
    for (auto& w : g_tp_works) {
        if (!w.used) {
            w.used = true;
            w.callback = callback;
            w.context = context;
            return &w;
        }
    }
    return nullptr;
}

TL_MSABI void tl_SubmitThreadpoolWork(void* work) noexcept {
    if (work == nullptr) return;
    auto* w = static_cast<InternalTpWork*>(work);
    if (!w->used || w->callback == nullptr) return;
    std::thread([cb = w->callback, ctx = w->context]() {
        using CallbackFn = void (*)(void*, void*, void*);
        reinterpret_cast<CallbackFn>(cb)(nullptr, ctx, nullptr);
    }).detach();
}

TL_MSABI void tl_WaitForThreadpoolWorkCallbacks(void* work, const int cancel_pending) noexcept {
    (void)work;
    (void)cancel_pending;
}

TL_MSABI void tl_CloseThreadpoolWork(void* work) noexcept {
    if (work != nullptr) {
        static_cast<InternalTpWork*>(work)->used = false;
    }
}

TL_MSABI void* tl_CreateThreadpoolTimer(void* callback, void* context, void* environment) noexcept {
    (void)environment;
    for (auto& t : g_tp_timers) {
        if (!t.used) {
            t.used = true;
            t.callback = callback;
            t.context = context;
            return &t;
        }
    }
    return nullptr;
}

TL_MSABI void tl_SetThreadpoolTimer(void* timer, const void* due_time, const std::uint32_t period, const std::uint32_t window_length) noexcept {
    (void)timer;
    (void)due_time;
    (void)period;
    (void)window_length;
}

TL_MSABI void tl_WaitForThreadpoolTimerCallbacks(void* timer, const int cancel_pending) noexcept {
    (void)timer;
    (void)cancel_pending;
}

TL_MSABI void tl_CloseThreadpoolTimer(void* timer) noexcept {
    if (timer != nullptr) {
        static_cast<InternalTpTimer*>(timer)->used = false;
    }
}

TL_MSABI void* tl_ConvertThreadToFiber(void* parameter) noexcept {
    g_current_fiber_data = parameter;
    static char g_fiber_token = 0;
    return &g_fiber_token;
}

TL_MSABI void* tl_ConvertThreadToFiberEx(void* parameter, std::uint32_t flags) noexcept {
    (void)flags;
    g_current_fiber_data = parameter;
    static char g_fiber_ex_token = 0;
    return &g_fiber_ex_token;
}

TL_MSABI int tl_ConvertFiberToThread() noexcept {
    g_current_fiber_data = nullptr;
    return 1;
}

TL_MSABI void* tl_CreateFiber(const std::size_t stack_size, void* start_address, void* parameter) noexcept {
    (void)stack_size;
    (void)start_address;
    (void)parameter;
    static char g_fiber_created_token = 0;
    return &g_fiber_created_token;
}

TL_MSABI void* tl_CreateFiberEx(const std::size_t stack_commit, const std::size_t stack_reserve,
                                const std::uint32_t flags, void* start_address, void* parameter) noexcept {
    (void)stack_commit;
    (void)stack_reserve;
    (void)flags;
    (void)start_address;
    (void)parameter;
    static char g_fiber_ex_created_token = 0;
    return &g_fiber_ex_created_token;
}

TL_MSABI void tl_SwitchToFiber(void* fiber) noexcept {
    (void)fiber;
}

TL_MSABI void tl_DeleteFiber(void* fiber) noexcept {
    (void)fiber;
}

TL_MSABI void* tl_GetFiberData() noexcept {
    return g_current_fiber_data;
}

TL_MSABI int tl_SetThreadPriority(const void* const thread_handle, const int priority) noexcept {
    (void)thread_handle;
    (void)priority;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_SetThreadExecutionState(const std::uint32_t es_flags) noexcept {
    return es_flags;
}

TL_MSABI std::uintptr_t tl_SetThreadAffinityMask(void* const thread, const std::uintptr_t thread_affinity_mask) noexcept {
    (void)thread;
    (void)thread_affinity_mask;
    set_last_error(abi::kErrorSuccess);
    return 1; // previous mask
}

TL_MSABI std::uint32_t tl_ResumeThread(void* const thread) noexcept {
    (void)thread;
    set_last_error(abi::kErrorSuccess);
    return 0; // previous suspend count
}

TL_MSABI int tl_GetExitCodeThread(void* const thread, std::uint32_t* const exit_code) noexcept {
    if (exit_code == nullptr || !mapped_guest_range(exit_code, 4, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    constexpr std::uint32_t kStillActive = 259;
    if (thread == nullptr || thread == reinterpret_cast<void*>(~static_cast<std::uintptr_t>(1))) {
        // (HANDLE)-2: GetCurrentThread() ou handle nulo default
        *exit_code = kStillActive;
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    ThreadSlot* slot = find_thread_slot(thread);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_threads_mutex);
    if (!slot->finished) {
        *exit_code = kStillActive;
    } else {
        *exit_code = static_cast<std::uint32_t>(slot->exit_code);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetThreadLocale(const std::uint32_t locale) noexcept {
    (void)locale;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint16_t tl_SetThreadUILanguage(const std::uint16_t lang_id) noexcept {
    return lang_id;
}

TL_MSABI void* tl_InterlockedPushEntrySList(void* const list_head, void* const list_entry) noexcept {
    if (list_head == nullptr || list_entry == nullptr) {
        return nullptr;
    }
    // Minimal atomic-compatible SLIST emulation for single guest context
    void** entry_next = static_cast<void**>(list_entry);
    void** head_ptr = static_cast<void**>(list_head);
    void* old_head = *head_ptr;
    *entry_next = old_head;
    *head_ptr = list_entry;
    return old_head;
}

TL_MSABI int tl_InitializeProcThreadAttributeList(void* const attribute_list, const std::uint32_t attribute_count,
                                                  const std::uint32_t flags, std::size_t* const size) noexcept {
    (void)attribute_count;
    (void)flags;
    if (size == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (attribute_list == nullptr) {
        *size = 64;
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    std::memset(attribute_list, 0, *size);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_UpdateProcThreadAttribute(void* const attribute_list, const std::uint32_t flags,
                                          const std::uintptr_t attribute, void* const value,
                                          const std::size_t size, void* const previous_value,
                                          std::size_t* const return_size) noexcept {
    (void)attribute_list;
    (void)flags;
    (void)attribute;
    (void)value;
    (void)size;
    (void)previous_value;
    (void)return_size;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::int32_t tl_SetThreadDescription(void* const thread, const wchar_t* const description) noexcept {
    (void)thread;
    (void)description;
    return 0; // S_OK
}

TL_MSABI void* tl_GetCurrentThread() noexcept {
    return reinterpret_cast<void*>(~static_cast<std::uintptr_t>(1)); // (HANDLE)-2
}

TL_MSABI int tl_SwitchToThread() noexcept {
    sched_yield();
    return 1;
}

TL_MSABI int tl_GetThreadTimes(void* const thread, void* const creation_time, void* const exit_time, void* const kernel_time, void* const user_time) noexcept {
    (void)thread;
    const std::uint64_t dummy_ft = 130000000000000000ULL;
    if (creation_time != nullptr && mapped_guest_range(creation_time, 8, true)) {
        *reinterpret_cast<std::uint64_t*>(creation_time) = dummy_ft;
    }
    if (exit_time != nullptr && mapped_guest_range(exit_time, 8, true)) {
        *reinterpret_cast<std::uint64_t*>(exit_time) = dummy_ft;
    }
    if (kernel_time != nullptr && mapped_guest_range(kernel_time, 8, true)) {
        *reinterpret_cast<std::uint64_t*>(kernel_time) = 1000000ULL;
    }
    if (user_time != nullptr && mapped_guest_range(user_time, 8, true)) {
        *reinterpret_cast<std::uint64_t*>(user_time) = 2000000ULL;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_QueueUserAPC(void* const pfnAPC, void* const hThread, const std::uintptr_t dwData) noexcept {
    (void)pfnAPC;
    (void)hThread;
    (void)dwData;
    return 1;
}

}  // extern "C"
}  // namespace tradutorlinux
