#include "tradutorlinux/runtime/unwind.hpp"

#include "runtime_context.hpp"

#include <cstring>
#include <limits>
#include <optional>

namespace tradutorlinux::runtime {
namespace {

struct UnwindImageView {
    const std::byte* base{};
    std::size_t size{};
    std::uint32_t exception_directory_rva{};
    std::span<const pe::RuntimeFunction> functions{};
};

thread_local UnwindImageView g_unwind_image{};

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
    if (g_unwind_image.base == nullptr || pc < base || pc - base >= g_unwind_image.size) {
        return std::nullopt;
    }
    const std::uint64_t rva = pc - base;
    for (std::size_t index = 0; index < g_unwind_image.functions.size(); ++index) {
        const pe::RuntimeFunction& function = g_unwind_image.functions[index];
        if (rva >= function.begin_rva && rva < function.end_rva) {
            return index;
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
    if (!validate_mapped_range(source, sizeof(out), false)) {
        return false;
    }
    std::memcpy(&out, source, sizeof(out));
    return true;
}

[[nodiscard]] bool read_m128(const std::uint64_t address, M128A& out) noexcept {
    const auto* const source = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address));
    if (!validate_mapped_range(source, sizeof(out), false)) {
        return false;
    }
    std::memcpy(&out, source, sizeof(out));
    return true;
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

}  // namespace tradutorlinux::runtime

namespace tradutorlinux {

extern "C" TL_MSABI std::uint32_t* tl_RtlLookupFunctionEntry(
    const std::uint64_t control_pc, std::uint64_t* const image_base, void*) noexcept {
    if (image_base == nullptr || !runtime::validate_mapped_range(image_base, sizeof(*image_base), true)) {
        runtime::trace_unwind_failure("RtlLookupFunctionEntry", "ImageBase inválido");
        return nullptr;
    }
    *image_base = 0;
    const std::optional<std::size_t> found = runtime::find_function_by_pc(control_pc);
    if (!found.has_value()) {
        return nullptr;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(runtime::g_unwind_image.base);
    *image_base = base;
    return reinterpret_cast<std::uint32_t*>(
        const_cast<std::byte*>(runtime::g_unwind_image.base) +
        runtime::g_unwind_image.exception_directory_rva + *found * 12U);
}

extern "C" TL_MSABI void* tl_RtlPcToFileHeader(void* const pc_value,
                                                void** const base_of_image) noexcept {
    if (base_of_image == nullptr ||
        !runtime::validate_mapped_range(base_of_image, sizeof(*base_of_image), true)) {
        runtime::trace_unwind_failure("RtlPcToFileHeader", "BaseOfImage inválido");
        return nullptr;
    }
    *base_of_image = nullptr;
    const std::uint64_t pc = reinterpret_cast<std::uintptr_t>(pc_value);
    const std::uint64_t base = reinterpret_cast<std::uintptr_t>(runtime::g_unwind_image.base);
    if (runtime::g_unwind_image.base == nullptr || pc < base ||
        pc - base >= runtime::g_unwind_image.size) {
        return nullptr;
    }
    *base_of_image = const_cast<std::byte*>(runtime::g_unwind_image.base);
    return *base_of_image;
}

extern "C" TL_MSABI void* tl_RtlVirtualUnwind(
    const std::uint32_t handler_type, const std::uint64_t image_base,
    const std::uint64_t control_pc, std::uint32_t* const function_entry,
    runtime::ContextAmd64* const context, void** const handler_data,
    std::uint64_t* const establisher_frame, void*) noexcept {
    if (context == nullptr ||
        !runtime::validate_mapped_range(context, sizeof(*context), true)) {
        runtime::trace_unwind_failure("RtlVirtualUnwind", "ContextRecord inválido");
        return nullptr;
    }
    if (handler_data != nullptr &&
        !runtime::validate_mapped_range(handler_data, sizeof(*handler_data), true)) {
        runtime::trace_unwind_failure("RtlVirtualUnwind", "HandlerData inválido");
        return nullptr;
    }
    if (establisher_frame != nullptr &&
        !runtime::validate_mapped_range(establisher_frame, sizeof(*establisher_frame), true)) {
        runtime::trace_unwind_failure("RtlVirtualUnwind", "EstablisherFrame inválido");
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

    runtime::ContextAmd64 updated = *context;
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
    if (establisher_frame != nullptr) {
        *establisher_frame = updated.rsp;
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
    *context = updated;

    const std::uint8_t flags = terminal->unwind.flags;
    if (handler_type == 0U || (flags & (0x1U | 0x2U)) == 0U ||
        (handler_type & flags) == 0U) {
        if (handler_data != nullptr) {
            *handler_data = nullptr;
        }
        return nullptr;
    }
    if (handler_data != nullptr) {
        *handler_data = const_cast<std::byte*>(runtime::g_unwind_image.base) +
                        terminal->unwind.handler_data_rva;
    }
    return const_cast<std::byte*>(runtime::g_unwind_image.base) + terminal->unwind.handler_rva;
}

}  // namespace tradutorlinux
