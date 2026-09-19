#include "tradutorlinux/runtime/unwind.hpp"
#include "tradutorlinux/runtime/cxx_eh.hpp"

#include "../core/runtime_state_common.hpp"
#include "../core/runtime_process_state.hpp"
#include "../core/runtime_thread_state.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <string>

namespace tradutorlinux::runtime {
namespace {

struct UnwindImageView {
    const std::byte* base{};
    std::size_t size{};
    std::uint32_t exception_directory_rva{};
    std::span<const pe::RuntimeFunction> functions{};
};

thread_local UnwindImageView g_unwind_image{};

struct VectoredHandlerSlot {
    bool used{};
    void* routine{};
    std::int64_t order{};
};

std::array<VectoredHandlerSlot, 64> g_vectored_handlers{};
std::mutex g_vectored_handlers_mutex;
std::int64_t g_next_first_veh_order{-1};
std::int64_t g_next_last_veh_order{1};

constexpr std::int32_t kInvalidDisposition = 0x7FFFFFFF;
constexpr std::uint32_t kStatusUnwindConsolidate = 0x80000029U;

[[nodiscard]] bool add_u64(const std::uint64_t value, const std::uint64_t amount,
                           std::uint64_t& out) noexcept {
    if (amount > std::numeric_limits<std::uint64_t>::max() - value) {
        return false;
    }
    out = value + amount;
    return true;
}

[[nodiscard]] std::optional<std::size_t> find_function_by_pc(const std::uint64_t pc) noexcept {
    const std::uint64_t base = reinterpret_cast<std::uintptr_t>(g_unwind_image.base);
    if (g_unwind_image.base == nullptr || pc < base || pc - base >= g_unwind_image.size ||
        g_unwind_image.functions.empty()) {
        return std::nullopt;
    }
    const std::uint64_t rva = pc - base;
    // Conforme a especificação PE32+ x86-64, o diretório .pdata é estritamente ordenado por begin_rva crescente
    auto it = std::upper_bound(g_unwind_image.functions.begin(), g_unwind_image.functions.end(), rva,
                               [](const std::uint64_t val, const pe::RuntimeFunction& fn) noexcept {
                                   return val < fn.begin_rva;
                               });
    if (it != g_unwind_image.functions.begin()) {
        --it;
        if (rva >= it->begin_rva && rva < it->end_rva) {
            return static_cast<std::size_t>(it - g_unwind_image.functions.begin());
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> find_function_by_raw_pointer(
    const std::uint32_t* const function_entry) noexcept {
    if (g_unwind_image.base == nullptr || function_entry == nullptr) {
        return std::nullopt;
    }
    const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(function_entry);
    const std::uintptr_t table = reinterpret_cast<std::uintptr_t>(g_unwind_image.base) +
                                 g_unwind_image.exception_directory_rva;
    for (std::size_t index = 0; index < g_unwind_image.functions.size(); ++index) {
        const std::uintptr_t candidate = table + index * 12U;
        if (candidate == target) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> find_chained_function(
    const pe::RuntimeFunction& function) noexcept {
    if (!function.unwind.has_chained_function) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < g_unwind_image.functions.size(); ++index) {
        const pe::RuntimeFunction& candidate = g_unwind_image.functions[index];
        if (candidate.begin_rva == function.unwind.chained_begin_rva &&
            candidate.end_rva == function.unwind.chained_end_rva &&
            candidate.unwind_info_rva == function.unwind.chained_unwind_info_rva) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool is_v2_epilog(const pe::RuntimeFunction& function,
                                 const std::uint64_t control_pc) noexcept {
    if (function.unwind.version != 2U || g_unwind_image.base == nullptr) {
        return false;
    }
    const std::uint64_t base = reinterpret_cast<std::uintptr_t>(g_unwind_image.base);
    if (control_pc < base) {
        return false;
    }
    const std::uint64_t rva = control_pc - base;
    for (const pe::UnwindEpilog& epilog : function.unwind.epilogs) {
        if (rva >= epilog.begin_rva && rva < epilog.end_rva) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::uint64_t& context_register(ContextAmd64& context,
                                                const std::uint8_t number) noexcept {
    switch (number) {
        case 0: return context.rax;
        case 1: return context.rcx;
        case 2: return context.rdx;
        case 3: return context.rbx;
        case 4: return context.rsp;
        case 5: return context.rbp;
        case 6: return context.rsi;
        case 7: return context.rdi;
        case 8: return context.r8;
        case 9: return context.r9;
        case 10: return context.r10;
        case 11: return context.r11;
        case 12: return context.r12;
        case 13: return context.r13;
        case 14: return context.r14;
        default: return context.r15;
    }
}

[[nodiscard]] bool read_u64(const std::uint64_t address, std::uint64_t& out) noexcept {
    const auto* const source = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address));
    if (!validate_guest_stack_range(source, sizeof(out), false)) {
        return false;
    }
    return read_guest_memory(source, &out, sizeof(out)).status ==
           GuestMemoryAccessStatus::Success;
}

[[nodiscard]] bool read_m128(const std::uint64_t address, M128A& out) noexcept {
    const auto* const source = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address));
    if (!validate_guest_stack_range(source, sizeof(out), false)) {
        return false;
    }
    return read_guest_memory(source, &out, sizeof(out)).status ==
           GuestMemoryAccessStatus::Success;
}

[[nodiscard]] bool apply_unwind_code(const pe::UnwindCode& code, ContextAmd64& context,
                                      bool& machine_frame) noexcept {
    std::uint64_t address{};
    std::uint64_t value{};
    switch (code.operation) {
        case pe::UnwindOperation::PushNonVol:
            if (!read_u64(context.rsp, value) || !add_u64(context.rsp, 8, context.rsp)) {
                return false;
            }
            context_register(context, code.operation_info) = value;
            return true;
        case pe::UnwindOperation::AllocLarge:
        case pe::UnwindOperation::AllocSmall:
            return add_u64(context.rsp, code.operand, context.rsp);
        case pe::UnwindOperation::SetFpReg:
            // O registrador de frame pertence ao UNWIND_INFO, não ao código;
            // RtlVirtualUnwind trata esta operação com esse metadado.
            return false;
        case pe::UnwindOperation::SaveNonVol:
        case pe::UnwindOperation::SaveNonVolFar:
            if (!add_u64(context.rsp, code.operand, address) || !read_u64(address, value)) {
                return false;
            }
            context_register(context, code.operation_info) = value;
            return true;
        case pe::UnwindOperation::SaveXmm128:
        case pe::UnwindOperation::SaveXmm128Far:
            if (code.operation_info >= 16U || !add_u64(context.rsp, code.operand, address) ||
                !read_m128(address, context.floating[10U + code.operation_info])) {
                return false;
            }
            return true;
        case pe::UnwindOperation::PushMachFrame: {
            if (!add_u64(context.rsp, static_cast<std::uint64_t>(code.operation_info) * 8U,
                         address) ||
                !read_u64(address, context.rip) || !add_u64(address, 24U, address) ||
                !read_u64(address, context.rsp)) {
                return false;
            }
            machine_frame = true;
            return true;
        }
    }
    return false;
}

void trace_unwind_failure(const char* const symbol, const char* const detail) noexcept {
    trace_guest_failure(symbol, "unwind", detail);
}

void trace_seh(const char* const state, const std::uint32_t code,
               const char* const detail) noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"state", state},
        diagnostics::TraceField{"code", std::to_string(code)},
        diagnostics::TraceField{"detail", detail},
        diagnostics::TraceField{"mechanism", "x64-seh"},
    };
    runtime_trace("seh", fields, fields.size());
}

void trace_seh_handler(const char* const state, const std::uint32_t code,
                       const char* const detail, const void* const handler,
                       const void* const handler_data, const std::size_t function_index) noexcept {
    const auto relative_rva = [](const void* const pointer) {
        if (pointer == nullptr || g_unwind_image.base == nullptr) {
            return std::string{"0"};
        }
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(g_unwind_image.base);
        const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(pointer);
        if (address < base || address - base >= g_unwind_image.size) {
            return std::string{"outside"};
        }
        return std::to_string(address - base);
    };
    const std::array<diagnostics::TraceField, 7> fields{
        diagnostics::TraceField{"state", state},
        diagnostics::TraceField{"code", std::to_string(code)},
        diagnostics::TraceField{"detail", detail},
        diagnostics::TraceField{"mechanism", "x64-seh"},
        diagnostics::TraceField{"function-index", std::to_string(function_index)},
        diagnostics::TraceField{"handler-rva", relative_rva(handler)},
        diagnostics::TraceField{"handler-data-rva", relative_rva(handler_data)},
    };
    try {
        diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime,
                                 diagnostics::TraceLevel::Info, "seh", fields);
    } catch (...) {
    }
}

[[nodiscard]] bool image_range(const void* const pointer, const std::size_t size) noexcept {
    if (pointer == nullptr || g_unwind_image.base == nullptr) {
        return false;
    }
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(g_unwind_image.base);
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(pointer);
    if (address < begin || size > g_unwind_image.size || address - begin > g_unwind_image.size - size) {
        return false;
    }
    return true;
}

[[nodiscard]] bool image_rva(const std::uint32_t rva, const std::size_t size) noexcept {
    if (g_unwind_image.base == nullptr || rva > g_unwind_image.size ||
        size > g_unwind_image.size - rva) {
        return false;
    }
    return true;
}

[[nodiscard]] void* raw_function_entry(const std::size_t index) noexcept {
    return const_cast<std::byte*>(g_unwind_image.base) +
           g_unwind_image.exception_directory_rva + index * 12U;
}

[[noreturn]] void fail_seh(const std::uint32_t code, const char* const detail) noexcept {
    trace_seh("failed", code, detail);
    tl_ExitThread(code);
    std::abort();
}

[[nodiscard]] bool advance_leaf(ContextAmd64& context) noexcept {
    std::uint64_t return_address{};
    if (!read_u64(context.rsp, return_address) || !add_u64(context.rsp, 8U, context.rsp)) {
        return false;
    }
    context.rip = return_address;
    return true;
}

struct UnwoundFrame {
    std::size_t index{};
    std::uint64_t establisher_frame{};
    void* handler{};
    void* handler_data{};
    bool has_function{};
};

[[nodiscard]] bool unwind_one(ContextAmd64& context, const std::uint32_t handler_type,
                               UnwoundFrame& frame, const char*& error) noexcept {
    const std::optional<std::size_t> found = find_function_by_pc(context.rip);
    if (!found.has_value()) {
        if (!advance_leaf(context)) {
            error = "frame folha inválido";
            return false;
        }
        return true;
    }
    const pe::RuntimeFunction& function = g_unwind_image.functions[*found];
    if (is_v2_epilog(function, context.rip)) {
        error = "epílogo UNWIND_INFO V2 não é interpretado";
        return false;
    }
    void* handler_data = nullptr;
    std::uint64_t establisher = 0;
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(g_unwind_image.base);
    const ContextAmd64 before = context;
    void* const handler = tl_RtlVirtualUnwind(
        handler_type, base, context.rip,
        static_cast<std::uint32_t*>(raw_function_entry(*found)), &context, &handler_data,
        &establisher, nullptr);
    // RtlVirtualUnwind devolve nullptr tanto para "sem handler" quanto para
    // erro. Sem uma API de status, um contexto inalterado é a forma segura de
    // distinguir o segundo caso neste caminho interno.
    if (handler == nullptr && std::memcmp(&before, &context, sizeof(context)) == 0) {
        error = "RtlVirtualUnwind falhou";
        return false;
    }
    frame = {.index = *found,
             .establisher_frame = establisher,
             .handler = handler,
             .handler_data = handler_data,
             .has_function = true};
    return true;
}

using ExceptionRoutine = std::int32_t (TL_MSABI *)(ExceptionRecordAmd64*, void*, ContextAmd64*,
                                                   DispatcherContextAmd64*);
using ConsolidationRoutine = std::uint64_t (TL_MSABI *)(ExceptionRecordAmd64*);
using VectoredRoutine = std::int32_t (TL_MSABI *)(ExceptionPointersAmd64*);

[[nodiscard]] bool valid_guest_code(const void* const pointer) noexcept {
    return image_range(pointer, 1U);
}

[[nodiscard]] std::int32_t invoke_vectored(const void* const routine,
                                            ExceptionPointersAmd64* const pointers) noexcept {
    if (!valid_guest_code(routine)) {
        return kInvalidDisposition;
    }
    return reinterpret_cast<VectoredRoutine>(const_cast<void*>(routine))(pointers);
}

[[nodiscard]] std::int32_t invoke_language_handler(
    const void* const routine, ExceptionRecordAmd64* const record,
    const std::uint64_t establisher, ContextAmd64* const context,
    DispatcherContextAmd64* const dispatcher) noexcept {
    if (!valid_guest_code(routine)) {
        return kInvalidDisposition;
    }
    return reinterpret_cast<ExceptionRoutine>(const_cast<void*>(routine))(
        record, reinterpret_cast<void*>(establisher), context, dispatcher);
}

[[nodiscard]] std::int32_t invoke_cxx_or_language_handler(
    const void* const routine, const bool fh4, ExceptionRecordAmd64* const record,
    const std::uint64_t establisher, ContextAmd64* const context,
    DispatcherContextAmd64* const dispatcher) noexcept {
    if (fh4) {
        return cxx_frame_handler4(record, reinterpret_cast<void*>(establisher), context,
                                  dispatcher);
    }
    return invoke_language_handler(routine, record, establisher, context, dispatcher);
}

[[nodiscard]] bool invoke_consolidation_callback(
    ExceptionRecordAmd64* const record, std::uint64_t& target_ip) noexcept {
    if (record == nullptr || record->code != kStatusUnwindConsolidate ||
        record->parameter_count == 0U) {
        return false;
    }
    const std::uint64_t callback_address = record->parameters[0];
    if (callback_address == 0U ||
        !valid_guest_code(reinterpret_cast<const void*>(callback_address))) {
        return false;
    }
    const auto callback = reinterpret_cast<ConsolidationRoutine>(callback_address);
    target_ip = callback(record);
    return valid_guest_code(reinterpret_cast<const void*>(target_ip));
}

[[nodiscard]] bool validate_exception_record(const ExceptionRecordAmd64* const record) noexcept {
    return record != nullptr && record->parameter_count <= record->parameters.size();
}

}  // namespace

void set_guest_unwind_view(const void* const image_base, const std::size_t image_size,
                           const std::uint32_t exception_directory_rva,
                           const std::span<const pe::RuntimeFunction> functions) noexcept {
    g_unwind_image = {.base = static_cast<const std::byte*>(image_base),
                      .size = image_size,
                      .exception_directory_rva = exception_directory_rva,
                      .functions = functions};
}

void clear_guest_unwind_view() noexcept {
    g_unwind_image = {};
}

GuestUnwindView current_guest_unwind_view() noexcept {
    return {.image_base = g_unwind_image.base,
            .image_size = g_unwind_image.size,
            .exception_directory_rva = g_unwind_image.exception_directory_rva,
            .functions = g_unwind_image.functions};
}

void restore_guest_unwind_view(const GuestUnwindView view) noexcept {
    set_guest_unwind_view(view.image_base, view.image_size, view.exception_directory_rva, view.functions);
}

bool validate_guest_stack_range(const void* const address, const std::size_t size,
                                const bool writable) noexcept {
    (void)writable;
    if (address == nullptr || size == 0U) {
        return false;
    }
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(address);
    if (size > std::numeric_limits<std::uintptr_t>::max() - start) {
        return false;
    }
    if (::tradutorlinux::g_thread_teb != nullptr) {
        const std::uintptr_t stack_limit = ::tradutorlinux::g_thread_teb->stack_limit;
        const std::uintptr_t stack_base = ::tradutorlinux::g_thread_teb->stack_base;
        if (stack_limit == 0U || stack_base <= stack_limit || start < stack_limit ||
            start >= stack_base ||
            size > stack_base - start) {
            return false;
        }
    }
    // A transferência efetiva deve passar por read_guest_memory ou
    // write_guest_memory. Esta função só limita a faixa à stack convidada e
    // não usa a fotografia de mapas como garantia de acesso.
    return true;
}

void* add_vectored_exception_handler(const std::uint32_t first, void* const handler) noexcept {
    if (handler == nullptr || !valid_guest_code(handler)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_vectored_handlers_mutex);
    auto available = std::find_if(g_vectored_handlers.begin(), g_vectored_handlers.end(),
                                  [](const VectoredHandlerSlot& slot) { return !slot.used; });
    if (available == g_vectored_handlers.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    available->used = true;
    available->routine = handler;
    available->order = first != 0U ? g_next_first_veh_order-- : g_next_last_veh_order++;
    set_last_error(abi::kErrorSuccess);
    return static_cast<void*>(&*available);
}

std::uint32_t remove_vectored_exception_handler(void* const handle) noexcept {
    if (handle == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_vectored_handlers_mutex);
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(handle);
    const std::uintptr_t first = reinterpret_cast<std::uintptr_t>(g_vectored_handlers.data());
    const std::uintptr_t end = first + sizeof(g_vectored_handlers);
    if (address < first || address >= end ||
        (address - first) % sizeof(VectoredHandlerSlot) != 0U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    auto* mutable_slot = reinterpret_cast<VectoredHandlerSlot*>(handle);
    if (!mutable_slot->used) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    mutable_slot->used = false;
    mutable_slot->routine = nullptr;
    mutable_slot->order = 0;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

std::int32_t c_specific_handler(ExceptionRecordAmd64* const exception_record,
                                void*, ContextAmd64* const context_record,
                                DispatcherContextAmd64* const dispatcher) noexcept {
    if (!validate_exception_record(exception_record) || context_record == nullptr || dispatcher == nullptr ||
        dispatcher->handler_data == nullptr || g_unwind_image.base == nullptr) {
        trace_seh("failed", exception_record != nullptr ? exception_record->code : 0U,
                  "dados de __C_specific_handler inválidos");
        return kInvalidDisposition;
    }
    const auto* const data = static_cast<const std::byte*>(dispatcher->handler_data);
    if (!image_range(data, sizeof(std::uint32_t))) {
        return kInvalidDisposition;
    }
    std::uint32_t count{};
    std::memcpy(&count, data, sizeof(count));
    constexpr std::size_t kScopeRecordSize = 16U;
    if (count > 1024U || !image_range(data, sizeof(count) + static_cast<std::size_t>(count) * kScopeRecordSize)) {
        return kInvalidDisposition;
    }
    const std::uint64_t base = reinterpret_cast<std::uintptr_t>(g_unwind_image.base);
    if (dispatcher->control_pc < base || dispatcher->control_pc - base >= g_unwind_image.size) {
        return kInvalidDisposition;
    }
    const std::uint32_t control_rva = static_cast<std::uint32_t>(dispatcher->control_pc - base);
    ExceptionPointersAmd64 pointers{exception_record, context_record};
    for (std::size_t reverse = count; reverse > 0U; --reverse) {
        const std::byte* const record = data + sizeof(count) + (reverse - 1U) * kScopeRecordSize;
        std::array<std::uint32_t, 4> fields{};
        std::memcpy(fields.data(), record, sizeof(fields));
        const std::uint32_t begin = fields[0];
        const std::uint32_t end = fields[1];
        const std::uint32_t filter_rva = fields[2];
        const std::uint32_t target_rva = fields[3];
        if (begin >= end || !image_rva(begin, 1U) || !image_rva(end - 1U, 1U) ||
            control_rva < begin || control_rva >= end) {
            continue;
        }
        std::int32_t result = kVectoredContinueSearch;
        if (filter_rva == 1U) {
            result = 1;
        } else if (filter_rva != 0U) {
            if (!image_rva(filter_rva, 1U)) {
                return kInvalidDisposition;
            }
            result = invoke_vectored(g_unwind_image.base + filter_rva, &pointers);
        }
        if (result == -1) {
            return kExceptionContinueExecution;
        }
        if (result == 0) {
            continue;
        }
        if (result != 1 || target_rva == 0U || !image_rva(target_rva, 1U)) {
            return kInvalidDisposition;
        }
        dispatcher->scope_index = static_cast<std::uint32_t>(reverse - 1U);
        dispatcher->target_ip = base + target_rva;
        return kExceptionExecuteHandler;
    }
    return kExceptionContinueSearch;
}

[[noreturn]] void unwind_to_target(void* const target_frame, void* const target_ip,
                                   ExceptionRecordAmd64* const exception_record,
                                   void* const return_value, ContextAmd64* const context,
                                   void*) noexcept {
    if (target_frame == nullptr || target_ip == nullptr || context == nullptr ||
        !valid_guest_code(target_ip)) {
        fail_seh(0U, "alvo de unwind inválido");
    }
    ContextAmd64 current_context{};
    if (read_guest_memory(context, &current_context, sizeof(current_context)).status !=
        GuestMemoryAccessStatus::Success) {
        fail_seh(0U, "contexto de unwind inacessível");
    }
    ExceptionRecordAmd64 copied_record{};
    ExceptionRecordAmd64* active_record = nullptr;
    if (exception_record != nullptr) {
        if (read_guest_memory(exception_record, &copied_record, sizeof(copied_record)).status !=
                GuestMemoryAccessStatus::Success ||
            !validate_exception_record(&copied_record)) {
            fail_seh(0U, "registro de exceção inacessível");
        }
        active_record = &copied_record;
    }
    if (active_record != nullptr) {
        active_record->flags = kExceptionUnwinding;
    }
    ContextAmd64 cursor = current_context;
    cursor.rax = reinterpret_cast<std::uintptr_t>(return_value);
    for (std::size_t depth = 0; depth <= g_unwind_image.functions.size() + 64U; ++depth) {
        const ContextAmd64 before = cursor;
        UnwoundFrame frame{};
        const char* error = nullptr;
        if (!unwind_one(cursor, 0x2U, frame, error)) {
            fail_seh(active_record != nullptr ? active_record->code : 0U, error);
        }
        if (frame.has_function) {
            trace_seh("frame", active_record != nullptr ? active_record->code : 0U,
                      "unwind");
        }
        const bool cxx_exception = active_record != nullptr &&
                                   active_record->code == 0xE06D7363U;
        const bool matches_target =
            reinterpret_cast<void*>(frame.establisher_frame) == target_frame ||
            (!cxx_exception && (reinterpret_cast<void*>(cursor.rsp - 8U) == target_frame ||
                                reinterpret_cast<void*>(cursor.rsp) == target_frame));
        if (frame.has_function && matches_target) {
            // RtlUnwindEx invokes every UHANDLER while it walks toward the
            // target, regardless of the exception code.  C++ EH is only one
            // language handler that can live behind this callback; Win32
            // images also use private handlers for explicit unwind records
            // such as STATUS_UNWIND_CONSOLIDATE.
            if (active_record != nullptr) {
                active_record->flags |= kExceptionTargetUnwind;
            }
            const bool fh4_cxx_handler =
                cxx_exception && is_fh4_cxx_handler_data(frame.handler_data);
            const bool supported_cxx_handler =
                cxx_exception && is_supported_cxx_handler_data(frame.handler_data);
            if (cxx_exception && frame.handler != nullptr && !supported_cxx_handler) {
                trace_seh_handler("skipped", active_record->code,
                                  "unsupported-cxx-handler-during-unwind", frame.handler,
                                  frame.handler_data, frame.index);
            } else if (active_record != nullptr && frame.handler != nullptr) {
                DispatcherContextAmd64 dispatcher{
                    .control_pc = before.rip,
                    .image_base = reinterpret_cast<std::uintptr_t>(g_unwind_image.base),
                    .function_entry = static_cast<std::uint32_t*>(raw_function_entry(frame.index)),
                    .establisher_frame = frame.establisher_frame,
                    .target_ip = reinterpret_cast<std::uintptr_t>(target_ip),
                    .context_record = &current_context,
                    .language_handler = frame.handler,
                    .handler_data = frame.handler_data};
                trace_seh_handler("handler", active_record->code, "termination", frame.handler,
                                  frame.handler_data, frame.index);
                const std::int32_t disposition = invoke_cxx_or_language_handler(
                    frame.handler, fh4_cxx_handler, active_record, frame.establisher_frame,
                    &current_context, &dispatcher);
                if (supported_cxx_handler &&
                    disposition == kExceptionExecuteHandler && dispatcher.target_ip != 0U) {
                    ContextAmd64 action_context = cursor;
                    if (!prepare_cxx_cleanup_transfer(
                            action_context, before,
                            reinterpret_cast<void*>(frame.establisher_frame),
                            reinterpret_cast<void*>(dispatcher.target_ip), target_ip)) {
                        fail_seh(active_record->code, "transferência de cleanup inválida");
                    }
                    trace_seh("unwind", active_record->code, "cxx-cleanup");
                    tl_restore_guest_context_and_jump(&action_context);
                }
                if (disposition != kExceptionContinueSearch &&
                    disposition != kExceptionExecuteHandler) {
                    fail_seh(active_record->code,
                             "disposição de termination handler C++ inválida");
                }
                if (disposition == kExceptionExecuteHandler && !supported_cxx_handler) {
                    trace_seh("continued", active_record->code,
                              "static-handler-target");
                }
            }
            ContextAmd64 result = before;
            result.rip = reinterpret_cast<std::uintptr_t>(target_ip);
            result.rax = reinterpret_cast<std::uintptr_t>(return_value);
            if (active_record != nullptr &&
                active_record->code == kStatusUnwindConsolidate) {
                std::uint64_t consolidated_ip = 0;
                if (!invoke_consolidation_callback(active_record, consolidated_ip)) {
                    fail_seh(active_record->code,
                             "callback de consolidação inválido");
                }
                result.rip = consolidated_ip;
                trace_seh("unwind", active_record->code, "consolidated");
            }
            if (supported_cxx_handler &&
                !prepare_cxx_catch_transfer(result, target_frame, target_ip)) {
                fail_seh(active_record->code, "transferência de catch funclet inválida");
            }
            trace_seh("unwind", active_record != nullptr ? active_record->code : 0U, "target");
            tl_restore_guest_context_and_jump(&result);
        }
        if (frame.handler != nullptr) {
            const bool unsupported_cxx_handler =
                active_record != nullptr && active_record->code == 0xE06D7363U &&
                !is_supported_cxx_handler_data(frame.handler_data);
            const bool fh4_cxx_handler =
                active_record != nullptr && active_record->code == 0xE06D7363U &&
                is_fh4_cxx_handler_data(frame.handler_data);
            if (unsupported_cxx_handler) {
                trace_seh_handler("skipped", active_record->code,
                                  "unsupported-cxx-handler-during-unwind", frame.handler,
                                  frame.handler_data, frame.index);
                current_context = cursor;
                continue;
            }
            DispatcherContextAmd64 dispatcher{.control_pc = before.rip,
                                              .image_base = reinterpret_cast<std::uintptr_t>(g_unwind_image.base),
                                              .function_entry = static_cast<std::uint32_t*>(raw_function_entry(frame.index)),
                                              .establisher_frame = frame.establisher_frame,
                                              .target_ip = active_record != nullptr &&
                                                               active_record->code == 0xE06D7363U
                                                           ? 0U
                                                           : reinterpret_cast<std::uintptr_t>(target_ip),
                                              .context_record = &current_context,
                                              .language_handler = frame.handler,
                                              .handler_data = frame.handler_data};
            trace_seh_handler("handler", active_record != nullptr ? active_record->code : 0U,
                              "termination", frame.handler, frame.handler_data, frame.index);
            const std::int32_t disposition = invoke_cxx_or_language_handler(
                frame.handler, fh4_cxx_handler, active_record, frame.establisher_frame,
                &current_context, &dispatcher);
            if (disposition == kInvalidDisposition ||
                (disposition != kExceptionContinueSearch && disposition != kExceptionContinueExecution)) {
                fail_seh(active_record != nullptr ? active_record->code : 0U,
                         "disposição de termination handler inválida");
            }
        }
        current_context = cursor;
    }
    fail_seh(active_record != nullptr ? active_record->code : 0U, "frame alvo não encontrado");
}

[[noreturn]] void dispatch_raised_exception(ContextAmd64 context, const std::uint32_t code,
                                            const std::uint32_t flags,
                                            const std::uint32_t parameter_count,
                                            const std::uint64_t* const parameters) noexcept {
    if ((flags & ~kExceptionNoncontinuable) != 0U || parameter_count > 15U ||
        (parameter_count != 0U && parameters == nullptr)) {
        fail_seh(code, "argumentos de RaiseException inválidos");
    }
    if (code == 0xE06D7363U && cxx_eh_funclet_active()) {
        // O protocolo completo de nested funclets/rethrow da ABI MSVC ainda
        // não faz parte do subconjunto validado. Rejeitar aqui evita que a
        // exceção seja redirecionada ao mesmo handler indefinidamente.
        fail_seh(code, "nested-cxx-exception-unsupported");
    }
    ExceptionRecordAmd64 record{};
    record.code = code;
    record.flags = flags;
    record.address = reinterpret_cast<void*>(context.rip);
    record.parameter_count = parameter_count;
    if (parameter_count != 0U) {
        if (read_guest_memory(parameters, record.parameters.data(),
                              parameter_count * sizeof(*parameters)).status !=
            GuestMemoryAccessStatus::Success) {
            fail_seh(code, "argumentos de RaiseException inacessíveis");
        }
    }
    ExceptionPointersAmd64 pointers{&record, &context};
    trace_seh("raised", code, "RaiseException");

    struct Callback {
        void* routine{};
        std::int64_t order{};
    };
    std::array<Callback, 64> callbacks{};
    std::size_t callback_count = 0;
    {
        std::lock_guard<std::mutex> lock(g_vectored_handlers_mutex);
        for (const VectoredHandlerSlot& slot : g_vectored_handlers) {
            if (slot.used && callback_count < callbacks.size()) {
                callbacks[callback_count++] = {.routine = slot.routine, .order = slot.order};
            }
        }
    }
    std::sort(callbacks.begin(), callbacks.begin() + static_cast<std::ptrdiff_t>(callback_count),
              [](const Callback& left, const Callback& right) { return left.order < right.order; });
    for (std::size_t index = 0; index < callback_count; ++index) {
        trace_seh("veh", code, "callback");
        const std::int32_t disposition = invoke_vectored(callbacks[index].routine, &pointers);
        if (disposition == kVectoredContinueExecution) {
            if ((record.flags & kExceptionNoncontinuable) != 0U) {
                fail_seh(code, "exceção não continuável");
            }
            trace_seh("continued", code, "veh");
            tl_restore_guest_context_and_jump(&context);
        }
        if (disposition != kVectoredContinueSearch) {
            fail_seh(code, "VEH inválido");
        }
    }

    ContextAmd64 cursor = context;
    for (std::size_t depth = 0; depth <= g_unwind_image.functions.size() + 64U; ++depth) {
        const ContextAmd64 before = cursor;
        UnwoundFrame frame{};
        const char* error = nullptr;
        if (!unwind_one(cursor, 0x1U, frame, error)) {
            // A stack convidada termina antes de qualquer frame hospedeiro.
            // Isso encerra a busca normalmente e deixa o
            // UnhandledExceptionFilter produzir o diagnóstico final; não é
            // uma autorização para ler a próxima área mapeada do host.
            if (error != nullptr && std::strcmp(error, "frame folha inválido") == 0) {
                break;
            }
            fail_seh(code, error);
        }
        if (frame.has_function) {
            trace_seh("frame", code, "search");
        }
        if (frame.handler == nullptr) {
            continue;
        }
        if (code == 0xE06D7363U && !is_supported_cxx_handler_data(frame.handler_data)) {
            trace_seh_handler("skipped", code, "unsupported-cxx-handler-during-search",
                              frame.handler, frame.handler_data, frame.index);
            continue;
        }
        DispatcherContextAmd64 dispatcher{.control_pc = before.rip,
                                          .image_base = reinterpret_cast<std::uintptr_t>(g_unwind_image.base),
                                          .function_entry = static_cast<std::uint32_t*>(raw_function_entry(frame.index)),
                                          .establisher_frame = frame.establisher_frame,
                                          .context_record = &context,
                                          .language_handler = frame.handler,
                                          .handler_data = frame.handler_data};
        trace_seh_handler("handler", code, "search", frame.handler, frame.handler_data,
                          frame.index);
        const std::int32_t disposition = invoke_cxx_or_language_handler(
            frame.handler, code == 0xE06D7363U &&
                              is_fh4_cxx_handler_data(frame.handler_data),
            &record, frame.establisher_frame, &context, &dispatcher);
        if (disposition == kExceptionContinueExecution) {
            if ((record.flags & kExceptionNoncontinuable) != 0U) {
                fail_seh(code, "exceção não continuável");
            }
            trace_seh("continued", code, "language-handler");
            tl_restore_guest_context_and_jump(&context);
        }
        if (disposition == kExceptionExecuteHandler && dispatcher.target_ip != 0U) {
            unwind_to_target(reinterpret_cast<void*>(frame.establisher_frame),
                             reinterpret_cast<void*>(dispatcher.target_ip), &record, nullptr,
                             &context, nullptr);
        }
        if (disposition != kExceptionContinueSearch) {
            fail_seh(code, "disposição de language handler inválida");
        }
    }
    const std::int32_t final_disposition = tl_UnhandledExceptionFilter(&pointers);
    if (final_disposition == kVectoredContinueExecution &&
        (record.flags & kExceptionNoncontinuable) == 0U) {
        trace_seh("continued", code, "unhandled-filter");
        tl_restore_guest_context_and_jump(&context);
    }
    fail_seh(code, "exceção não tratada");
}

}  // namespace tradutorlinux::runtime

namespace tradutorlinux {

extern "C" [[noreturn]] void tl_dispatch_raised_exception_from_asm(
    runtime::ContextAmd64* const context, const std::uint32_t code,
    const std::uint32_t flags, const std::uint32_t parameter_count,
    const std::uint64_t* const parameters) noexcept {
    const std::array fields{diagnostics::TraceField{"function", "tl_RaiseException"}};
    diagnostics::write_json_trace(diagnostics::TraceComponent::Runtime,
                                  diagnostics::TraceLevel::Debug, "function-enter", fields);
    if (context == nullptr) {
        std::abort();
    }
    runtime::ContextAmd64 copied_context{};
    if (runtime::read_guest_memory(context, &copied_context, sizeof(copied_context)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        trace_guest_failure("RaiseException", "guest-memory", "contexto inacessível");
    }
    runtime::dispatch_raised_exception(copied_context, code, flags, parameter_count, parameters);
}

extern "C" [[noreturn]] void tl_dispatch_rtl_unwind_from_asm(
    runtime::ContextAmd64* const context, void* const target_frame, void* const target_ip,
    runtime::ExceptionRecordAmd64* const exception_record, void* const return_value) noexcept {
    const std::array fields{diagnostics::TraceField{"function", "tl_RtlUnwind"}};
    diagnostics::write_json_trace(diagnostics::TraceComponent::Runtime,
                                  diagnostics::TraceLevel::Debug, "function-enter", fields);
    runtime::unwind_to_target(target_frame, target_ip, exception_record, return_value,
                              context, nullptr);
}

extern "C" [[noreturn]] void tl_dispatch_rtl_unwind_ex_from_asm(
    runtime::ContextAmd64* const captured_context, void* const target_frame,
    void* const target_ip, runtime::ExceptionRecordAmd64* const exception_record,
    void* const return_value, runtime::ContextAmd64* const context_record) noexcept {
    const std::array fields{diagnostics::TraceField{"function", "tl_RtlUnwindEx"}};
    diagnostics::write_json_trace(diagnostics::TraceComponent::Runtime,
                                  diagnostics::TraceLevel::Debug, "function-enter", fields);
    runtime::ContextAmd64* working_context = captured_context;
    if (working_context == nullptr) {
        working_context = context_record;
    }
    runtime::unwind_to_target(target_frame, target_ip, exception_record, return_value,
                              working_context != nullptr ? working_context : context_record, nullptr);
}

extern "C" TL_MSABI std::uint32_t* tl_RtlLookupFunctionEntry(
    const std::uint64_t control_pc, std::uint64_t* const image_base, void*) noexcept {
    if (image_base == nullptr) {
        runtime::trace_unwind_failure("RtlLookupFunctionEntry", "ImageBase inválido");
        return nullptr;
    }
    std::uint64_t result_base = 0;
    const std::optional<std::size_t> found = runtime::find_function_by_pc(control_pc);
    if (!found.has_value()) {
        if (runtime::write_guest_memory(image_base, &result_base, sizeof(result_base)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            runtime::trace_unwind_failure("RtlLookupFunctionEntry", "ImageBase inacessível");
        }
        return nullptr;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(runtime::g_unwind_image.base);
    result_base = base;
    if (runtime::write_guest_memory(image_base, &result_base, sizeof(result_base)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        runtime::trace_unwind_failure("RtlLookupFunctionEntry", "ImageBase inacessível");
        return nullptr;
    }
    return reinterpret_cast<std::uint32_t*>(
        const_cast<std::byte*>(runtime::g_unwind_image.base) +
        runtime::g_unwind_image.exception_directory_rva + *found * 12U);
}

extern "C" TL_MSABI void* tl_RtlPcToFileHeader(void* const pc_value,
                                                void** const base_of_image) noexcept {
    if (base_of_image == nullptr) {
        runtime::trace_unwind_failure("RtlPcToFileHeader", "BaseOfImage inválido");
        return nullptr;
    }
    void* result = nullptr;
    const std::uint64_t pc = reinterpret_cast<std::uintptr_t>(pc_value);
    const std::uint64_t base = reinterpret_cast<std::uintptr_t>(runtime::g_unwind_image.base);
    if (runtime::g_unwind_image.base == nullptr || pc < base ||
        pc - base >= runtime::g_unwind_image.size) {
        if (runtime::write_guest_memory(base_of_image, &result, sizeof(result)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            runtime::trace_unwind_failure("RtlPcToFileHeader", "BaseOfImage inacessível");
        }
        return nullptr;
    }
    result = const_cast<std::byte*>(runtime::g_unwind_image.base);
    if (runtime::write_guest_memory(base_of_image, &result, sizeof(result)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        runtime::trace_unwind_failure("RtlPcToFileHeader", "BaseOfImage inacessível");
        return nullptr;
    }
    return result;
}

extern "C" TL_MSABI void* tl_RtlVirtualUnwind(
    const std::uint32_t handler_type, const std::uint64_t image_base,
    const std::uint64_t control_pc, std::uint32_t* const function_entry,
    runtime::ContextAmd64* const context, void** const handler_data,
    std::uint64_t* const establisher_frame, void*) noexcept {
    if (context == nullptr) {
        runtime::trace_unwind_failure("RtlVirtualUnwind", "ContextRecord inválido");
        return nullptr;
    }
    runtime::ContextAmd64 updated{};
    if (runtime::read_guest_memory(context, &updated, sizeof(updated)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        runtime::trace_unwind_failure("RtlVirtualUnwind", "ContextRecord inacessível");
        return nullptr;
    }
    const auto expected_base = reinterpret_cast<std::uintptr_t>(runtime::g_unwind_image.base);
    const std::optional<std::size_t> first = runtime::find_function_by_raw_pointer(function_entry);
    const std::optional<std::size_t> control_function = runtime::find_function_by_pc(control_pc);
    if (image_base != expected_base || !first.has_value() || !control_function.has_value() ||
        *first != *control_function) {
        runtime::trace_unwind_failure("RtlVirtualUnwind", "RUNTIME_FUNCTION fora da imagem ativa");
        return nullptr;
    }
    if (runtime::is_v2_epilog(runtime::g_unwind_image.functions[*first], control_pc)) {
        runtime::trace_unwind_failure("RtlVirtualUnwind",
                                      "epílogo UNWIND_INFO V2 não é interpretado");
        return nullptr;
    }

    // No x64, o EstablisherFrame de uma função sem frame register é o RSP do
    // corpo da função, não o RSP do quadro do chamador produzido depois de
    // aplicar os códigos de unwind. Isso é observável por handlers /GS, que
    // localizam o cookie com offsets relativos ao frame ainda ativo.
    std::uint64_t establisher_value = updated.rsp;
    std::size_t current = *first;
    bool machine_frame = false;
    const pe::RuntimeFunction* terminal = nullptr;
    for (std::size_t depth = 0; depth <= runtime::g_unwind_image.functions.size(); ++depth) {
        const pe::RuntimeFunction& function = runtime::g_unwind_image.functions[current];
        // Uma entrada CHAININFO representa um prólogo já concluído pelo
        // frame externo; todas as suas operações precisam ser aplicadas.
        const std::uint64_t function_pc = depth == 0 ? control_pc :
            expected_base + function.end_rva;
        const std::uint64_t pc_offset = function_pc - expected_base - function.begin_rva;
        for (const pe::UnwindCode& code : function.unwind.codes) {
            if (pc_offset < code.code_offset) {
                continue;
            }
            if (code.operation == pe::UnwindOperation::SetFpReg) {
                const std::uint64_t frame = runtime::context_register(updated, function.unwind.frame_register);
                const std::uint64_t displacement = static_cast<std::uint64_t>(function.unwind.frame_offset) * 16U;
                if (frame < displacement) {
                    runtime::trace_unwind_failure("RtlVirtualUnwind", "frame pointer inválido");
                    return nullptr;
                }
                establisher_value = frame - displacement;
                updated.rsp = frame - displacement;
                continue;
            }
            if (!runtime::apply_unwind_code(code, updated, machine_frame)) {
                runtime::trace_unwind_failure("RtlVirtualUnwind", "pilha ou código de unwind inválido");
                return nullptr;
            }
        }
        if (!function.unwind.has_chained_function) {
            terminal = &function;
            break;
        }
        const std::optional<std::size_t> chained = runtime::find_chained_function(function);
        if (!chained.has_value()) {
            runtime::trace_unwind_failure("RtlVirtualUnwind", "CHAININFO inválido");
            return nullptr;
        }
        current = *chained;
    }
    if (terminal == nullptr) {
        runtime::trace_unwind_failure("RtlVirtualUnwind", "CHAININFO cíclico");
        return nullptr;
    }
    if (!machine_frame) {
        std::uint64_t return_address{};
        if (!runtime::read_u64(updated.rsp, return_address) ||
            !runtime::add_u64(updated.rsp, 8U, updated.rsp)) {
            runtime::trace_unwind_failure("RtlVirtualUnwind", "endereço de retorno inválido");
            return nullptr;
        }
        updated.rip = return_address;
    }
    const std::uint8_t flags = terminal->unwind.flags;
    const pe::RuntimeFunction& initial = runtime::g_unwind_image.functions[*first];
    const std::uint64_t initial_offset = control_pc - expected_base - initial.begin_rva;
    const bool in_prolog = initial_offset < initial.unwind.prolog_size;
    if (in_prolog || handler_type == 0U || (flags & (0x1U | 0x2U)) == 0U ||
        (handler_type & flags) == 0U) {
        void* result_handler_data = nullptr;
        if (establisher_frame != nullptr &&
            runtime::write_guest_memory(establisher_frame, &establisher_value,
                                        sizeof(establisher_value)).status !=
                runtime::GuestMemoryAccessStatus::Success) {
            runtime::trace_unwind_failure("RtlVirtualUnwind", "EstablisherFrame inacessível");
            return nullptr;
        }
        if (handler_data != nullptr &&
            runtime::write_guest_memory(handler_data, &result_handler_data, sizeof(result_handler_data)).status !=
                runtime::GuestMemoryAccessStatus::Success) {
            runtime::trace_unwind_failure("RtlVirtualUnwind", "HandlerData inacessível");
            return nullptr;
        }
        if (runtime::write_guest_memory(context, &updated, sizeof(updated)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            runtime::trace_unwind_failure("RtlVirtualUnwind", "ContextRecord inacessível");
            return nullptr;
        }
        return nullptr;
    }
    if (establisher_frame != nullptr &&
        runtime::write_guest_memory(establisher_frame, &establisher_value,
                                    sizeof(establisher_value)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
        runtime::trace_unwind_failure("RtlVirtualUnwind", "EstablisherFrame inacessível");
        return nullptr;
    }
    void* result_handler_data = const_cast<std::byte*>(runtime::g_unwind_image.base) +
                                terminal->unwind.handler_data_rva;
    if (handler_data != nullptr) {
        if (runtime::write_guest_memory(handler_data, &result_handler_data,
                                        sizeof(result_handler_data)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            runtime::trace_unwind_failure("RtlVirtualUnwind", "HandlerData inacessível");
            return nullptr;
        }
    }
    if (runtime::write_guest_memory(context, &updated, sizeof(updated)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        runtime::trace_unwind_failure("RtlVirtualUnwind", "ContextRecord inacessível");
        return nullptr;
    }
    return const_cast<std::byte*>(runtime::g_unwind_image.base) + terminal->unwind.handler_rva;
}

extern "C" TL_MSABI std::int32_t tl_UnhandledExceptionFilter(
    runtime::ExceptionPointersAmd64* const pointers) noexcept {
    if (pointers == nullptr) {
        return runtime::kVectoredContinueSearch;
    }
    runtime::ExceptionPointersAmd64 pointers_copy{};
    if (runtime::read_guest_memory(pointers, &pointers_copy, sizeof(pointers_copy)).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        pointers_copy.exception_record == nullptr || pointers_copy.context_record == nullptr) {
        return runtime::kVectoredContinueSearch;
    }
    runtime::ExceptionRecordAmd64 record_copy{};
    runtime::ContextAmd64 context_copy{};
    if (runtime::read_guest_memory(pointers_copy.exception_record, &record_copy,
                                   sizeof(record_copy)).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        runtime::read_guest_memory(pointers_copy.context_record, &context_copy,
                                   sizeof(context_copy)).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        record_copy.parameter_count > record_copy.parameters.size()) {
        return runtime::kVectoredContinueSearch;
    }
    const std::uintptr_t filter = g_unhandled_exception_filter.load(std::memory_order_acquire);
    if (filter == 0U || !runtime::image_range(reinterpret_cast<const void*>(filter), 1U)) {
        return runtime::kVectoredContinueSearch;
    }
    using FilterRoutine = std::int32_t (TL_MSABI *)(runtime::ExceptionPointersAmd64*);
    runtime::ExceptionPointersAmd64 callback_pointers{&record_copy, &context_copy};
    const std::int32_t result = reinterpret_cast<FilterRoutine>(filter)(&callback_pointers);
    if (result != runtime::kVectoredContinueExecution) {
        return runtime::kVectoredContinueSearch;
    }
    if (runtime::write_guest_memory(pointers_copy.context_record, &context_copy,
                                    sizeof(context_copy)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        return runtime::kVectoredContinueSearch;
    }
    return result;
}

}  // namespace tradutorlinux
