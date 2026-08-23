#pragma once

#include "tradutorlinux/pe/pe_reader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace tradutorlinux::runtime {

struct alignas(16) M128A {
    std::uint64_t low{};
    std::int64_t high{};
};

// Layout de CONTEXT para Windows AMD64. A área floating corresponde a
// Header[2], Legacy[8], Xmm0..Xmm15 e os 96 bytes reservados; portanto XmmN
// fica em floating[10 + N].
struct alignas(16) ContextAmd64 {
    std::array<std::uint64_t, 6> home{};
    std::uint32_t context_flags{};
    std::uint32_t mx_csr{};
    std::array<std::uint16_t, 6> segments{};
    std::uint32_t e_flags{};
    std::array<std::uint64_t, 6> debug{};
    std::uint64_t rax{};
    std::uint64_t rcx{};
    std::uint64_t rdx{};
    std::uint64_t rbx{};
    std::uint64_t rsp{};
    std::uint64_t rbp{};
    std::uint64_t rsi{};
    std::uint64_t rdi{};
    std::uint64_t r8{};
    std::uint64_t r9{};
    std::uint64_t r10{};
    std::uint64_t r11{};
    std::uint64_t r12{};
    std::uint64_t r13{};
    std::uint64_t r14{};
    std::uint64_t r15{};
    std::uint64_t rip{};
    std::array<M128A, 32> floating{};
    std::array<M128A, 26> vector_register{};
    std::uint64_t vector_control{};
    std::uint64_t debug_control{};
    std::uint64_t last_branch_to_rip{};
    std::uint64_t last_branch_from_rip{};
    std::uint64_t last_exception_to_rip{};
    std::uint64_t last_exception_from_rip{};
};

static_assert(alignof(ContextAmd64) == 16);
static_assert(offsetof(ContextAmd64, rax) == 120);
static_assert(offsetof(ContextAmd64, rsp) == 152);
static_assert(offsetof(ContextAmd64, rip) == 248);
static_assert(offsetof(ContextAmd64, floating) == 256);
static_assert(offsetof(ContextAmd64, vector_register) == 768);
static_assert(sizeof(ContextAmd64) == 1232);

// Layouts Win64 usados pelo despachante SEH. Eles ficam aqui, junto ao
// CONTEXT, pois cruzam a fronteira ABI Microsoft x64 e podem ser acessados
// diretamente pelo código PE convidado.
struct ExceptionRecordAmd64 {
    std::uint32_t code{};
    std::uint32_t flags{};
    ExceptionRecordAmd64* nested_record{};
    void* address{};
    std::uint32_t parameter_count{};
    std::uint32_t reserved{};
    std::array<std::uint64_t, 15> parameters{};
};
static_assert(sizeof(ExceptionRecordAmd64) == 152);
static_assert(offsetof(ExceptionRecordAmd64, parameters) == 32);

struct ExceptionPointersAmd64 {
    ExceptionRecordAmd64* exception_record{};
    ContextAmd64* context_record{};
};
static_assert(sizeof(ExceptionPointersAmd64) == 16);

struct DispatcherContextAmd64 {
    std::uint64_t control_pc{};
    std::uint64_t image_base{};
    std::uint32_t* function_entry{};
    std::uint64_t establisher_frame{};
    std::uint64_t target_ip{};
    ContextAmd64* context_record{};
    void* language_handler{};
    void* handler_data{};
    void* history_table{};
    std::uint32_t scope_index{};
    std::uint32_t reserved{};
};
static_assert(sizeof(DispatcherContextAmd64) == 80);

constexpr std::uint32_t kExceptionNoncontinuable = 0x1U;
constexpr std::uint32_t kExceptionUnwinding = 0x2U;
constexpr std::uint32_t kExceptionTargetUnwind = 0x20U;
constexpr std::int32_t kExceptionContinueExecution = 0;
constexpr std::int32_t kExceptionContinueSearch = 1;
constexpr std::int32_t kExceptionExecuteHandler = 4;
constexpr std::int32_t kVectoredContinueExecution = 0;
constexpr std::int32_t kVectoredContinueSearch = -1;

constexpr std::uint32_t kContextAmd64 = 0x00100000U;
constexpr std::uint32_t kContextControl = kContextAmd64 | 0x1U;
constexpr std::uint32_t kContextInteger = kContextAmd64 | 0x2U;
constexpr std::uint32_t kContextFloatingPoint = kContextAmd64 | 0x8U;
constexpr std::uint32_t kContextFull =
    kContextControl | kContextInteger | kContextFloatingPoint;

// O span pertence ao PeInfo mantido pelo GuestProcess; ele deve continuar vivo
// até clear_guest_unwind_view(). O contexto é thread-local porque filhos e
// threads do runtime podem executar imagens distintas.
void set_guest_unwind_view(const void* image_base, std::size_t image_size,
                           std::uint32_t exception_directory_rva,
                           std::span<const pe::RuntimeFunction> functions) noexcept;
void clear_guest_unwind_view() noexcept;

struct GuestUnwindView {
    const void* image_base{};
    std::size_t image_size{};
    std::uint32_t exception_directory_rva{};
    std::span<const pe::RuntimeFunction> functions{};
};

[[nodiscard]] GuestUnwindView current_guest_unwind_view() noexcept;
void restore_guest_unwind_view(GuestUnwindView view) noexcept;

// Registro de VEH separado das APIs KERNEL32 para que o despachante e a
// superfície de imports compartilhem o mesmo estado por processo.
void* add_vectored_exception_handler(std::uint32_t first, void* handler) noexcept;
std::uint32_t remove_vectored_exception_handler(void* handle) noexcept;

// Entrada chamada pelo pequeno stub assembly de RaiseException. Nunca retorna:
// ou restaura um CONTEXT convidado ou encerra o processo por ExitProcess.
[[noreturn]] void dispatch_raised_exception(ContextAmd64 context, std::uint32_t code,
                                            std::uint32_t flags, std::uint32_t parameter_count,
                                            const std::uint64_t* parameters) noexcept;

// Reutilizado por __C_specific_handler e RtlUnwind(Ex). Também não retorna
// quando os argumentos são válidos.
[[noreturn]] void unwind_to_target(void* target_frame, void* target_ip,
                                   ExceptionRecordAmd64* exception_record,
                                   void* return_value, ContextAmd64* context,
                                   void* history_table) noexcept;

std::int32_t c_specific_handler(ExceptionRecordAmd64* exception_record,
                                void* establisher_frame, ContextAmd64* context_record,
                                DispatcherContextAmd64* dispatcher_context) noexcept;

}  // namespace tradutorlinux::runtime

namespace tradutorlinux {

extern "C" {

// Entrada em assembly MS x64: captura o frame do chamador diretamente para
// não perder RIP/RSP no prólogo de uma função System V do hospedeiro.
void tl_RtlCaptureContext(runtime::ContextAmd64* context) noexcept
    __attribute__((ms_abi));

std::uint32_t* tl_RtlLookupFunctionEntry(std::uint64_t control_pc,
                                         std::uint64_t* image_base,
                                         void* history_table) noexcept
    __attribute__((ms_abi));
void* tl_RtlVirtualUnwind(std::uint32_t handler_type, std::uint64_t image_base,
                          std::uint64_t control_pc, std::uint32_t* function_entry,
                          runtime::ContextAmd64* context, void** handler_data,
                          std::uint64_t* establisher_frame, void* context_pointers) noexcept
    __attribute__((ms_abi));
void* tl_RtlPcToFileHeader(void* pc_value, void** base_of_image) noexcept
    __attribute__((ms_abi));
void tl_RtlUnwind(void* target_frame, void* target_ip,
                  runtime::ExceptionRecordAmd64* exception_record,
                  void* return_value) noexcept __attribute__((ms_abi));
void tl_RtlUnwindEx(void* target_frame, void* target_ip,
                    runtime::ExceptionRecordAmd64* exception_record,
                    void* return_value, runtime::ContextAmd64* context,
                    void* history_table) noexcept __attribute__((ms_abi));
std::int32_t tl_UnhandledExceptionFilter(runtime::ExceptionPointersAmd64* pointers) noexcept
    __attribute__((ms_abi));

// Implementadas em assembly: precisam observar o frame do chamador antes de
// qualquer prólogo C++ e restaurar registros sem retornar ao runtime.
void tl_RaiseException(std::uint32_t exception_code, std::uint32_t exception_flags,
                       std::uint32_t number_of_arguments,
                       const std::uint64_t* arguments) noexcept __attribute__((ms_abi));
[[noreturn]] void tl_restore_guest_context_and_jump(runtime::ContextAmd64* context) noexcept;

}  // extern "C"

}  // namespace tradutorlinux
