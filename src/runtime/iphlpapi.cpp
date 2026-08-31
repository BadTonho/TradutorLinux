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
    constexpr std::uint32_t kRequired = 640;
    if (AdapterInfo == nullptr) {
        *OutBufLen = kRequired;
        return 111; // ERROR_BUFFER_OVERFLOW
    }
    if (*OutBufLen < kRequired) {
        *OutBufLen = kRequired;
        return 111;
    }
    if (!mapped_guest_range(AdapterInfo, *OutBufLen, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    std::memset(AdapterInfo, 0, *OutBufLen);
    // IP_ADAPTER_INFO minimal: ComboIndex(4) Next(8) AdapterName[260] Description[132] etc.
    // Para o Worker basta retornar 1 adapter com AddressLength 6 e Type 6 (Ethernet)
    struct GuestAdapterInfo {
        void* Next{nullptr};
        std::uint32_t ComboIndex{1};
        char AdapterName[260]{};
        char Description[132]{};
        std::uint32_t AddressLength{6};
        std::uint8_t Address[8]{};
        std::uint32_t Index{1};
        std::uint32_t Type{6};
        std::uint32_t DhcpEnabled{0};
        void* CurrentIpAddress{nullptr};
        struct { void* Next{nullptr}; char IpAddress[16]{}; char IpMask[16]{}; std::uint32_t Context{0}; } IpAddressList{};
        struct { void* Next{nullptr}; char IpAddress[16]{}; char IpMask[16]{}; std::uint32_t Context{0}; } GatewayList{};
        struct { void* Next{nullptr}; char IpAddress[16]{}; char IpMask[16]{}; std::uint32_t Context{0}; } DhcpServer{};
        std::int32_t HaveWins{0};
        struct { void* Next{nullptr}; char IpAddress[16]{}; char IpMask[16]{}; std::uint32_t Context{0}; } PrimaryWinsServer{};
        struct { void* Next{nullptr}; char IpAddress[16]{}; char IpMask[16]{}; std::uint32_t Context{0}; } SecondaryWinsServer{};
        std::uint32_t LeaseObtained{0};
        std::uint32_t LeaseExpires{0};
    };
    auto* info = static_cast<GuestAdapterInfo*>(AdapterInfo);
    info->Next = nullptr;
    std::strcpy(info->AdapterName, "lo");
    std::strcpy(info->Description, "Loopback");
    info->Address[0] = 0x7F; info->Address[1] = 0x00; info->Address[2] = 0x00; info->Address[3] = 0x01;
    *OutBufLen = kRequired;
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
    constexpr std::uint32_t kRequired = 1024;
    if (AdapterAddresses == nullptr) {
        *SizePointer = kRequired;
        return 111; // ERROR_BUFFER_OVERFLOW
    }
    if (*SizePointer < kRequired) {
        *SizePointer = kRequired;
        return 111;
    }
    if (!mapped_guest_range(AdapterAddresses, *SizePointer, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    std::memset(AdapterAddresses, 0, *SizePointer);
    // IP_ADAPTER_ADDRESSES minimal para 127.0.0.1 + ::1
    // Layout simplificado: Length(4) IfIndex(4) Next(8) AdapterName(8) ... FriendlyName(8) Description(8) etc.
    // Para o Worker basta Next==0, Length==kRequired, IfIndex==1, OperStatus==1 e FriendlyName apontando para L"lo"
    struct GuestAdapterAddrs {
        std::uint32_t Length{0};
        std::uint32_t IfIndex{1};
        void* Next{nullptr};
        char AdapterName[8]{};
        void* FirstUnicastAddress{nullptr};
        void* FirstAnycastAddress{nullptr};
        void* FirstMulticastAddress{nullptr};
        void* FirstDnsServerAddress{nullptr};
        wchar_t DnsSuffix[1]{0};
        wchar_t Description[9]{};
        wchar_t FriendlyName[3]{};
        std::uint8_t PhysicalAddress[8]{};
        std::uint32_t PhysicalAddressLength{6};
        std::uint32_t Flags{0};
        std::uint32_t Mtu{1500};
        std::uint32_t IfType{24}; // IF_TYPE_ETHERNET
        std::uint32_t OperStatus{1}; // IfOperStatusUp
        std::uint32_t Ipv6IfIndex{0};
        std::uint32_t ZoneIndices[16]{};
        void* FirstPrefix{nullptr};
    };
    auto* addrs = static_cast<GuestAdapterAddrs*>(AdapterAddresses);
    addrs->Length = kRequired;
    addrs->IfIndex = 1;
    addrs->Next = nullptr;
    // FriendlyName = L"lo" logo após a estrutura
    wchar_t* friendly = reinterpret_cast<wchar_t*>(static_cast<char*>(AdapterAddresses) + sizeof(GuestAdapterAddrs));
    if (reinterpret_cast<std::uintptr_t>(friendly) + 6 <= reinterpret_cast<std::uintptr_t>(AdapterAddresses) + *SizePointer) {
        friendly[0] = L'l'; friendly[1] = L'o'; friendly[2] = 0;
        // Aponta FriendlyName para essa string (offset dentro do buffer)
        // O campo FriendlyName na estrutura original é WCHAR*, mas simplificamos para array; copiamos "lo" para o array
        addrs->FriendlyName[0] = L'l'; addrs->FriendlyName[1] = L'o'; addrs->FriendlyName[2] = 0;
        const wchar_t lo_desc[] = L"Loopback";
        for (int i = 0; lo_desc[i] != 0 && i < 8; ++i) addrs->Description[i] = lo_desc[i];
    }
    addrs->PhysicalAddress[0] = 0x02; // locally administered
    *SizePointer = kRequired;
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
