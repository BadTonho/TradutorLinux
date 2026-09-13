#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "../../core/runtime_state_common.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netpacket/packet.h>

namespace tradutorlinux {
namespace {

constexpr std::uint32_t kErrorBufferOverflow = 111U;
constexpr std::uint32_t kErrorNoData = 232U;
constexpr std::size_t kMaxHostAdapters = 64U;
constexpr std::uint32_t kGuestAfInet = 2U;
constexpr std::uint32_t kGuestAfInet6 = 23U;
constexpr std::uint32_t kGaaFlagSkipUnicast = 0x00000001U;
constexpr std::uint32_t kGaaFlagSkipFriendlyName = 0x00000020U;
constexpr std::size_t kAdapterNameCapacity = IFNAMSIZ;

struct HostAdapter {
    std::array<char, kAdapterNameCapacity> name{};
    std::uint32_t index{0};
    bool up{false};
    bool loopback{false};
    bool has_ipv4{false};
    sockaddr_in ipv4{};
    sockaddr_in netmask{};
    std::array<std::uint8_t, 8> physical_address{};
    std::uint32_t physical_address_length{0};
};

struct GuestIpAddressString {
    void* next{nullptr};
    char ip_address[16]{};
    char ip_mask[16]{};
    std::uint32_t context{0};
};

struct GuestAdapterInfo {
    std::uint32_t combo_index{0};
    void* next{nullptr};
    char adapter_name[260]{};
    char description[132]{};
    std::uint32_t address_length{0};
    std::uint8_t address[8]{};
    std::uint32_t index{0};
    std::uint32_t type{0};
    std::uint32_t dhcp_enabled{0};
    void* current_ip_address{nullptr};
    GuestIpAddressString ip_address_list{};
    GuestIpAddressString gateway_list{};
    GuestIpAddressString dhcp_server{};
    std::int32_t have_wins{0};
    GuestIpAddressString primary_wins_server{};
    GuestIpAddressString secondary_wins_server{};
    std::uint32_t lease_obtained{0};
    std::uint32_t lease_expires{0};
};

struct GuestSocketAddress {
    void* address{nullptr};
    std::int32_t length{0};
    std::uint32_t padding{0};
};

struct GuestUnicastAddress {
    std::uint32_t length{0};
    std::uint32_t flags{0};
    void* next{nullptr};
    GuestSocketAddress address{};
    std::uint32_t prefix_origin{0};
    std::uint32_t suffix_origin{0};
    std::uint32_t dad_state{0};
    std::uint32_t valid_lifetime{0};
    std::uint32_t preferred_lifetime{0};
    std::uint32_t lease_lifetime{0};
    std::uint8_t on_link_prefix_length{0};
    std::uint8_t reserved[3]{};
};

struct GuestAdapterAddresses {
    std::uint32_t length{0};
    std::uint32_t if_index{0};
    void* next{nullptr};
    char* adapter_name{nullptr};
    void* first_unicast_address{nullptr};
    void* first_anycast_address{nullptr};
    void* first_multicast_address{nullptr};
    void* first_dns_server_address{nullptr};
    std::uint16_t* dns_suffix{nullptr};
    std::uint16_t* description{nullptr};
    std::uint16_t* friendly_name{nullptr};
    std::uint8_t physical_address[8]{};
    std::uint32_t physical_address_length{0};
    std::uint32_t flags{0};
    std::uint32_t mtu{1500};
    std::uint32_t if_type{0};
    std::uint32_t oper_status{0};
    std::uint32_t ipv6_if_index{0};
    std::uint32_t zone_indices[16]{};
    void* first_prefix{nullptr};
};

static_assert(sizeof(GuestIpAddressString) == 48U);
static_assert(sizeof(GuestAdapterInfo) == 696U);
static_assert(sizeof(GuestSocketAddress) == 16U);
static_assert(sizeof(GuestUnicastAddress) == 64U);
static_assert(sizeof(GuestAdapterAddresses) == 184U);

void trace_iphlpapi(const char* const operation, const char* const status,
                    const std::uint32_t result, const std::uint32_t size) noexcept {
    runtime_trace("iphlpapi", {
        diagnostics::TraceField{"operation", operation},
        diagnostics::TraceField{"status", status},
        diagnostics::TraceField{"result", std::to_string(result)},
        diagnostics::TraceField{"size", std::to_string(size)},
    }, 4);
}

HostAdapter* find_adapter(std::array<HostAdapter, kMaxHostAdapters>& adapters,
                          const std::size_t count, const char* const name) noexcept {
    for (std::size_t index = 0; index < count; ++index) {
        if (std::strncmp(adapters[index].name.data(), name, adapters[index].name.size()) == 0) {
            return &adapters[index];
        }
    }
    return nullptr;
}

HostAdapter* add_adapter(std::array<HostAdapter, kMaxHostAdapters>& adapters,
                         std::size_t& count, const char* const name,
                         const unsigned int flags) noexcept {
    if (name == nullptr || name[0] == '\0') return nullptr;
    const std::size_t name_length = strnlen(name, kAdapterNameCapacity);
    if (name_length == 0U || name_length >= kAdapterNameCapacity) return nullptr;

    if (HostAdapter* const existing = find_adapter(adapters, count, name); existing != nullptr) {
        existing->up = existing->up || (flags & IFF_UP) != 0U;
        return existing;
    }
    if (count >= adapters.size()) return nullptr;
    HostAdapter& result = adapters[count++];
    std::memcpy(result.name.data(), name, name_length);
    result.name[name_length] = '\0';
    result.index = ::if_nametoindex(name);
    if (result.index == 0U) {
        --count;
        return nullptr;
    }
    result.up = (flags & IFF_UP) != 0U;
    result.loopback = (flags & IFF_LOOPBACK) != 0U;
    return &result;
}

bool collect_host_adapters(std::array<HostAdapter, kMaxHostAdapters>& adapters,
                           std::size_t& count) noexcept {
    count = 0;
    ifaddrs* addresses = nullptr;
    if (::getifaddrs(&addresses) != 0) {
        return false;
    }

    for (ifaddrs* current = addresses; current != nullptr; current = current->ifa_next) {
        HostAdapter* const adapter = add_adapter(adapters, count, current->ifa_name,
                                                  current->ifa_flags);
        if (adapter == nullptr || current->ifa_addr == nullptr) continue;

        const int family = current->ifa_addr->sa_family;
        if (family == AF_INET && !adapter->has_ipv4) {
            std::memcpy(&adapter->ipv4, current->ifa_addr, sizeof(adapter->ipv4));
            if (current->ifa_netmask != nullptr) {
                std::memcpy(&adapter->netmask, current->ifa_netmask, sizeof(adapter->netmask));
            }
            adapter->has_ipv4 = true;
        } else if (family == AF_PACKET && adapter->physical_address_length == 0U) {
            const auto* const link = reinterpret_cast<const sockaddr_ll*>(current->ifa_addr);
            const std::size_t copy_size = std::min<std::size_t>(link->sll_halen,
                                                                 adapter->physical_address.size());
            if (copy_size > 0U) {
                std::memcpy(adapter->physical_address.data(), link->sll_addr, copy_size);
                adapter->physical_address_length = static_cast<std::uint32_t>(copy_size);
            }
        }
    }
    ::freeifaddrs(addresses);
    return count != 0U;
}

void copy_ipv4_text(const sockaddr_in& address, char (&output)[16]) noexcept {
    if (::inet_ntop(AF_INET, &address.sin_addr, output, sizeof(output)) == nullptr) {
        output[0] = '\0';
    }
}

std::uint8_t prefix_length(const sockaddr_in& netmask) noexcept {
    std::uint32_t mask = ntohl(netmask.sin_addr.s_addr);
    std::uint8_t result = 0;
    while ((mask & 0x80000000U) != 0U) {
        ++result;
        mask <<= 1U;
    }
    return result;
}

bool checked_add(const std::size_t left, const std::size_t right,
                 std::size_t& result) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) return false;
    result = left + right;
    return true;
}

bool checked_align8(const std::size_t value, std::size_t& result) noexcept {
    constexpr std::size_t mask = 7U;
    if (mask > std::numeric_limits<std::size_t>::max() - value) return false;
    result = (value + mask) & ~mask;
    return true;
}

std::size_t adapter_addresses_record_size(const HostAdapter& adapter,
                                          const std::uint32_t flags) noexcept {
    std::size_t result = sizeof(GuestAdapterAddresses);
    const std::size_t name_length = std::strlen(adapter.name.data());
    if (!checked_align8(result, result) ||
        !checked_add(result, name_length + 1U, result) ||
        !checked_add(result, (name_length + 1U) * sizeof(std::uint16_t), result)) {
        return 0U;
    }
    if ((flags & kGaaFlagSkipUnicast) == 0U && adapter.has_ipv4) {
        if (!checked_align8(result, result) ||
            !checked_add(result, sizeof(GuestUnicastAddress), result) ||
            !checked_add(result, sizeof(sockaddr_in), result)) {
            return 0U;
        }
    }
    return checked_align8(result, result) ? result : 0U;
}

void copy_wide_name(const char* const source, std::uint16_t* const destination) noexcept {
    const std::size_t length = std::strlen(source);
    for (std::size_t index = 0; index <= length; ++index) {
        destination[index] = static_cast<std::uint8_t>(source[index]);
    }
}

std::uint32_t required_adapter_info_size(const std::size_t count) noexcept {
    if (count > std::numeric_limits<std::uint32_t>::max() / sizeof(GuestAdapterInfo)) {
        return 0U;
    }
    return static_cast<std::uint32_t>(count * sizeof(GuestAdapterInfo));
}

std::uint32_t required_adapter_addresses_size(
    const std::array<HostAdapter, kMaxHostAdapters>& adapters, const std::size_t count,
    const std::uint32_t flags) noexcept {
    std::size_t total = 0U;
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t record_size = adapter_addresses_record_size(adapters[index], flags);
        if (record_size == 0U || !checked_add(total, record_size, total) ||
            total > std::numeric_limits<std::uint32_t>::max()) {
            return 0U;
        }
    }
    return static_cast<std::uint32_t>(total);
}

}  // namespace

extern "C" {

TL_MSABI std::uint32_t tl_GetAdaptersInfo(void* const AdapterInfo,
                                          std::uint32_t* const OutBufLen) noexcept {
    if (OutBufLen == nullptr || !mapped_guest_range(OutBufLen, sizeof(*OutBufLen), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return abi::kErrorInvalidParameter;
    }

    std::array<HostAdapter, kMaxHostAdapters> adapters{};
    std::size_t adapter_count = 0U;
    if (!collect_host_adapters(adapters, adapter_count)) {
        *OutBufLen = 0U;
        set_last_error(kErrorNoData);
        trace_iphlpapi("GetAdaptersInfo", "no-data", kErrorNoData, 0U);
        return kErrorNoData;
    }
    const std::uint32_t required = required_adapter_info_size(adapter_count);
    if (required == 0U) {
        *OutBufLen = 0U;
        set_last_error(abi::kErrorNotEnoughMemory);
        trace_iphlpapi("GetAdaptersInfo", "size-overflow", abi::kErrorNotEnoughMemory, 0U);
        return abi::kErrorNotEnoughMemory;
    }
    if (AdapterInfo == nullptr || *OutBufLen < required) {
        *OutBufLen = required;
        set_last_error(kErrorBufferOverflow);
        trace_iphlpapi("GetAdaptersInfo", "buffer-overflow", kErrorBufferOverflow, required);
        return kErrorBufferOverflow;
    }
    if (!mapped_guest_range(AdapterInfo, required, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return abi::kErrorInvalidParameter;
    }

    std::memset(AdapterInfo, 0, required);
    auto* const first = static_cast<std::byte*>(AdapterInfo);
    for (std::size_t index = 0; index < adapter_count; ++index) {
        auto* const info = reinterpret_cast<GuestAdapterInfo*>(
            first + index * sizeof(GuestAdapterInfo));
        const HostAdapter& adapter = adapters[index];
        info->combo_index = static_cast<std::uint32_t>(index + 1U);
        info->next = index + 1U < adapter_count
                         ? first + (index + 1U) * sizeof(GuestAdapterInfo)
                         : nullptr;
        std::memcpy(info->adapter_name, adapter.name.data(),
                    std::min(adapter.name.size(), sizeof(info->adapter_name) - 1U));
        std::memcpy(info->description, adapter.name.data(),
                    std::min(adapter.name.size(), sizeof(info->description) - 1U));
        info->address_length = std::min<std::uint32_t>(adapter.physical_address_length,
                                                       sizeof(info->address));
        std::memcpy(info->address, adapter.physical_address.data(), info->address_length);
        info->index = adapter.index;
        info->type = adapter.loopback ? 24U : 6U;
        if (adapter.has_ipv4) {
            copy_ipv4_text(adapter.ipv4, info->ip_address_list.ip_address);
            copy_ipv4_text(adapter.netmask, info->ip_address_list.ip_mask);
        }
    }
    *OutBufLen = required;
    set_last_error(abi::kErrorSuccess);
    trace_iphlpapi("GetAdaptersInfo", "success", abi::kErrorSuccess, required);
    return abi::kErrorSuccess;
}

TL_MSABI std::uint32_t tl_GetAdaptersAddresses(
    const std::uint32_t Family, const std::uint32_t Flags, void* const Reserved,
    void* const AdapterAddresses, std::uint32_t* const SizePointer) noexcept {
    if (Reserved != nullptr || (Family != 0U && Family != kGuestAfInet && Family != kGuestAfInet6) ||
        SizePointer == nullptr || !mapped_guest_range(SizePointer, sizeof(*SizePointer), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return abi::kErrorInvalidParameter;
    }

    std::array<HostAdapter, kMaxHostAdapters> adapters{};
    std::size_t adapter_count = 0U;
    if (!collect_host_adapters(adapters, adapter_count)) {
        *SizePointer = 0U;
        set_last_error(kErrorNoData);
        trace_iphlpapi("GetAdaptersAddresses", "no-data", kErrorNoData, 0U);
        return kErrorNoData;
    }
    if (Family == kGuestAfInet6) {
        *SizePointer = 0U;
        set_last_error(kErrorNoData);
        trace_iphlpapi("GetAdaptersAddresses", "ipv6-not-supported", kErrorNoData, 0U);
        return kErrorNoData;
    }
    // The current contract exposes only AF_INET records. Do not return
    // adapter headers without an address when the caller requested IPv4.
    std::size_t ipv4_count = 0U;
    for (std::size_t index = 0; index < adapter_count; ++index) {
        if (adapters[index].has_ipv4) {
            if (ipv4_count != index) adapters[ipv4_count] = adapters[index];
            ++ipv4_count;
        }
    }
    adapter_count = ipv4_count;
    if (adapter_count == 0U) {
        *SizePointer = 0U;
        set_last_error(kErrorNoData);
        trace_iphlpapi("GetAdaptersAddresses", "no-ipv4-data", kErrorNoData, 0U);
        return kErrorNoData;
    }
    const std::uint32_t required = required_adapter_addresses_size(adapters, adapter_count, Flags);
    if (required == 0U) {
        *SizePointer = 0U;
        set_last_error(abi::kErrorNotEnoughMemory);
        trace_iphlpapi("GetAdaptersAddresses", "size-overflow", abi::kErrorNotEnoughMemory, 0U);
        return abi::kErrorNotEnoughMemory;
    }
    if (AdapterAddresses == nullptr || *SizePointer < required) {
        *SizePointer = required;
        set_last_error(kErrorBufferOverflow);
        trace_iphlpapi("GetAdaptersAddresses", "buffer-overflow", kErrorBufferOverflow, required);
        return kErrorBufferOverflow;
    }
    if (!mapped_guest_range(AdapterAddresses, required, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return abi::kErrorInvalidParameter;
    }

    std::memset(AdapterAddresses, 0, required);
    auto* const first = static_cast<std::byte*>(AdapterAddresses);
    GuestAdapterAddresses* previous = nullptr;
    std::size_t offset = 0U;
    for (std::size_t index = 0; index < adapter_count; ++index) {
        const HostAdapter& adapter = adapters[index];
        auto* const current = reinterpret_cast<GuestAdapterAddresses*>(first + offset);
        current->length = sizeof(GuestAdapterAddresses);
        current->if_index = adapter.index;
        current->if_type = adapter.loopback ? 24U : 6U;
        current->oper_status = adapter.up ? 1U : 2U;
        current->physical_address_length = std::min<std::uint32_t>(
            adapter.physical_address_length, sizeof(current->physical_address));
        std::memcpy(current->physical_address, adapter.physical_address.data(),
                    current->physical_address_length);
        if (previous != nullptr) previous->next = current;
        previous = current;

        std::size_t cursor = offset + sizeof(GuestAdapterAddresses);
        if (!checked_align8(cursor, cursor)) return abi::kErrorNotEnoughMemory;
        current->adapter_name = reinterpret_cast<char*>(first + cursor);
        const std::size_t name_length = std::strlen(adapter.name.data());
        std::memcpy(current->adapter_name, adapter.name.data(), name_length + 1U);
        cursor += name_length + 1U;

        current->description = reinterpret_cast<std::uint16_t*>(first + cursor);
        copy_wide_name(adapter.name.data(), current->description);
        cursor += (name_length + 1U) * sizeof(std::uint16_t);
        if ((Flags & kGaaFlagSkipFriendlyName) == 0U) {
            current->friendly_name = current->description;
        }

        if ((Flags & kGaaFlagSkipUnicast) == 0U && adapter.has_ipv4) {
            cursor = (cursor + 7U) & ~static_cast<std::size_t>(7U);
            auto* const unicast = reinterpret_cast<GuestUnicastAddress*>(first + cursor);
            cursor += sizeof(GuestUnicastAddress);
            auto* const address = reinterpret_cast<sockaddr_in*>(first + cursor);
            cursor += sizeof(sockaddr_in);
            unicast->length = sizeof(GuestUnicastAddress);
            unicast->address.address = address;
            unicast->address.length = sizeof(sockaddr_in);
            unicast->on_link_prefix_length = prefix_length(adapter.netmask);
            std::memcpy(address, &adapter.ipv4, sizeof(*address));
            current->first_unicast_address = unicast;
        }
        offset += adapter_addresses_record_size(adapter, Flags);
    }
    *SizePointer = static_cast<std::uint32_t>(offset);
    set_last_error(abi::kErrorSuccess);
    trace_iphlpapi("GetAdaptersAddresses", "success", abi::kErrorSuccess, *SizePointer);
    return abi::kErrorSuccess;
}

TL_MSABI std::uint32_t tl_if_nametoindex(const char* const ifname) noexcept {
    if (ifname == nullptr || !mapped_guest_cstring(ifname) || ifname[0] == '\0') {
        set_last_error(abi::kErrorInvalidParameter);
        return 0U;
    }
    const unsigned int index = ::if_nametoindex(ifname);
    if (index == 0U) {
        set_last_error(abi::kErrorFileNotFound);
        return 0U;
    }
    set_last_error(abi::kErrorSuccess);
    return index;
}

}  // extern "C"
}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_iphlpapi_module() {
    static const ExportedFunction kIphlpapiExports[] = {
        {"GetAdaptersInfo", 1, reinterpret_cast<std::uintptr_t>(&tl_GetAdaptersInfo), ExportSupport::Full},
        {"GetAdaptersAddresses", 2, reinterpret_cast<std::uintptr_t>(&tl_GetAdaptersAddresses), ExportSupport::Full},
        {"if_nametoindex", 3, reinterpret_cast<std::uintptr_t>(&tl_if_nametoindex), ExportSupport::Full},
    };
    static const InternalModule kIphlpapiModule{"IPHLPAPI.DLL", kIphlpapiExports};
    register_module(kIphlpapiModule);
}

}  // namespace tradutorlinux::loader
