#include "tradutorlinux/runtime/ws2_32.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace tradutorlinux {
namespace {

constexpr int kAfInet = 2;
constexpr int kSockStream = 1;
constexpr int kSockDgram = 2;
constexpr int kIpProtoTcp = 6;
constexpr int kIpProtoUdp = 17;
constexpr int kWsaENotSocket = 10038;
constexpr int kWsaEAccess = 10013;
constexpr int kWsaEInvalidArgument = 10022;
constexpr int kWsaEAddressInUse = 10048;
constexpr int kWsaEWouldBlock = 10035;
constexpr int kWsaEConnectionRefused = 10061;
constexpr int kWsaENetUnreachable = 10051;
constexpr int kWsaENoData = 11004;
constexpr int kWsaEAIFlags = 10022;
constexpr std::uintptr_t kSocketHandleBase = 0x0000B00000000000ULL;

thread_local int g_wsa_last_error = 0;

struct SocketSlot {
    bool used{false};
    int fd{-1};
    int type{0};
};
std::array<SocketSlot, 64> g_sockets{};

struct GuestAddrInfo {
    int flags{};
    int family{};
    int socktype{};
    int protocol{};
    std::size_t address_length{};
    char* canonname{};
    void* address{};
    GuestAddrInfo* next{};
    sockaddr_in socket_address{};
    std::string canon_name;
};
std::array<GuestAddrInfo, 16> g_addrinfos{};

struct GuestPollFd {
    std::uintptr_t socket{};
    std::int16_t events{};
    std::int16_t revents{};
};
static_assert(sizeof(GuestPollFd) == 16);

bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    if (address == nullptr) {
        return false;
    }
    const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(address);
    if (size > std::numeric_limits<std::uintptr_t>::max() - target) {
        return false;
    }
    std::ifstream maps{"/proc/self/maps"};
    std::string line;
    while (std::getline(maps, line)) {
        const std::size_t dash = line.find('-');
        const std::size_t space = line.find(' ', dash == std::string::npos ? 0 : dash);
        if (dash == std::string::npos || space == std::string::npos) {
            continue;
        }
        std::uintptr_t begin = 0;
        std::uintptr_t end = 0;
        if (std::from_chars(line.data(), line.data() + dash, begin, 16).ec != std::errc{} ||
            std::from_chars(line.data() + dash + 1, line.data() + space, end, 16).ec != std::errc{} ||
            target < begin || target > end || size > end - target) {
            continue;
        }
        const std::size_t permissions = space + 1;
        return line.size() >= permissions + 2 && line[permissions] == 'r' &&
               (!writable || line[permissions + 1] == 'w');
    }
    return false;
}

bool mapped_cstring(const char* value) noexcept {
    if (value == nullptr) {
        return true;
    }
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(value);
    for (std::size_t index = 0; index < 65535U; ++index) {
        if (index > std::numeric_limits<std::uintptr_t>::max() - address ||
            !mapped_range(reinterpret_cast<const void*>(address + index), 1, false)) {
            return false;
        }
        if (value[index] == '\0') {
            return true;
        }
    }
    return false;
}

SocketSlot* find_socket(const std::uintptr_t handle) noexcept {
    if (handle < kSocketHandleBase || handle >= kSocketHandleBase + g_sockets.size()) {
        return nullptr;
    }
    SocketSlot& slot = g_sockets[handle - kSocketHandleBase];
    return slot.used ? &slot : nullptr;
}

std::uintptr_t socket_handle(SocketSlot& slot) noexcept {
    return kSocketHandleBase + static_cast<std::uintptr_t>(&slot - g_sockets.data());
}

int errno_to_wsa(const int error) noexcept {
    switch (error) {
        case EACCES:
        case EPERM:
            return kWsaEAccess;
        case EADDRINUSE:
            return kWsaEAddressInUse;
        case EAGAIN:
            return kWsaEWouldBlock;
        case ECONNREFUSED:
            return kWsaEConnectionRefused;
        case ENETUNREACH:
            return kWsaENetUnreachable;
        case EINVAL:
            return kWsaEInvalidArgument;
        default:
            return kWsaENotSocket;
    }
}

bool copy_guest_sockaddr(const void* address, const int length, sockaddr_in& result) noexcept {
    if (address == nullptr || length < static_cast<int>(sizeof(sockaddr_in)) ||
        !mapped_range(address, sizeof(sockaddr_in), false)) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return false;
    }
    std::memcpy(&result, address, sizeof(result));
    if (result.sin_family != AF_INET) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return false;
    }
    return true;
}

void copy_host_sockaddr(const sockaddr_in& source, void* address, int* length) noexcept {
    if (address != nullptr && length != nullptr && *length >= static_cast<int>(sizeof(source)) &&
        mapped_range(address, sizeof(source), true) && mapped_range(length, sizeof(*length), true)) {
        std::memcpy(address, &source, sizeof(source));
        *length = sizeof(source);
    }
}

}  // namespace

extern "C" {

TL_MSABI int tl_WSAStartup(const std::uint16_t version_requested, void* data) noexcept {
    if (data == nullptr || !mapped_range(data, 400, true) || version_requested < 0x0101U) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return kWsaEInvalidArgument;
    }
    std::memset(data, 0, 400);
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI int tl_WSACleanup() noexcept {
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI int tl_WSAGetLastError() noexcept {
    return g_wsa_last_error;
}

TL_MSABI std::uintptr_t tl_socket(const int address_family, const int type,
                                  const int protocol) noexcept {
    if (address_family != kAfInet || (type != kSockStream && type != kSockDgram) ||
        (protocol != 0 && protocol != kIpProtoTcp && protocol != kIpProtoUdp)) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return kInvalidSocket;
    }
    const int fd = ::socket(AF_INET, type == kSockStream ? SOCK_STREAM : SOCK_DGRAM, protocol);
    if (fd < 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return kInvalidSocket;
    }
    auto free_it = std::find_if(g_sockets.begin(), g_sockets.end(),
                                [](const SocketSlot& slot) { return !slot.used; });
    if (free_it == g_sockets.end()) {
        ::close(fd);
        g_wsa_last_error = ENOBUFS;
        return kInvalidSocket;
    }
    free_it->used = true;
    free_it->fd = fd;
    free_it->type = type;
    g_wsa_last_error = 0;
    return socket_handle(*free_it);
}

TL_MSABI int tl_closesocket(const std::uintptr_t socket) noexcept {
    SocketSlot* slot = find_socket(socket);
    if (slot == nullptr) {
        g_wsa_last_error = kWsaENotSocket;
        return -1;
    }
    const int result = ::close(slot->fd);
    *slot = {};
    if (result != 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI int tl_bind(const std::uintptr_t socket, const void* name, const int name_length) noexcept {
    SocketSlot* slot = find_socket(socket);
    sockaddr_in address{};
    if (slot == nullptr) {
        g_wsa_last_error = kWsaENotSocket;
        return -1;
    }
    if (!copy_guest_sockaddr(name, name_length, address)) {
        return -1;
    }
    if (::bind(slot->fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI int tl_listen(const std::uintptr_t socket, const int backlog) noexcept {
    SocketSlot* slot = find_socket(socket);
    if (slot == nullptr || slot->type != kSockStream || backlog < 0 || ::listen(slot->fd, backlog) != 0) {
        g_wsa_last_error = slot == nullptr ? kWsaENotSocket : errno_to_wsa(errno);
        return -1;
    }
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI std::uintptr_t tl_accept(const std::uintptr_t socket, void* name,
                                  int* name_length) noexcept {
    SocketSlot* listener = find_socket(socket);
    if (listener == nullptr || listener->type != kSockStream) {
        g_wsa_last_error = kWsaENotSocket;
        return kInvalidSocket;
    }
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    const int fd = ::accept(listener->fd, reinterpret_cast<sockaddr*>(&address), &length);
    if (fd < 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return kInvalidSocket;
    }
    auto free_it = std::find_if(g_sockets.begin(), g_sockets.end(),
                                [](const SocketSlot& slot) { return !slot.used; });
    if (free_it == g_sockets.end()) {
        ::close(fd);
        g_wsa_last_error = ENOBUFS;
        return kInvalidSocket;
    }
    copy_host_sockaddr(address, name, name_length);
    free_it->used = true;
    free_it->fd = fd;
    free_it->type = kSockStream;
    g_wsa_last_error = 0;
    return socket_handle(*free_it);
}

TL_MSABI int tl_connect(const std::uintptr_t socket, const void* name,
                        const int name_length) noexcept {
    SocketSlot* slot = find_socket(socket);
    sockaddr_in address{};
    if (slot == nullptr) {
        g_wsa_last_error = kWsaENotSocket;
        return -1;
    }
    if (!copy_guest_sockaddr(name, name_length, address) ||
        ::connect(slot->fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        if (g_wsa_last_error == 0) {
            g_wsa_last_error = errno_to_wsa(errno);
        }
        return -1;
    }
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI int tl_send(const std::uintptr_t socket, const char* buffer, const int length,
                     const int flags) noexcept {
    SocketSlot* slot = find_socket(socket);
    if (slot == nullptr || buffer == nullptr || length < 0 || flags != 0 ||
        !mapped_range(buffer, static_cast<std::size_t>(length), false)) {
        g_wsa_last_error = slot == nullptr ? kWsaENotSocket : kWsaEInvalidArgument;
        return -1;
    }
    const ssize_t result = ::send(slot->fd, buffer, static_cast<std::size_t>(length), 0);
    if (result < 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    g_wsa_last_error = 0;
    return static_cast<int>(result);
}

TL_MSABI int tl_recv(const std::uintptr_t socket, char* buffer, const int length,
                     const int flags) noexcept {
    SocketSlot* slot = find_socket(socket);
    if (slot == nullptr || buffer == nullptr || length < 0 || flags != 0 ||
        !mapped_range(buffer, static_cast<std::size_t>(length), true)) {
        g_wsa_last_error = slot == nullptr ? kWsaENotSocket : kWsaEInvalidArgument;
        return -1;
    }
    const ssize_t result = ::recv(slot->fd, buffer, static_cast<std::size_t>(length), 0);
    if (result < 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    g_wsa_last_error = 0;
    return static_cast<int>(result);
}

TL_MSABI int tl_sendto(const std::uintptr_t socket, const char* buffer, const int length,
                       const int flags, const void* to, const int to_length) noexcept {
    SocketSlot* slot = find_socket(socket);
    sockaddr_in address{};
    if (slot == nullptr || buffer == nullptr || length < 0 || flags != 0 ||
        !mapped_range(buffer, static_cast<std::size_t>(length), false) ||
        !copy_guest_sockaddr(to, to_length, address)) {
        g_wsa_last_error = slot == nullptr ? kWsaENotSocket : kWsaEInvalidArgument;
        return -1;
    }
    const ssize_t result = ::sendto(slot->fd, buffer, static_cast<std::size_t>(length), 0,
                                    reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (result < 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    g_wsa_last_error = 0;
    return static_cast<int>(result);
}

TL_MSABI int tl_recvfrom(const std::uintptr_t socket, char* buffer, const int length,
                         const int flags, void* from, int* from_length) noexcept {
    SocketSlot* slot = find_socket(socket);
    if (slot == nullptr || buffer == nullptr || length < 0 || flags != 0 ||
        !mapped_range(buffer, static_cast<std::size_t>(length), true)) {
        g_wsa_last_error = slot == nullptr ? kWsaENotSocket : kWsaEInvalidArgument;
        return -1;
    }
    sockaddr_in address{};
    socklen_t host_length = sizeof(address);
    const ssize_t result = ::recvfrom(slot->fd, buffer, static_cast<std::size_t>(length), 0,
                                      reinterpret_cast<sockaddr*>(&address), &host_length);
    if (result < 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    copy_host_sockaddr(address, from, from_length);
    g_wsa_last_error = 0;
    return static_cast<int>(result);
}

TL_MSABI int tl_getsockname(const std::uintptr_t socket, void* name, int* name_length) noexcept {
    SocketSlot* slot = find_socket(socket);
    if (slot == nullptr || name == nullptr || name_length == nullptr ||
        !mapped_range(name_length, sizeof(*name_length), true) ||
        *name_length < static_cast<int>(sizeof(sockaddr_in)) ||
        !mapped_range(name, sizeof(sockaddr_in), true)) {
        g_wsa_last_error = slot == nullptr ? kWsaENotSocket : kWsaEInvalidArgument;
        return -1;
    }
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    if (::getsockname(slot->fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    std::memcpy(name, &address, sizeof(address));
    *name_length = sizeof(address);
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI int tl_shutdown(const std::uintptr_t socket, const int how) noexcept {
    SocketSlot* slot = find_socket(socket);
    if (slot == nullptr || (how != 0 && how != 1 && how != 2) || ::shutdown(slot->fd, how) != 0) {
        g_wsa_last_error = slot == nullptr ? kWsaENotSocket : errno_to_wsa(errno);
        return -1;
    }
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI int tl_getaddrinfo(const char* node, const char* service, const void* hints,
                            void* result) noexcept {
    if (result == nullptr || !mapped_range(result, sizeof(void*), true) ||
        !mapped_cstring(node) || !mapped_cstring(service)) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return kWsaEInvalidArgument;
    }
    int family = AF_INET;
    int type = SOCK_STREAM;
    int protocol = IPPROTO_TCP;
    if (hints != nullptr && mapped_range(hints, 16, false)) {
        std::memcpy(&family, hints, sizeof(int));
        std::memcpy(&type, static_cast<const char*>(hints) + 8, sizeof(int));
        std::memcpy(&protocol, static_cast<const char*>(hints) + 12, sizeof(int));
        if (family != 0 && family != AF_INET) {
            g_wsa_last_error = kWsaEAIFlags;
            return kWsaEAIFlags;
        }
    }
    if (node != nullptr && std::strcmp(node, "localhost") != 0 &&
        std::strcmp(node, "127.0.0.1") != 0) {
        g_wsa_last_error = kWsaENoData;
        return kWsaENoData;
    }
    unsigned long port = 0;
    if (service != nullptr) {
        const auto parsed = std::from_chars(service, service + std::strlen(service), port, 10);
        if (parsed.ec != std::errc{} || port > 65535U) {
            g_wsa_last_error = kWsaEInvalidArgument;
            return kWsaEInvalidArgument;
        }
    }
    auto free_it = std::find_if(g_addrinfos.begin(), g_addrinfos.end(),
                                [](const GuestAddrInfo& info) { return info.address == nullptr; });
    if (free_it == g_addrinfos.end()) {
        g_wsa_last_error = ENOBUFS;
        return ENOBUFS;
    }
    GuestAddrInfo& info = *free_it;
    info = {};
    info.family = family == 0 ? AF_INET : family;
    info.socktype = type == 0 ? SOCK_STREAM : type;
    info.protocol = protocol == 0 ? (info.socktype == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP) : protocol;
    info.address_length = sizeof(sockaddr_in);
    info.socket_address.sin_family = AF_INET;
    info.socket_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    info.socket_address.sin_port = htons(static_cast<std::uint16_t>(port));
    info.address = &info.socket_address;
    info.canon_name = node != nullptr ? node : "localhost";
    info.canonname = info.canon_name.data();
    info.next = nullptr;
    *static_cast<void**>(result) = &info;
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI void tl_freeaddrinfo(void* address_info) noexcept {
    if (address_info == nullptr) {
        return;
    }
    for (GuestAddrInfo& info : g_addrinfos) {
        if (&info == address_info) {
            info = {};
            g_wsa_last_error = 0;
            return;
        }
    }
    g_wsa_last_error = kWsaEInvalidArgument;
}

TL_MSABI std::uint16_t tl_htons(const std::uint16_t host_short) noexcept {
    return htons(host_short);
}

TL_MSABI std::uint16_t tl_ntohs(const std::uint16_t network_short) noexcept {
    return ntohs(network_short);
}

TL_MSABI std::uint32_t tl_htonl(const std::uint32_t host_long) noexcept {
    return htonl(host_long);
}

TL_MSABI std::uint32_t tl_ntohl(const std::uint32_t network_long) noexcept {
    return ntohl(network_long);
}

TL_MSABI std::uint32_t tl_inet_addr(const char* address) noexcept {
    if (!mapped_cstring(address)) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return 0xFFFFFFFFU;
    }
    in_addr parsed{};
    if (::inet_aton(address, &parsed) == 0) {
        g_wsa_last_error = kWsaENoData;
        return 0xFFFFFFFFU;
    }
    g_wsa_last_error = 0;
    return parsed.s_addr;
}

TL_MSABI int tl_WSAPoll(void* descriptors, const std::uint32_t count,
                        const int timeout) noexcept {
    if (count > 64 || (count != 0 &&
                       !mapped_range(descriptors, static_cast<std::size_t>(count) * sizeof(GuestPollFd),
                                     true))) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return -1;
    }
    std::array<pollfd, 64> host_descriptors{};
    auto* guest_descriptors = static_cast<GuestPollFd*>(descriptors);
    for (std::uint32_t index = 0; index < count; ++index) {
        SocketSlot* slot = find_socket(guest_descriptors[index].socket);
        if (slot == nullptr) {
            g_wsa_last_error = kWsaENotSocket;
            return -1;
        }
        host_descriptors[index].fd = slot->fd;
        host_descriptors[index].events = static_cast<short>(guest_descriptors[index].events);
        host_descriptors[index].revents = 0;
    }
    const int result = ::poll(host_descriptors.data(), count, timeout);
    if (result < 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        guest_descriptors[index].revents = host_descriptors[index].revents;
    }
    g_wsa_last_error = 0;
    return result;
}

}  // extern "C"
}  // namespace tradutorlinux
