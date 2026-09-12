#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
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
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_MSABI void* tl_ImageNtHeader(void* const base) noexcept {
    if (base == nullptr || !mapped_guest_range(base, 0x40, false)) {
        return nullptr;
    }
    const auto* const dos = static_cast<const std::uint8_t*>(base);
    if (dos[0] != 'M' || dos[1] != 'Z') {
        return nullptr;
    }
    const std::uint32_t e_lfanew = *reinterpret_cast<const std::uint32_t*>(dos + 0x3C);
    if (!mapped_guest_range(dos + e_lfanew, 4, false)) {
        return nullptr;
    }
    if (*reinterpret_cast<const std::uint32_t*>(dos + e_lfanew) != 0x00004550U) { // 'PE\0\0'
        return nullptr;
    }
    return const_cast<void*>(static_cast<const void*>(dos + e_lfanew));
}

} // extern "C"
} // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_dbghelp_module() {
    static const ExportedFunction kDbghelpExports[] = {
        {"SymFromAddr", 1, reinterpret_cast<std::uintptr_t>(&tl_SymFromAddr),
         ExportSupport::Stub},
        {"ImageNtHeader", 2, reinterpret_cast<std::uintptr_t>(&tl_ImageNtHeader)},
    };
    static const InternalModule kDbghelpModule{"dbghelp.dll", kDbghelpExports};
    register_module(kDbghelpModule);
}

} // namespace tradutorlinux::loader
