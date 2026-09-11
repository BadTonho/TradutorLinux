#include "tradutorlinux/runtime/cxx_eh.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/win32/kernel32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

namespace tradutorlinux::runtime {
namespace {

constexpr std::uint32_t kCxxException = 0xE06D7363U;
constexpr std::uint32_t kFuncInfoMagicV3 = 0x19930522U;
constexpr std::uint32_t kCatchAll = 0x40U;
constexpr std::size_t kFuncInfoWords = 10U;
constexpr std::size_t kUnwindMapEntrySize = 8U;
constexpr std::size_t kTryBlockEntrySize = 20U;
constexpr std::size_t kHandlerMapEntrySize = 20U;
constexpr std::size_t kIpMapEntrySize = 8U;
constexpr std::int32_t kInvalidState = -1;
constexpr std::int32_t kMaxState = 4095;
constexpr std::uint32_t kMaxTryBlocks = 256U;
constexpr std::uint32_t kMaxHandlersPerTry = 64U;
constexpr std::uint32_t kMaxIpEntries = 4096U;
constexpr std::uint32_t kMaxCatchableTypes = 64U;
constexpr std::size_t kThrowInfoSize = 16U;
constexpr std::size_t kCatchableTypeSize = 28U;
constexpr std::size_t kTypeDescriptorNameOffset = 16U;
constexpr std::size_t kMaxCleanupActions = 64U;

thread_local ContextAmd64 g_cxx_catch_context{};
thread_local bool g_cxx_catch_context_ready = false;
thread_local bool g_cxx_funclet_active = false;
thread_local std::uint64_t g_cxx_catch_target = 0U;
thread_local bool g_cxx_cleanup_context_ready = false;
thread_local std::uint64_t g_cxx_cleanup_target = 0U;
thread_local std::uint64_t g_cxx_cleanup_establisher = 0U;
thread_local std::array<std::uint32_t, kMaxCleanupActions> g_cxx_cleanup_actions{};
thread_local std::size_t g_cxx_cleanup_count = 0U;
thread_local std::size_t g_cxx_cleanup_index = 0U;

struct ImageReader {
    GuestUnwindView view{};

    [[nodiscard]] bool range(const std::uint32_t rva, const std::size_t size) const noexcept {
        return view.image_base != nullptr && rva <= view.image_size &&
               size <= view.image_size - static_cast<std::size_t>(rva);
    }

    [[nodiscard]] const std::byte* address(const std::uint32_t rva,
                                            const std::size_t size = 1U) const noexcept {
        if (!range(rva, size)) {
            return nullptr;
        }
        return static_cast<const std::byte*>(view.image_base) + rva;
    }

    [[nodiscard]] bool read_u32(const std::uint32_t rva, std::uint32_t& value) const noexcept {
        const std::byte* const source = address(rva, sizeof(value));
        if (source == nullptr) {
            return false;
        }
        std::memcpy(&value, source, sizeof(value));
        return true;
    }

    [[nodiscard]] bool add_rva(const std::uint32_t rva, const std::size_t delta,
                               std::uint32_t& result) const noexcept {
        if (delta > std::numeric_limits<std::uint32_t>::max() - rva) {
            return false;
        }
        result = rva + static_cast<std::uint32_t>(delta);
        return true;
    }

    [[nodiscard]] bool read_i32(const std::uint32_t rva, std::int32_t& value) const noexcept {
        std::uint32_t raw{};
        if (!read_u32(rva, raw)) {
            return false;
        }
        std::memcpy(&value, &raw, sizeof(value));
        return true;
    }

    [[nodiscard]] bool range_for_count(const std::uint32_t rva, const std::uint32_t count,
                                       const std::size_t stride) const noexcept {
        if (stride != 0U && count > std::numeric_limits<std::size_t>::max() / stride) {
            return false;
        }
        return address(rva, static_cast<std::size_t>(count) * stride) != nullptr;
    }

    [[nodiscard]] bool rva_of(const void* const pointer, std::uint32_t& rva) const noexcept {
        if (pointer == nullptr || view.image_base == nullptr) {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(view.image_base);
        const auto value = reinterpret_cast<std::uintptr_t>(pointer);
        if (value < base || value - base >= view.image_size || value - base >
            std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        rva = static_cast<std::uint32_t>(value - base);
        return true;
    }
};

struct FuncInfo {
    std::int32_t max_state{};
    std::uint32_t unwind_map{};
    std::uint32_t try_block_count{};
    std::uint32_t try_block_map{};
    std::uint32_t ip_map_count{};
    std::uint32_t ip_map{};
    std::uint32_t eh_flags{};
};

struct UnwindAction {
    std::int32_t to_state{kInvalidState};
    std::uint32_t action_rva{0xFFFFFFFFU};
};

struct CleanupPlan {
    std::array<std::uint32_t, kMaxCleanupActions> actions{};
    std::size_t count{};
};

void trace_cxx_eh(const diagnostics::TraceLevel level, const char* const state,
                  const char* const detail) noexcept {
    try {
        const std::array fields{
            diagnostics::TraceField{"state", state},
            diagnostics::TraceField{"detail", detail},
            diagnostics::TraceField{"mechanism", "msvc-x64"},
        };
        diagnostics::write_trace(
            std::cerr, diagnostics::TraceComponent::Runtime, level, "cxx-eh", fields);
    } catch (...) {
    }
}

void trace_cxx_eh_state(const diagnostics::TraceLevel level, const char* const detail,
                        const std::uint32_t control_rva, const std::int32_t state) noexcept {
    try {
        const std::array fields{
            diagnostics::TraceField{"state", "search"},
            diagnostics::TraceField{"detail", detail},
            diagnostics::TraceField{"mechanism", "msvc-x64"},
            diagnostics::TraceField{"control-rva", std::to_string(control_rva)},
            diagnostics::TraceField{"current-state", std::to_string(state)},
        };
        diagnostics::write_trace(
            std::cerr, diagnostics::TraceComponent::Runtime, level, "cxx-eh", fields);
    } catch (...) {
    }
}

[[nodiscard]] bool read_func_info(const ImageReader& image, const std::uint32_t rva,
                                  FuncInfo& info) noexcept {
    std::array<std::uint32_t, kFuncInfoWords> words{};
    for (std::size_t index = 0; index < words.size(); ++index) {
        std::uint32_t entry{};
        if (!image.add_rva(rva, index * sizeof(std::uint32_t), entry) ||
            !image.read_u32(entry, words[index])) {
            return false;
        }
    }
    if (words[0] != kFuncInfoMagicV3) {
        return false;
    }
    std::memcpy(&info.max_state, &words[1], sizeof(info.max_state));
    info.unwind_map = words[2];
    info.try_block_count = words[3];
    info.try_block_map = words[4];
    info.ip_map_count = words[5];
    info.ip_map = words[6];
    info.eh_flags = words[9];
    if (info.max_state < kInvalidState || info.max_state > kMaxState ||
        info.try_block_count > kMaxTryBlocks || info.ip_map_count > kMaxIpEntries) {
        return false;
    }
    const std::uint32_t unwind_count =
        info.max_state < 0 ? 0U : static_cast<std::uint32_t>(info.max_state);
    return image.range_for_count(info.unwind_map, unwind_count, kUnwindMapEntrySize) &&
           image.range_for_count(info.try_block_map, info.try_block_count,
                                 kTryBlockEntrySize) &&
           image.range_for_count(info.ip_map, info.ip_map_count, kIpMapEntrySize);
}

[[nodiscard]] bool valid_state(const std::int32_t state, const FuncInfo& info) noexcept {
    return state == kInvalidState ||
           (state >= 0 && state < info.max_state);
}

[[nodiscard]] bool validate_unwind_map(const ImageReader& image, const FuncInfo& info) noexcept {
    const std::uint32_t count =
        info.max_state < 0 ? 0U : static_cast<std::uint32_t>(info.max_state);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint32_t entry{};
        std::int32_t state{};
        std::uint32_t action{};
        std::uint32_t action_entry{};
        if (!image.add_rva(info.unwind_map,
                           static_cast<std::size_t>(index) * kUnwindMapEntrySize, entry) ||
            !image.add_rva(entry, 4U, action_entry) || !image.read_i32(entry, state) ||
            !image.read_u32(action_entry, action) ||
            !valid_state(state, info) || (action != 0xFFFFFFFFU && !image.range(action, 1U))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool read_unwind_action(const ImageReader& image, const FuncInfo& info,
                                      const std::int32_t state,
                                      UnwindAction& action) noexcept {
    if (state == kInvalidState || !valid_state(state, info)) {
        return false;
    }
    std::uint32_t entry{};
    std::uint32_t action_entry{};
    if (!image.add_rva(info.unwind_map,
                       static_cast<std::size_t>(state) * kUnwindMapEntrySize, entry) ||
        !image.add_rva(entry, 4U, action_entry) || !image.read_i32(entry, action.to_state) ||
        !image.read_u32(action_entry, action.action_rva) ||
        !valid_state(action.to_state, info) ||
        (action.action_rva != 0xFFFFFFFFU && !image.range(action.action_rva, 1U))) {
        return false;
    }
    // LLVM usa tanto RVA zero quanto -1 como entrada sem ação, dependendo
    // da versão do WinEH que produziu a tabela. A entrada continua válida;
    // o construtor da cadeia decide quando não há mais uma ação executável.
    return true;
}

[[nodiscard]] bool build_cleanup_plan(const ImageReader& image, const FuncInfo& info,
                                      const std::int32_t initial_state,
                                      CleanupPlan& plan) noexcept {
    std::array<std::int32_t, kMaxCleanupActions> visited{};
    std::size_t visited_count = 0U;
    std::int32_t state = initial_state;
    while (state != kInvalidState) {
        if (!valid_state(state, info) || visited_count >= visited.size()) {
            return false;
        }
        if (std::find(visited.begin(), visited.begin() +
                                      static_cast<std::ptrdiff_t>(visited_count), state) !=
            visited.begin() + static_cast<std::ptrdiff_t>(visited_count)) {
            return false;
        }
        visited[visited_count++] = state;

        UnwindAction action{};
        if (!read_unwind_action(image, info, state, action)) {
            return false;
        }
        // Uma entrada sem ação encerra a cadeia relevante para o handler
        // selecionado. Estados anteriores podem representar outra região do
        // mesmo FuncInfo (por exemplo, o caminho de terminação do catch).
        if (action.action_rva == 0U || action.action_rva == 0xFFFFFFFFU) {
            break;
        }
        if (plan.count >= plan.actions.size()) {
            return false;
        }
        plan.actions[plan.count++] = action.action_rva;
        state = action.to_state;
    }
    return true;
}

[[nodiscard]] bool validate_ip_map(const ImageReader& image, const FuncInfo& info) noexcept {
    std::uint32_t previous_ip = 0U;
    for (std::uint32_t index = 0; index < info.ip_map_count; ++index) {
        std::uint32_t entry{};
        std::uint32_t state_entry{};
        std::uint32_t ip{};
        std::int32_t state{};
        if (!image.add_rva(info.ip_map,
                           static_cast<std::size_t>(index) * kIpMapEntrySize, entry) ||
            !image.add_rva(entry, 4U, state_entry) || !image.read_u32(entry, ip) ||
            !image.read_i32(state_entry, state) ||
            !image.range(ip, 1U) || !valid_state(state, info) ||
            (index != 0U && ip < previous_ip)) {
            return false;
        }
        previous_ip = ip;
    }
    return true;
}

struct CatchTarget {
    std::uint32_t handler_rva{};
    std::uint32_t scope_index{};
    bool catch_all{};
};

[[nodiscard]] bool thrown_type_matches(const ImageReader& image,
                                       const ExceptionRecordAmd64& record,
                                       const std::uint32_t handler_type_rva) noexcept {
    if (record.parameter_count < 3U) {
        return false;
    }
    std::uint32_t throw_info_rva{};
    if (!image.rva_of(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
                          record.parameters[2])), throw_info_rva) ||
        !image.range(throw_info_rva, kThrowInfoSize)) {
        return false;
    }

    std::uint32_t catchable_array_rva{};
    if (!image.add_rva(throw_info_rva, 12U, catchable_array_rva) ||
        !image.read_u32(catchable_array_rva, catchable_array_rva) ||
        !image.range(catchable_array_rva, sizeof(std::uint32_t))) {
        return false;
    }
    std::uint32_t catchable_count{};
    std::uint32_t entries_rva{};
    if (!image.read_u32(catchable_array_rva, catchable_count) ||
        catchable_count > kMaxCatchableTypes ||
        !image.add_rva(catchable_array_rva, sizeof(std::uint32_t), entries_rva) ||
        !image.range_for_count(entries_rva, catchable_count, sizeof(std::uint32_t))) {
        return false;
    }

    for (std::uint32_t index = 0; index < catchable_count; ++index) {
        std::uint32_t entry_rva{};
        std::uint32_t catchable_type_rva{};
        std::uint32_t type_descriptor_rva{};
        if (!image.add_rva(entries_rva, static_cast<std::size_t>(index) * sizeof(std::uint32_t),
                           entry_rva) ||
            !image.read_u32(entry_rva, catchable_type_rva) ||
            !image.range(catchable_type_rva, kCatchableTypeSize) ||
            !image.add_rva(catchable_type_rva, sizeof(std::uint32_t), entry_rva) ||
            !image.read_u32(entry_rva, type_descriptor_rva) ||
            !image.range(type_descriptor_rva, kTypeDescriptorNameOffset + 1U)) {
            return false;
        }
        if (type_descriptor_rva == handler_type_rva) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool find_catch(const ImageReader& image, const FuncInfo& info,
                              const ExceptionRecordAmd64& record, const std::int32_t state,
                              CatchTarget& target) noexcept {
    for (std::uint32_t index = 0; index < info.try_block_count; ++index) {
        std::uint32_t entry{};
        std::uint32_t try_high_entry{};
        std::uint32_t catch_high_entry{};
        std::uint32_t handler_count_entry{};
        std::uint32_t handler_map_entry{};
        std::int32_t try_low{};
        std::int32_t try_high{};
        std::int32_t catch_high{};
        std::uint32_t handler_count{};
        std::uint32_t handler_map{};
        if (!image.add_rva(info.try_block_map,
                           static_cast<std::size_t>(index) * kTryBlockEntrySize, entry) ||
            !image.add_rva(entry, 4U, try_high_entry) ||
            !image.add_rva(entry, 8U, catch_high_entry) ||
            !image.add_rva(entry, 12U, handler_count_entry) ||
            !image.add_rva(entry, 16U, handler_map_entry) ||
            !image.read_i32(entry, try_low) || !image.read_i32(try_high_entry, try_high) ||
            !image.read_i32(catch_high_entry, catch_high) ||
            !image.read_u32(handler_count_entry, handler_count) ||
            !image.read_u32(handler_map_entry, handler_map) ||
            !valid_state(try_low, info) || !valid_state(try_high, info) ||
            !valid_state(catch_high, info) || try_low > try_high ||
            handler_count > kMaxHandlersPerTry ||
            !image.range_for_count(handler_map, handler_count, kHandlerMapEntrySize)) {
            return false;
        }
        if (state < try_low || state > catch_high) {
            continue;
        }
        for (std::uint32_t handler_index = 0; handler_index < handler_count;
             ++handler_index) {
            std::uint32_t handler{};
            std::uint32_t type_entry{};
            std::uint32_t handler_rva_entry{};
            std::uint32_t adjectives{};
            std::uint32_t type_rva{};
            std::uint32_t handler_rva{};
            if (!image.add_rva(handler_map,
                               static_cast<std::size_t>(handler_index) * kHandlerMapEntrySize,
                               handler) ||
                !image.add_rva(handler, 4U, type_entry) ||
                !image.add_rva(handler, 12U, handler_rva_entry) ||
                !image.read_u32(handler, adjectives) || !image.read_u32(type_entry, type_rva) ||
                !image.read_u32(handler_rva_entry, handler_rva) ||
                !image.range(handler_rva, 1U)) {
                return false;
            }
            if ((adjectives & kCatchAll) != 0U) {
                if (type_rva != 0U) {
                    return false;
                }
                target = {.handler_rva = handler_rva, .scope_index = index, .catch_all = true};
                return true;
            }
            if (type_rva != 0U &&
                image.range(type_rva, kTypeDescriptorNameOffset + 1U) &&
                thrown_type_matches(image, record, type_rva)) {
                target = {.handler_rva = handler_rva, .scope_index = index, .catch_all = false};
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] bool current_state(const ImageReader& image, const FuncInfo& info,
                                 const std::uint32_t control_rva,
                                 std::int32_t& state) noexcept {
    state = kInvalidState;
    bool found = false;
    for (std::uint32_t index = 0; index < info.ip_map_count; ++index) {
        std::uint32_t entry{};
        std::uint32_t state_entry{};
        std::uint32_t ip{};
        std::int32_t candidate{};
        if (!image.add_rva(info.ip_map,
                           static_cast<std::size_t>(index) * kIpMapEntrySize, entry) ||
            !image.add_rva(entry, 4U, state_entry) || !image.read_u32(entry, ip) ||
            !image.read_i32(state_entry, candidate)) {
            return false;
        }
        if (ip > control_rva) {
            break;
        }
        state = candidate;
        found = true;
    }
    return !found || valid_state(state, info);
}

}  // namespace

bool is_supported_cxx_handler_data(void* const handler_data) noexcept {
    const GuestUnwindView view = current_guest_unwind_view();
    ImageReader image{view};
    std::uint32_t handler_data_rva{};
    std::uint32_t func_info_rva{};
    if (!image.rva_of(handler_data, handler_data_rva) ||
        !image.read_u32(handler_data_rva, func_info_rva)) {
        return false;
    }
    FuncInfo info{};
    return read_func_info(image, func_info_rva, info) &&
           validate_unwind_map(image, info) && validate_ip_map(image, info);
}

std::int32_t cxx_frame_handler3(
    ExceptionRecordAmd64* const exception_record, void* const establisher_frame,
    ContextAmd64* const context_record,
    DispatcherContextAmd64* const dispatcher_context) noexcept {
    (void)establisher_frame;
    if (exception_record == nullptr || context_record == nullptr || dispatcher_context == nullptr ||
        !validate_mapped_range(exception_record, sizeof(*exception_record), false) ||
        !validate_mapped_range(context_record, sizeof(*context_record), true) ||
        !validate_mapped_range(dispatcher_context, sizeof(*dispatcher_context), true)) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-dispatcher-data");
        return kExceptionContinueSearch;
    }
    if (exception_record->code != kCxxException) {
        return kExceptionContinueSearch;
    }
    const bool unwinding = (exception_record->flags & kExceptionUnwinding) != 0U;

    const GuestUnwindView view = current_guest_unwind_view();
    ImageReader image{view};
    std::uint32_t handler_data_rva{};
    std::uint32_t func_info_rva{};
    const auto handler_data = static_cast<const std::byte*>(dispatcher_context->handler_data);
    if (!image.rva_of(handler_data, handler_data_rva) ||
        !image.read_u32(handler_data_rva, func_info_rva)) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-handler-data");
        return kExceptionContinueSearch;
    }

    FuncInfo info{};
    if (!read_func_info(image, func_info_rva, info) || !validate_unwind_map(image, info) ||
        !validate_ip_map(image, info)) {
        trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected", "invalid-func-info");
        return kExceptionContinueSearch;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(view.image_base);
    if (dispatcher_context->control_pc < base ||
        dispatcher_context->control_pc - base > std::numeric_limits<std::uint32_t>::max()) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-control-pc");
        return kExceptionContinueSearch;
    }
    std::int32_t state{};
    if (!current_state(image, info, static_cast<std::uint32_t>(dispatcher_context->control_pc - base),
                       state)) {
        trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected", "invalid-ip-state");
        return kExceptionContinueSearch;
    }
    if (unwinding) {
        // Para frames intermediários a rotina de unwind continua a procura
        // sem transferir para um funclet. A etapa atual executa o cleanup do
        // frame-alvo antes do catch; isso mantém a continuação em um único
        // contexto convidado validado.
        if (dispatcher_context->target_ip == 0U) {
            return kExceptionContinueSearch;
        }
        CleanupPlan plan{};
        if (!build_cleanup_plan(image, info, state, plan) || plan.count == 0U) {
            trace_cxx_eh_state(diagnostics::TraceLevel::Info, "no-supported-cleanup",
                               static_cast<std::uint32_t>(dispatcher_context->control_pc - base),
                               state);
            return kExceptionContinueSearch;
        }
        dispatcher_context->target_ip = base + plan.actions[0];
        g_cxx_cleanup_actions = plan.actions;
        g_cxx_cleanup_count = plan.count;
        g_cxx_cleanup_index = 0U;
        g_cxx_cleanup_target = dispatcher_context->target_ip;
        trace_cxx_eh(diagnostics::TraceLevel::Info, "matched", "termination-cleanup");
        return kExceptionExecuteHandler;
    }
    CatchTarget target{};
    if (!find_catch(image, info, *exception_record, state, target)) {
        trace_cxx_eh_state(
            diagnostics::TraceLevel::Info, "no-supported-catch",
            static_cast<std::uint32_t>(dispatcher_context->control_pc - base), state);
        return kExceptionContinueSearch;
    }

    dispatcher_context->scope_index = target.scope_index;
    dispatcher_context->target_ip = base + target.handler_rva;
    g_cxx_catch_target = dispatcher_context->target_ip;
    trace_cxx_eh(diagnostics::TraceLevel::Info, "matched",
                 target.catch_all ? "catch-all" : "catch-typed");
    return kExceptionExecuteHandler;
}

bool prepare_cxx_catch_transfer(ContextAmd64& context, void* const establisher_frame,
                                void* const target_ip) noexcept {
    const std::uint64_t target = reinterpret_cast<std::uintptr_t>(target_ip);
    if (target == 0U || target != g_cxx_catch_target ||
        !validate_guest_stack_range(reinterpret_cast<void*>(context.rsp), sizeof(std::uint64_t), true)) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-catch-transfer");
        return false;
    }
    context.rdx = reinterpret_cast<std::uintptr_t>(establisher_frame);
    g_cxx_catch_context = context;
    *reinterpret_cast<std::uint64_t*>(context.rsp) =
        reinterpret_cast<std::uintptr_t>(&tl_cxx_catch_return_trampoline);
    g_cxx_catch_context_ready = true;
    g_cxx_funclet_active = true;
    g_cxx_cleanup_context_ready = false;
    g_cxx_catch_target = 0U;
    return true;
}

bool prepare_cxx_cleanup_transfer(ContextAmd64& action_context,
                                  const ContextAmd64& catch_context,
                                  void* const establisher_frame,
                                  void* const cleanup_ip,
                                  void* const catch_ip) noexcept {
    const std::uint64_t cleanup_target = reinterpret_cast<std::uintptr_t>(cleanup_ip);
    const std::uint64_t catch_target = reinterpret_cast<std::uintptr_t>(catch_ip);
    if (cleanup_target == 0U || cleanup_target != g_cxx_cleanup_target ||
        catch_target == 0U || catch_target != g_cxx_catch_target ||
        !validate_guest_stack_range(reinterpret_cast<void*>(action_context.rsp),
                                    sizeof(std::uint64_t), true) ||
        !validate_guest_stack_range(reinterpret_cast<void*>(catch_context.rsp),
                                    sizeof(std::uint64_t), true)) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-cleanup-transfer");
        return false;
    }

    action_context.rip = cleanup_target;
    action_context.rdx = reinterpret_cast<std::uintptr_t>(establisher_frame);
    *reinterpret_cast<std::uint64_t*>(action_context.rsp) =
        reinterpret_cast<std::uintptr_t>(&tl_cxx_cleanup_return_trampoline);

    g_cxx_catch_context = catch_context;
    g_cxx_catch_context.rip = catch_target;
    g_cxx_catch_context.rdx = reinterpret_cast<std::uintptr_t>(establisher_frame);
    *reinterpret_cast<std::uint64_t*>(g_cxx_catch_context.rsp) =
        reinterpret_cast<std::uintptr_t>(&tl_cxx_catch_return_trampoline);
    g_cxx_catch_context_ready = true;
    g_cxx_cleanup_context_ready = true;
    g_cxx_funclet_active = true;
    g_cxx_cleanup_establisher = reinterpret_cast<std::uintptr_t>(establisher_frame);
    g_cxx_cleanup_target = 0U;
    return true;
}

extern "C" [[noreturn]] void tl_cxx_cleanup_return_from_asm(
    const std::uint64_t stack_pointer) noexcept {
    const GuestUnwindView view = current_guest_unwind_view();
    const auto base = reinterpret_cast<std::uintptr_t>(view.image_base);
    if (view.image_base == nullptr ||
        !validate_guest_stack_range(reinterpret_cast<void*>(stack_pointer), 1U, false) ||
        !g_cxx_cleanup_context_ready || !g_cxx_catch_context_ready ||
        g_cxx_cleanup_count == 0U || g_cxx_cleanup_index >= g_cxx_cleanup_count ||
        g_cxx_catch_context.rip < base ||
        g_cxx_catch_context.rip - base >= view.image_size) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-cleanupret-target");
        tl_ExitThread(kCxxException);
        std::abort();
    }

    if (g_cxx_cleanup_index + 1U < g_cxx_cleanup_count) {
        const std::size_t next_index = g_cxx_cleanup_index + 1U;
        const std::uint32_t next_rva = g_cxx_cleanup_actions[next_index];
        if (next_rva >= view.image_size ||
            base > std::numeric_limits<std::uintptr_t>::max() - next_rva ||
            !validate_guest_stack_range(reinterpret_cast<void*>(stack_pointer),
                                        sizeof(std::uint64_t), true)) {
            trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected",
                         "invalid-chained-cleanup-target");
            tl_ExitThread(kCxxException);
            std::abort();
        }
        ContextAmd64 next_context = g_cxx_catch_context;
        next_context.rip = base + next_rva;
        next_context.rsp = stack_pointer;
        next_context.rdx = g_cxx_cleanup_establisher;
        *reinterpret_cast<std::uint64_t*>(next_context.rsp) =
            reinterpret_cast<std::uintptr_t>(&tl_cxx_cleanup_return_trampoline);
        g_cxx_cleanup_index = next_index;
        g_cxx_cleanup_target = next_context.rip;
        g_cxx_cleanup_context_ready = true;
        tl_restore_guest_context_and_jump(&next_context);
    }

    g_cxx_cleanup_context_ready = false;
    g_cxx_funclet_active = false;
    // O cleanup funclet chamou este callback usando temporariamente a pilha
    // convidada. O prólogo/locals do callback podem ter coberto a palavra de
    // retorno reservada no frame original; reescreva-a antes do salto para o
    // catch, sem confiar no conteúdo que atravessou a fronteira host/guest.
    if (!validate_guest_stack_range(reinterpret_cast<void*>(g_cxx_catch_context.rsp),
                                    sizeof(std::uint64_t), true)) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-catch-return-slot");
        tl_ExitThread(kCxxException);
        std::abort();
    }
    *reinterpret_cast<std::uint64_t*>(g_cxx_catch_context.rsp) =
        reinterpret_cast<std::uintptr_t>(&tl_cxx_catch_return_trampoline);
    g_cxx_cleanup_target = 0U;
    g_cxx_cleanup_establisher = 0U;
    g_cxx_cleanup_actions.fill(0U);
    g_cxx_cleanup_count = 0U;
    g_cxx_cleanup_index = 0U;
    tl_restore_guest_context_and_jump(&g_cxx_catch_context);
}

extern "C" [[noreturn]] void tl_cxx_catch_return_from_asm(
    const std::uint64_t target_ip, const std::uint64_t stack_pointer) noexcept {
    const GuestUnwindView view = current_guest_unwind_view();
    const auto base = reinterpret_cast<std::uintptr_t>(view.image_base);
    if (view.image_base == nullptr || target_ip < base || target_ip - base >= view.image_size ||
        !validate_guest_stack_range(reinterpret_cast<void*>(stack_pointer), 1U, false) ||
        !g_cxx_catch_context_ready) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-catchret-target");
        tl_ExitThread(kCxxException);
        std::abort();
    }
    g_cxx_catch_context.rip = target_ip;
    g_cxx_catch_context.rsp = stack_pointer;
    g_cxx_catch_context_ready = false;
    g_cxx_funclet_active = false;
    tl_restore_guest_context_and_jump(&g_cxx_catch_context);
}

bool cxx_eh_funclet_active() noexcept {
    return g_cxx_funclet_active;
}

}  // namespace tradutorlinux::runtime
