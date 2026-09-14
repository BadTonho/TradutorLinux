#pragma once

#include "tradutorlinux/runtime/unwind.hpp"

#include <cstdint>

namespace tradutorlinux::runtime {

// Subconjunto seguro dos handlers MSVC x64. O parser aceita FuncInfo v3 e a
// representação comprimida FH4 relativa à imagem; conversões e metadados
// desconhecidos continuam em busca de outro handler e acabam no caminho
// controlado de exceção não tratada.
[[nodiscard]] std::int32_t cxx_frame_handler3(
    ExceptionRecordAmd64* exception_record, void* establisher_frame,
    ContextAmd64* context_record, DispatcherContextAmd64* dispatcher_context) noexcept;

// Identifica as representações C++ que o runtime consegue interpretar.
// Handlers estáticos de __C_specific_handler podem receber a mesma exceção,
// mas usam outro formato de handler data.
[[nodiscard]] bool is_supported_cxx_handler_data(void* handler_data) noexcept;

// Identifica a variante FH4 para que o despachante possa usar o decodificador
// do runtime em vez de executar __GSHandlerCheck_EH4 do convidado.
[[nodiscard]] bool is_fh4_cxx_handler_data(void* handler_data) noexcept;

// Handler host-side da ABI FH4. O código de catch continua sendo executado
// pelo convidado; somente a leitura das tabelas comprimidas e a seleção do
// funclet atravessam esta fronteira.
[[nodiscard]] std::int32_t cxx_frame_handler4(
    ExceptionRecordAmd64* exception_record, void* establisher_frame,
    ContextAmd64* context_record, DispatcherContextAmd64* dispatcher_context) noexcept;

// Prepara a transferência para um catch funclet. O estado é thread-local:
// nenhum ponteiro do convidado atravessa a chamada de retorno do trampoline.
[[nodiscard]] bool prepare_cxx_catch_transfer(ContextAmd64& context,
                                              void* establisher_frame,
                                              void* target_ip) noexcept;

// Indica que a execução convidada está dentro de um funclet C++ que ainda não
// retornou. O dispatcher rejeita reentrada que o subconjunto atual de WinEH
// não consegue propagar com segurança.
[[nodiscard]] bool cxx_eh_funclet_active() noexcept;

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
