#include "tradutorlinux/runtime/ws2_32.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "tradutorlinux/runtime/memory_validator.hpp"

namespace tradutorlinux {
namespace {

constexpr int kAfInet = 2;
constexpr int kSockStream = 1;
constexpr int kSockDgram = 2;
constexpr int kIpProtoTcp = 6;
constexpr int kIpProtoUdp = 17;
constexpr int kWsaENotSocket = 10038;
constexpr int kWsaEAccess = 10013;
constexpr int kWsaEFault = 10014;
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
std::array<SocketSlot, 256> g_sockets{};

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
std::array<GuestAddrInfo, 128> g_addrinfos{};

struct GuestPollFd {
    std::uintptr_t socket{};
    std::int16_t events{};
    std::int16_t revents{};
};
static_assert(sizeof(GuestPollFd) == 16);

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

inline bool mapped_cstring(const char* value) noexcept {
    if (value == nullptr) {
        return true;
    }
    return runtime::validate_mapped_cstring(value);
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

namespace {

struct Win32FdSet {
    std::uint32_t fd_count;
    std::uintptr_t fd_array[64];
};

struct Win32TimeVal {
    std::int32_t tv_sec;
    std::int32_t tv_usec;
};

}  // namespace

TL_MSABI int tl_select(const int nfds, void* readfds, void* writefds, void* exceptfds, const void* timeout) noexcept {
    (void)nfds;
    fd_set rset, wset, eset;
    FD_ZERO(&rset);
    FD_ZERO(&wset);
    FD_ZERO(&eset);
    int max_fd = -1;

    auto* r_win = static_cast<Win32FdSet*>(readfds);
    auto* w_win = static_cast<Win32FdSet*>(writefds);
    auto* e_win = static_cast<Win32FdSet*>(exceptfds);

    if (r_win != nullptr && mapped_range(r_win, sizeof(Win32FdSet), true)) {
        for (std::uint32_t i = 0; i < std::min<std::uint32_t>(r_win->fd_count, 64); ++i) {
            if (SocketSlot* slot = find_socket(r_win->fd_array[i])) {
                FD_SET(slot->fd, &rset);
                max_fd = std::max(max_fd, slot->fd);
            }
        }
    }
    if (w_win != nullptr && mapped_range(w_win, sizeof(Win32FdSet), true)) {
        for (std::uint32_t i = 0; i < std::min<std::uint32_t>(w_win->fd_count, 64); ++i) {
            if (SocketSlot* slot = find_socket(w_win->fd_array[i])) {
                FD_SET(slot->fd, &wset);
                max_fd = std::max(max_fd, slot->fd);
            }
        }
    }
    if (e_win != nullptr && mapped_range(e_win, sizeof(Win32FdSet), true)) {
        for (std::uint32_t i = 0; i < std::min<std::uint32_t>(e_win->fd_count, 64); ++i) {
            if (SocketSlot* slot = find_socket(e_win->fd_array[i])) {
                FD_SET(slot->fd, &eset);
                max_fd = std::max(max_fd, slot->fd);
            }
        }
    }

    struct timeval tv{};
    struct timeval* ptv = nullptr;
    if (timeout != nullptr && mapped_range(timeout, sizeof(Win32TimeVal), false)) {
        const auto* wt = static_cast<const Win32TimeVal*>(timeout);
        tv.tv_sec = wt->tv_sec;
        tv.tv_usec = wt->tv_usec;
        ptv = &tv;
    }

    const int result = ::select(max_fd + 1, (r_win ? &rset : nullptr), (w_win ? &wset : nullptr), (e_win ? &eset : nullptr), ptv);
    if (result < 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }

    if (r_win != nullptr && mapped_range(r_win, sizeof(Win32FdSet), true)) {
        std::vector<std::uintptr_t> active;
        for (std::uint32_t i = 0; i < std::min<std::uint32_t>(r_win->fd_count, 64); ++i) {
            if (SocketSlot* slot = find_socket(r_win->fd_array[i])) {
                if (FD_ISSET(slot->fd, &rset)) {
                    active.push_back(r_win->fd_array[i]);
                }
            }
        }
        r_win->fd_count = static_cast<std::uint32_t>(active.size());
        for (std::size_t i = 0; i < active.size(); ++i) {
            r_win->fd_array[i] = active[i];
        }
    }
    if (w_win != nullptr && mapped_range(w_win, sizeof(Win32FdSet), true)) {
        std::vector<std::uintptr_t> active;
        for (std::uint32_t i = 0; i < std::min<std::uint32_t>(w_win->fd_count, 64); ++i) {
            if (SocketSlot* slot = find_socket(w_win->fd_array[i])) {
                if (FD_ISSET(slot->fd, &wset)) {
                    active.push_back(w_win->fd_array[i]);
                }
            }
        }
        w_win->fd_count = static_cast<std::uint32_t>(active.size());
        for (std::size_t i = 0; i < active.size(); ++i) {
            w_win->fd_array[i] = active[i];
        }
    }
    if (e_win != nullptr && mapped_range(e_win, sizeof(Win32FdSet), true)) {
        std::vector<std::uintptr_t> active;
        for (std::uint32_t i = 0; i < std::min<std::uint32_t>(e_win->fd_count, 64); ++i) {
            if (SocketSlot* slot = find_socket(e_win->fd_array[i])) {
                if (FD_ISSET(slot->fd, &eset)) {
                    active.push_back(e_win->fd_array[i]);
                }
            }
        }
        e_win->fd_count = static_cast<std::uint32_t>(active.size());
        for (std::size_t i = 0; i < active.size(); ++i) {
            e_win->fd_array[i] = active[i];
        }
    }

    g_wsa_last_error = 0;
    return result;
}

TL_MSABI int tl_ioctlsocket(const std::uintptr_t socket, const std::int32_t cmd, std::uint32_t* argp) noexcept {
    SocketSlot* slot = find_socket(socket);
    if (slot == nullptr || argp == nullptr || !mapped_range(argp, sizeof(*argp), true)) {
        g_wsa_last_error = slot == nullptr ? kWsaENotSocket : kWsaEInvalidArgument;
        return -1;
    }
    // FIONBIO = 0x8004667EU
    if (static_cast<std::uint32_t>(cmd) == 0x8004667EU) {
        int flags = fcntl(slot->fd, F_GETFL, 0);
        if (flags < 0) {
            g_wsa_last_error = errno_to_wsa(errno);
            return -1;
        }
        if (*argp != 0) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }
        if (fcntl(slot->fd, F_SETFL, flags) < 0) {
            g_wsa_last_error = errno_to_wsa(errno);
            return -1;
        }
        g_wsa_last_error = 0;
        return 0;
    }
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI int tl_gethostname(char* name, const int namelen) noexcept {
    if (name == nullptr || namelen <= 0 || !mapped_range(name, static_cast<std::size_t>(namelen), true)) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return -1;
    }
    if (::gethostname(name, static_cast<std::size_t>(namelen)) != 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI const char* tl_inet_ntop(const int af, const void* src, char* dst, const std::size_t size) noexcept {
    if (src == nullptr || dst == nullptr || size == 0 || !mapped_range(dst, size, true)) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return nullptr;
    }
    const char* res = ::inet_ntop(af == kAfInet ? AF_INET : AF_INET6, src, dst, static_cast<socklen_t>(size));
    if (res == nullptr) {
        g_wsa_last_error = errno_to_wsa(errno);
        return nullptr;
    }
    g_wsa_last_error = 0;
    return res;
}

TL_MSABI int tl_inet_pton(const int af, const char* src, void* dst) noexcept {
    if (src == nullptr || dst == nullptr || !mapped_cstring(src)) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return -1;
    }
    const int res = ::inet_pton(af == kAfInet ? AF_INET : AF_INET6, src, dst);
    if (res <= 0) {
        g_wsa_last_error = (res == 0) ? kWsaEInvalidArgument : errno_to_wsa(errno);
        return res;
    }
    g_wsa_last_error = 0;
    return res;
}

TL_MSABI int tl_getpeername(const std::uintptr_t socket, void* const name, int* const name_length) noexcept {
    if (name == nullptr || name_length == nullptr) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
    const SocketSlot* const slot = find_socket(socket);
    if (slot == nullptr) {
        g_wsa_last_error = kWsaENotSocket;
        return -1;
    }
    sockaddr_storage ss{};
    socklen_t slen = sizeof(ss);
    if (::getpeername(slot->fd, reinterpret_cast<sockaddr*>(&ss), &slen) != 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    const int copy_len = std::min(*name_length, static_cast<int>(slen));
    if (copy_len > 0) {
        std::memcpy(name, &ss, static_cast<std::size_t>(copy_len));
    }
    *name_length = copy_len;
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI int tl_setsockopt(const std::uintptr_t socket, const int level, const int optname,
                          const char* const optval, const int optlen) noexcept {
    const SocketSlot* const slot = find_socket(socket);
    if (slot == nullptr) {
        g_wsa_last_error = kWsaENotSocket;
        return -1;
    }
    int host_level = level;
    if (level == 0xFFFF) { // SOL_SOCKET Win32
        host_level = SOL_SOCKET;
    }
    if (::setsockopt(slot->fd, host_level, optname, optval, static_cast<socklen_t>(optlen)) != 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI int tl_getsockopt(const std::uintptr_t socket, const int level, const int optname,
                          char* const optval, int* const optlen) noexcept {
    const SocketSlot* const slot = find_socket(socket);
    if (slot == nullptr) {
        g_wsa_last_error = kWsaENotSocket;
        return -1;
    }
    int host_level = level;
    if (level == 0xFFFF) {
        host_level = SOL_SOCKET;
    }
    socklen_t slen = optlen != nullptr ? static_cast<socklen_t>(*optlen) : 0;
    if (::getsockopt(slot->fd, host_level, optname, optval, &slen) != 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    if (optlen != nullptr) {
        *optlen = static_cast<int>(slen);
    }
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI int tl_WSAAsyncSelect(const std::uintptr_t socket, void* const hwnd,
                              const unsigned int msg, const long events) noexcept {
    (void)socket;
    (void)hwnd;
    (void)msg;
    (void)events;
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI int tl_WSAEventSelect(const std::uintptr_t socket, void* const event_handle,
                              const long network_events) noexcept {
    (void)socket;
    (void)event_handle;
    (void)network_events;
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI void* tl_WSACreateEvent() noexcept {
    g_wsa_last_error = 0;
    return reinterpret_cast<void*>(0x57534145ULL); // 'WSAE'
}

TL_MSABI int tl_WSACloseEvent(void* const event_handle) noexcept {
    (void)event_handle;
    g_wsa_last_error = 0;
    return 1;
}

TL_MSABI int tl_WSASetEvent(void* const event_handle) noexcept {
    (void)event_handle;
    g_wsa_last_error = 0;
    return 1;
}

TL_MSABI int tl_WSAResetEvent(void* const event_handle) noexcept {
    (void)event_handle;
    g_wsa_last_error = 0;
    return 1;
}

TL_MSABI std::uint32_t tl_WSAWaitForMultipleEvents(const std::uint32_t count, const void* const* const events,
                                                  const int wait_all, const std::uint32_t timeout,
                                                  const int alertable) noexcept {
    (void)count;
    (void)events;
    (void)wait_all;
    (void)timeout;
    (void)alertable;
    g_wsa_last_error = 0;
    return 0; // WSA_WAIT_EVENT_0
}

TL_MSABI int tl_WSAEnumNetworkEvents(const std::uintptr_t socket, void* const event_handle,
                                    void* const network_events) noexcept {
    (void)socket;
    (void)event_handle;
    if (network_events != nullptr && mapped_range(network_events, 4 + 10 * 4, true)) {
        std::memset(network_events, 0, 4 + 10 * 4);
    }
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI void* tl_gethostbyname(const char* const name) noexcept {
    (void)name;
    static in_addr kAddr{};
    static char* kAddrList[2] = {reinterpret_cast<char*>(&kAddr), nullptr};
    static hostent kHostEnt{};
    kHostEnt.h_name = const_cast<char*>("localhost");
    kHostEnt.h_aliases = nullptr;
    kHostEnt.h_addrtype = AF_INET;
    kHostEnt.h_length = sizeof(in_addr);
    kHostEnt.h_addr_list = kAddrList;
    g_wsa_last_error = 0;
    return &kHostEnt;
}

TL_MSABI void* tl_getservbyname(const char* const name, const char* const proto) noexcept {
    (void)name;
    (void)proto;
    static servent kServ{};
    kServ.s_name = const_cast<char*>("ssh");
    kServ.s_port = htons(22);
    kServ.s_proto = const_cast<char*>("tcp");
    g_wsa_last_error = 0;
    return &kServ;
}

TL_MSABI void tl_WSASetLastError(const int error) noexcept {
    g_wsa_last_error = error;
}

TL_MSABI int tl___WSAFDIsSet(const std::uintptr_t socket, void* const set) noexcept {
    (void)socket;
    (void)set;
    return 0;
}

TL_MSABI int tl_WSAIoctl(const std::uintptr_t socket, const std::uint32_t io_control_code,
                         void* const in_buffer, const std::uint32_t in_buffer_size,
                         void* const out_buffer, const std::uint32_t out_buffer_size,
                         std::uint32_t* const bytes_returned, void* const overlapped,
                         void* const completion_routine) noexcept {
    (void)socket;
    (void)io_control_code;
    (void)in_buffer;
    (void)in_buffer_size;
    (void)out_buffer;
    (void)out_buffer_size;
    (void)overlapped;
    (void)completion_routine;
    if (bytes_returned != nullptr && mapped_range(bytes_returned, sizeof(std::uint32_t), true)) {
        *bytes_returned = 0;
    }
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI int tl_getnameinfo(const void* const sa, const int salen, char* const host,
                            const std::uint32_t hostlen, char* const serv,
                            const std::uint32_t servlen, const int flags) noexcept {
    (void)sa;
    (void)salen;
    (void)flags;
    if (host != nullptr && hostlen > 0 && mapped_range(host, hostlen, true)) {
        std::strncpy(host, "127.0.0.1", hostlen - 1);
        host[hostlen - 1] = '\0';
    }
    if (serv != nullptr && servlen > 0 && mapped_range(serv, servlen, true)) {
        std::strncpy(serv, "80", servlen - 1);
        serv[servlen - 1] = '\0';
    }
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI std::uintptr_t tl_WSASocketA(const int af, const int type, const int protocol,
                                      void* const protocol_info, const std::uint32_t group,
                                      const std::uint32_t flags) noexcept {
    (void)protocol_info;
    (void)group;
    (void)flags;
    return tl_socket(af, type, protocol);
}

TL_MSABI void* tl_gethostbyaddr(const char* const addr, const int len, const int type) noexcept {
    (void)addr;
    (void)len;
    (void)type;
    return tl_gethostbyname("127.0.0.1");
}

TL_MSABI char* tl_inet_ntoa(const std::uint32_t in) noexcept {
    in_addr addr{};
    addr.s_addr = in;
    return ::inet_ntoa(addr);
}

}  // extern "C"
}  // namespace tradutorlinux
