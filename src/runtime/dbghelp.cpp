#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "core/runtime_state_common.hpp"

#include <array>
#include <cstring>
#include <limits>

namespace tradutorlinux {

extern "C" {

TL_MSABI int tl_SymFromAddr(void* process, std::uint64_t address, std::uint64_t* displacement, void* symbol) noexcept {
    (void)process;
    (void)address;
    if (symbol != nullptr) {
        // SYMBOL_INFO: SizeOfStruct (4), TypeIndex (4), Reserved[2] (8), Index (4), Size (4), ModBase (8), Flags (4), Value (8), Address (8), Register (4), Scope (4), Tag (4), NameLen (4), MaxNameLen (4), Name[1]
        // Para o stub, apenas copia SizeOfStruct de uma estrutura convidada.
        std::uint32_t size_of_struct = 0;
        if (!read_guest_value(symbol, size_of_struct)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    if (displacement != nullptr && !write_guest_value(displacement, std::uint64_t{0})) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_MSABI void* tl_ImageNtHeader(void* const base) noexcept {
    if (base == nullptr) {
        return nullptr;
    }
    std::array<std::uint8_t, 0x40> dos{};
    if (runtime::read_guest_memory(base, dos.data(), dos.size()).status !=
        runtime::GuestMemoryAccessStatus::Success ||
        dos[0] != 'M' || dos[1] != 'Z') {
        return nullptr;
    }
    std::uint32_t e_lfanew = 0;
    std::memcpy(&e_lfanew, dos.data() + 0x3C, sizeof(e_lfanew));
    const std::uintptr_t base_address = reinterpret_cast<std::uintptr_t>(base);
    if (e_lfanew > std::numeric_limits<std::uintptr_t>::max() - base_address) {
        return nullptr;
    }
    auto* const nt_header = reinterpret_cast<const std::uint32_t*>(base_address + e_lfanew);
    std::uint32_t signature = 0;
    if (!read_guest_value(nt_header, signature) || signature != 0x00004550U) { // 'PE\0\0'
        return nullptr;
    }
    return const_cast<std::uint32_t*>(nt_header);
}

} // extern "C"
} // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_dbghelp_module() {
    static const ExportedFunction kDbghelpExports[] = {
        {"SymFromAddr", 1, reinterpret_cast<std::uintptr_t>(&tl_SymFromAddr),
         ExportSupport::Stub},
        {"ImageNtHeader", 2, reinterpret_cast<std::uintptr_t>(&tl_ImageNtHeader), ExportSupport::Full},
    };
    static const InternalModule kDbghelpModule{"dbghelp.dll", kDbghelpExports};
    register_module(kDbghelpModule);
}

} // namespace tradutorlinux::loader
