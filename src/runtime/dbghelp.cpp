#include "tradutorlinux/runtime/winapi.hpp"
#include "runtime_context.hpp"

namespace tradutorlinux {

extern "C" {

TL_MSABI int tl_SymFromAddr(void* process, std::uint64_t address, std::uint64_t* displacement, void* symbol) noexcept {
    (void)process;
    (void)address;
    if (displacement != nullptr && mapped_guest_range(displacement, sizeof(std::uint64_t), true)) {
        *displacement = 0;
    }
    if (symbol != nullptr) {
        // SYMBOL_INFO: SizeOfStruct (4), TypeIndex (4), Reserved[2] (8), Index (4), Size (4), ModBase (8), Flags (4), Value (8), Address (8), Register (4), Scope (4), Tag (4), NameLen (4), MaxNameLen (4), Name[1]
        // Para stub, apenas valida que symbol aponta para memória gravável de pelo menos 4 bytes (SizeOfStruct)
        if (!mapped_guest_range(symbol, sizeof(std::uint32_t), false)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    set_last_error(abi::kErrorSuccess);
    // Retorna 0 (falha) para indicar símbolo não encontrado, mas não é erro fatal; algumas apps tratam falha como ok
    // Para aumentar compatibilidade, retornamos 0 mas com last error success? Melhor retornar 0 e deixar caller tratar.
    // Para teste de fixture, esperamos 0 (não encontrado) mas não crash.
    return 0;
}

} // extern "C"

} // namespace tradutorlinux
