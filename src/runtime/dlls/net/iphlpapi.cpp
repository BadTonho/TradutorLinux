#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "runtime_context.hpp"

#include <cstdint>
#include <cstring>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace tradutorlinux {

extern "C" {

TL_MSABI std::uint32_t tl_GetAdaptersInfo(void* AdapterInfo, std::uint32_t* OutBufLen) noexcept {
    if (OutBufLen == nullptr || !mapped_guest_range(OutBufLen, sizeof(std::uint32_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    constexpr std::uint32_t kRequired = 640;
    if (AdapterInfo == nullptr) {
        if (*OutBufLen == 0) {
            return 0;
        }
        *OutBufLen = 640;
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
        if (*SizePointer == 0) {
            return 0;
        }
        *SizePointer = kRequired;
        return 111; // ERROR_BUFFER_OVERFLOW
    }
    if (*SizePointer < sizeof(void*)) {
        *SizePointer = 0;
        return 0;
    }
    if (!mapped_guest_range(AdapterAddresses, *SizePointer, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    std::memset(AdapterAddresses, 0, *SizePointer);
    // Layout completo para 1 adapter com 1 unicast (127.0.0.1) + 1 DNS (8.8.8.8)
    struct GuestSocketAddress {
        void* lpSockaddr{nullptr};
        int iSockaddrLength{0};
    };
    struct GuestUnicast {
        std::uint32_t Length{0};
        std::uint32_t Flags{0};
        void* Next{nullptr};
        GuestSocketAddress Address{};
        std::uint32_t PrefixOrigin{0};
        std::uint32_t SuffixOrigin{0};
        std::uint32_t DadState{0};
        std::uint32_t ValidLifetime{0};
        std::uint32_t PreferredLifetime{0};
        std::uint32_t LeaseLifetime{0};
        std::uint8_t OnLinkPrefixLength{24};
        std::uint8_t Reserved[3]{};
    };
    struct GuestDns {
        void* Next{nullptr};
        GuestSocketAddress Address{};
    };
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
        std::uint32_t IfType{24};
        std::uint32_t OperStatus{1};
        std::uint32_t Ipv6IfIndex{0};
        std::uint32_t ZoneIndices[16]{};
        void* FirstPrefix{nullptr};
    };
    auto* addrs = static_cast<GuestAdapterAddrs*>(AdapterAddresses);
    addrs->Length = kRequired;
    addrs->IfIndex = 1;
    addrs->Next = nullptr;
    addrs->FriendlyName[0] = L'l'; addrs->FriendlyName[1] = L'o'; addrs->FriendlyName[2] = 0;
    const wchar_t lo_desc[] = L"Loopback";
    for (int i = 0; lo_desc[i] != 0 && i < 8; ++i) addrs->Description[i] = lo_desc[i];
    addrs->PhysicalAddress[0] = 0x02;
    // Coloca Unicast + sockaddr_in + Dns + sockaddr_in logo após o adapter
    std::size_t offset = sizeof(GuestAdapterAddrs);
    offset = (offset + 7) & ~static_cast<std::size_t>(7); // align 8
    if (offset + sizeof(GuestUnicast) + sizeof(sockaddr_in) + sizeof(GuestDns) + sizeof(sockaddr_in) <= *SizePointer) {
        auto* unicast = reinterpret_cast<GuestUnicast*>(static_cast<char*>(AdapterAddresses) + offset);
        offset += sizeof(GuestUnicast);
        auto* uni_sock = reinterpret_cast<sockaddr_in*>(static_cast<char*>(AdapterAddresses) + offset);
        offset += sizeof(sockaddr_in);
        auto* dns = reinterpret_cast<GuestDns*>(static_cast<char*>(AdapterAddresses) + offset);
        offset += sizeof(GuestDns);
        auto* dns_sock = reinterpret_cast<sockaddr_in*>(static_cast<char*>(AdapterAddresses) + offset);
        // Unicast 127.0.0.1
        unicast->Length = sizeof(GuestUnicast);
        unicast->Next = nullptr;
        unicast->Address.lpSockaddr = uni_sock;
        unicast->Address.iSockaddrLength = sizeof(sockaddr_in);
        uni_sock->sin_family = AF_INET;
        uni_sock->sin_port = 0;
        uni_sock->sin_addr.s_addr = htonl(0x7F000001U); // 127.0.0.1
        // DNS 8.8.8.8
        dns->Next = nullptr;
        dns->Address.lpSockaddr = dns_sock;
        dns->Address.iSockaddrLength = sizeof(sockaddr_in);
        dns_sock->sin_family = AF_INET;
        dns_sock->sin_port = 0;
        dns_sock->sin_addr.s_addr = htonl(0x08080808U); // 8.8.8.8
        addrs->FirstUnicastAddress = unicast;
        addrs->FirstDnsServerAddress = dns;
    }
    *SizePointer = kRequired;
    set_last_error(abi::kErrorSuccess);
    return 0;
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

namespace tradutorlinux::loader {

void register_iphlpapi_module() {
    static const ExportedFunction kIphlpapiExports[] = {
        {"GetAdaptersInfo", 1, reinterpret_cast<std::uintptr_t>(&tl_GetAdaptersInfo)},
        {"GetAdaptersAddresses", 2, reinterpret_cast<std::uintptr_t>(&tl_GetAdaptersAddresses)},
        {"if_nametoindex", 3, reinterpret_cast<std::uintptr_t>(&tl_if_nametoindex)},
    };
    static const InternalModule kIphlpapiModule{"IPHLPAPI.DLL", kIphlpapiExports};
    register_module(kIphlpapiModule);
}

} // namespace tradutorlinux::loader
