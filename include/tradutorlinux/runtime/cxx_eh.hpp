#pragma once

#include "tradutorlinux/runtime/unwind.hpp"

#include <cstdint>

namespace tradutorlinux::runtime {

// Subconjunto seguro do handler MSVC x64. O parser aceita somente FuncInfo v3
// relativo à imagem, cleanups de término com funclets retornáveis e handlers
// catch-all; metadados desconhecidos continuam em busca de outro handler e
// acabam no caminho controlado de exceção não tratada.
[[nodiscard]] std::int32_t cxx_frame_handler3(
    ExceptionRecordAmd64* exception_record, void* establisher_frame,
    ContextAmd64* context_record, DispatcherContextAmd64* dispatcher_context) noexcept;

// Prepara a transferência para um catch funclet. O estado é thread-local:
// nenhum ponteiro do convidado atravessa a chamada de retorno do trampoline.
[[nodiscard]] bool prepare_cxx_catch_transfer(ContextAmd64& context,
                                              void* establisher_frame,
                                              void* target_ip) noexcept;

// Prepara um cleanup funclet antes de transferir para o catch selecionado.
// action_context é o contexto já desempilhado para chamar o cleanup; o
// contexto original do frame é preservado para a entrada posterior no catch.
[[nodiscard]] bool prepare_cxx_cleanup_transfer(ContextAmd64& action_context,
                                                const ContextAmd64& catch_context,
                                                void* establisher_frame,
                                                void* cleanup_ip,
                                                void* catch_ip) noexcept;

extern "C" [[noreturn]] void tl_cxx_catch_return_from_asm(
    std::uint64_t target_ip, std::uint64_t stack_pointer) noexcept;
extern "C" [[noreturn]] void tl_cxx_catch_return_trampoline() noexcept;
extern "C" [[noreturn]] void tl_cxx_cleanup_return_from_asm(
    std::uint64_t stack_pointer) noexcept;
extern "C" [[noreturn]] void tl_cxx_cleanup_return_trampoline() noexcept;

}  // namespace tradutorlinux::runtime
