#include "tradutorlinux/runtime/winapi.hpp"
#include "runtime_context.hpp"

#include <cstdint>
#include <cstring>

namespace tradutorlinux {

extern "C" {

TL_MSABI std::uint32_t tl_GetAdaptersInfo(void* AdapterInfo, std::uint32_t* OutBufLen) noexcept {
    if (OutBufLen == nullptr || !mapped_guest_range(OutBufLen, sizeof(std::uint32_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    // Se buffer nulo ou tamanho 0, retorna tamanho necessário (0 para stub vazio)
    if (AdapterInfo == nullptr) {
        *OutBufLen = 0;
        set_last_error(abi::kErrorSuccess);
        return 0; // ERROR_SUCCESS com 0 adapters
    }
    if (!mapped_guest_range(AdapterInfo, *OutBufLen, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    // Preenche com zero adapters (lista vazia)
    std::memset(AdapterInfo, 0, *OutBufLen);
    *OutBufLen = 0;
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI std::uint32_t tl_GetAdaptersAddresses(std::uint32_t Family, std::uint32_t Flags, void* Reserved,
                                               void* AdapterAddresses, std::uint32_t* SizePointer) noexcept {
    (void)Family;
    (void)Flags;
    (void)Reserved;
    if (SizePointer == nullptr || !mapped_guest_range(SizePointer, sizeof(std::uint32_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    if (AdapterAddresses == nullptr) {
        *SizePointer = 0;
        set_last_error(abi::kErrorSuccess);
        return 0;
    }
    if (!mapped_guest_range(AdapterAddresses, *SizePointer, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    std::memset(AdapterAddresses, 0, *SizePointer);
    *SizePointer = 0;
    set_last_error(abi::kErrorSuccess);
    return 0; // ERROR_SUCCESS
}

TL_MSABI std::uint32_t tl_if_nametoindex(const char* ifname) noexcept {
    if (ifname == nullptr || !mapped_guest_cstring(ifname)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // Stub: retorna 1 para qualquer interface válida (ex.: "lo", "eth0")
    if (ifname[0] == '\0') {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

} // extern "C"

} // namespace tradutorlinux
