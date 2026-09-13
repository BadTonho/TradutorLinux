#include "tradutorlinux/runtime/ws2_32.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <thread>
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
#include "../../core/runtime_state_common.hpp"

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
constexpr std::uintptr_t kWsaEventHandleBase = 0x0000B10000000000ULL;
constexpr std::size_t kMaxWsaEvents = 64;
constexpr std::uint32_t kWsaWaitEvent0 = 0U;
constexpr std::uint32_t kWsaWaitTimeout = 258U;
constexpr std::uint32_t kWsaWaitFailed = 0xFFFFFFFFU;

constexpr long kFdRead = 0x0001L;
constexpr long kFdWrite = 0x0002L;
constexpr long kFdOob = 0x0004L;
constexpr long kFdAccept = 0x0008L;
constexpr long kFdConnect = 0x0010L;
constexpr long kFdClose = 0x0020L;
constexpr long kSupportedNetworkEvents = kFdRead | kFdWrite | kFdOob | kFdAccept | kFdConnect | kFdClose;

thread_local int g_wsa_last_error = 0;

struct SocketSlot {
    bool used{false};
    int fd{-1};
    int type{0};
    bool listening{false};
    bool connecting{false};
    void* event_handle{nullptr};
    long network_events{0};
    long pending_events{0};
    std::array<int, 10> event_errors{};
};
std::array<SocketSlot, 256> g_sockets{};
std::mutex g_sockets_mutex;

struct WsaEventSlot {
    bool used{false};
    bool signaled{false};
};
std::array<WsaEventSlot, kMaxWsaEvents> g_wsa_events{};

int errno_to_wsa(int error) noexcept;

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
std::mutex g_addrinfos_mutex;

struct GuestPollFd {
    std::uintptr_t socket{};
    std::int16_t events{};
    std::int16_t revents{};
};
static_assert(sizeof(GuestPollFd) == 16);

void* guest_address_at(const void* const base, const std::size_t offset) noexcept {
    const std::uintptr_t raw = reinterpret_cast<std::uintptr_t>(base);
    if (offset > std::numeric_limits<std::uintptr_t>::max() - raw) {
        return nullptr;
    }
    return reinterpret_cast<void*>(raw + offset);
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

WsaEventSlot* find_wsa_event(void* const handle) noexcept {
    const std::uintptr_t raw = reinterpret_cast<std::uintptr_t>(handle);
    if (raw < kWsaEventHandleBase || raw >= kWsaEventHandleBase + g_wsa_events.size()) {
        return nullptr;
    }
    WsaEventSlot& event = g_wsa_events[raw - kWsaEventHandleBase];
    return event.used ? &event : nullptr;
}

void* wsa_event_handle(WsaEventSlot& event) noexcept {
    return reinterpret_cast<void*>(kWsaEventHandleBase +
                                   static_cast<std::uintptr_t>(&event - g_wsa_events.data()));
}

int event_index(const long event) noexcept {
    switch (event) {
        case kFdRead: return 0;
        case kFdWrite: return 1;
        case kFdOob: return 2;
        case kFdAccept: return 3;
        case kFdConnect: return 4;
        case kFdClose: return 5;
        default: return -1;
    }
}

void set_pending_event(SocketSlot& slot, const long event, const int error = 0) noexcept {
    if ((slot.network_events & event) == 0) {
        return;
    }
    slot.pending_events |= event;
    if (const int index = event_index(event); index >= 0) {
        slot.event_errors[static_cast<std::size_t>(index)] = error;
    }
    if (WsaEventSlot* const event_slot = find_wsa_event(slot.event_handle)) {
        event_slot->signaled = true;
    }
}

void refresh_wsa_events_locked() noexcept {
    for (SocketSlot& slot : g_sockets) {
        if (!slot.used || slot.event_handle == nullptr || slot.network_events == 0) {
            continue;
        }

        pollfd descriptor{};
        descriptor.fd = slot.fd;
        if ((slot.network_events & (kFdRead | kFdAccept | kFdClose)) != 0) {
            descriptor.events |= POLLIN;
        }
        if ((slot.network_events & (kFdWrite | kFdConnect)) != 0) {
            descriptor.events |= POLLOUT;
        }
        if ((slot.network_events & kFdOob) != 0) {
            descriptor.events |= POLLPRI;
        }
        if (::poll(&descriptor, 1, 0) < 0) {
            if (errno != EINTR) {
                set_pending_event(slot, kFdClose, errno_to_wsa(errno));
            }
            continue;
        }

        if (slot.connecting && (descriptor.revents & (POLLOUT | POLLERR | POLLHUP)) != 0) {
            int error = 0;
            socklen_t error_length = sizeof(error);
            int wsa_error = 0;
            if (::getsockopt(slot.fd, SOL_SOCKET, SO_ERROR, &error, &error_length) != 0) {
                wsa_error = errno_to_wsa(errno);
            } else if (error != 0) {
                wsa_error = errno_to_wsa(error);
            }
            slot.connecting = false;
            set_pending_event(slot, kFdConnect, wsa_error);
        }
        if ((descriptor.revents & POLLPRI) != 0) {
            set_pending_event(slot, kFdOob);
        }
        if ((descriptor.revents & POLLOUT) != 0 && !slot.connecting) {
            set_pending_event(slot, kFdWrite);
        }
        if ((descriptor.revents & POLLIN) != 0) {
            set_pending_event(slot, slot.listening ? kFdAccept : kFdRead);
        }
        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            set_pending_event(slot, kFdClose,
                              (descriptor.revents & POLLNVAL) != 0 ? kWsaENotSocket : 0);
        }
    }
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
        runtime::read_guest_memory(address, &result, sizeof(result)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return false;
    }
    if (result.sin_family != AF_INET) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return false;
    }
    return true;
}

bool copy_host_sockaddr(const sockaddr_in& source, void* address, int* length) noexcept {
    if (address == nullptr && length == nullptr) return true;
    if (address == nullptr || length == nullptr) return false;
    int capacity = 0;
    if (!read_guest_value(length, capacity) || capacity < static_cast<int>(sizeof(source)) ||
        runtime::write_guest_memory(address, &source, sizeof(source)).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        !write_guest_value(length, static_cast<int>(sizeof(source)))) {
        return false;
    }
    return true;
}

bool write_guest_text(const char* const source, char* const destination,
                      const std::uint32_t capacity) noexcept {
    if (destination == nullptr || capacity == 0U) return false;
    const std::size_t source_length = std::strlen(source);
    const std::size_t copy_length = std::min<std::size_t>(source_length, capacity - 1U);
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(destination);
    if (copy_length > std::numeric_limits<std::uintptr_t>::max() - base ||
        runtime::write_guest_memory(destination, source, copy_length).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        !write_guest_value(reinterpret_cast<char*>(base + copy_length), static_cast<char>('\0'))) {
        return false;
    }
    return true;
}

}  // namespace

extern "C" {

TL_MSABI int tl_WSAStartup(const std::uint16_t version_requested, void* data) noexcept {
    if (data == nullptr) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return kWsaEInvalidArgument;
    }
    const std::uint16_t major = version_requested & 0xFFU;
    const std::uint16_t minor = (version_requested >> 8) & 0xFFU;
    if (major < 1 || (major == 1 && minor < 1)) {
        g_wsa_last_error = 10092; // WSAVERNOTSUPPORTED
        return 10092;
    }
    std::array<std::byte, 400> output{};
    // WSADATA: wVersion(0), wHighVersion(2), szDescription(4,257), szSystemStatus(261,128), iMaxSockets(389,2), iMaxUdpDg(391,2), lpVendorInfo(393,8)
    const std::uint16_t high_version = 0x0202U;
    std::memcpy(output.data(), &version_requested, sizeof(version_requested));
    std::memcpy(output.data() + 2, &high_version, sizeof(high_version));
    const char desc[] = "WinSock 2.0";
    const char status[] = "Running";
    std::memcpy(output.data() + 4, desc, sizeof(desc));
    std::memcpy(output.data() + 261, status, sizeof(status));
    const std::uint16_t zero = 0;
    std::memcpy(output.data() + 389, &zero, sizeof(zero));
    std::memcpy(output.data() + 391, &zero, sizeof(zero));
    if (runtime::write_guest_memory(data, output.data(), output.size()).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return kWsaEInvalidArgument;
    }
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
    std::lock_guard<std::mutex> lock(g_sockets_mutex);
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
    std::lock_guard<std::mutex> lock(g_sockets_mutex);
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
    slot->listening = true;
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
    if (!copy_host_sockaddr(address, name, name_length)) {
        ::close(fd);
        g_wsa_last_error = kWsaEFault;
        return kInvalidSocket;
    }
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
    if (!copy_guest_sockaddr(name, name_length, address)) {
        return -1;
    }
    if (::connect(slot->fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        if (errno == EINPROGRESS || errno == EALREADY) {
            slot->connecting = true;
        }
        if (g_wsa_last_error == 0) {
            g_wsa_last_error = errno_to_wsa(errno);
        }
        return -1;
    }
    slot->connecting = false;
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI int tl_send(const std::uintptr_t socket, const char* buffer, const int length,
                     const int flags) noexcept {
    SocketSlot* slot = find_socket(socket);
    if (slot == nullptr || buffer == nullptr || length < 0 || flags != 0) {
        g_wsa_last_error = slot == nullptr ? kWsaENotSocket : kWsaEInvalidArgument;
        return -1;
    }
    std::vector<char> host_buffer;
    try {
        host_buffer.resize(static_cast<std::size_t>(length));
    } catch (const std::bad_alloc&) {
        g_wsa_last_error = ENOBUFS;
        return -1;
    }
    if (length > 0 && runtime::read_guest_memory(buffer, host_buffer.data(), host_buffer.size()).status !=
                         runtime::GuestMemoryAccessStatus::Success) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
    const ssize_t result = ::send(slot->fd, host_buffer.data(), host_buffer.size(), 0);
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
    if (slot == nullptr || buffer == nullptr || length < 0 || flags != 0) {
        g_wsa_last_error = slot == nullptr ? kWsaENotSocket : kWsaEInvalidArgument;
        return -1;
    }
    std::vector<char> host_buffer;
    try {
        host_buffer.resize(static_cast<std::size_t>(length));
    } catch (const std::bad_alloc&) {
        g_wsa_last_error = ENOBUFS;
        return -1;
    }
    const ssize_t result = ::recv(slot->fd, host_buffer.data(), host_buffer.size(), 0);
    if (result < 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    if (result > 0 && runtime::write_guest_memory(buffer, host_buffer.data(),
                                                  static_cast<std::size_t>(result)).status !=
                            runtime::GuestMemoryAccessStatus::Success) {
        g_wsa_last_error = kWsaEFault;
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
        !copy_guest_sockaddr(to, to_length, address)) {
        g_wsa_last_error = slot == nullptr ? kWsaENotSocket : kWsaEInvalidArgument;
        return -1;
    }
    std::vector<char> host_buffer;
    try {
        host_buffer.resize(static_cast<std::size_t>(length));
    } catch (const std::bad_alloc&) {
        g_wsa_last_error = ENOBUFS;
        return -1;
    }
    if (length > 0 && runtime::read_guest_memory(buffer, host_buffer.data(), host_buffer.size()).status !=
                         runtime::GuestMemoryAccessStatus::Success) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
    const ssize_t result = ::sendto(slot->fd, host_buffer.data(), host_buffer.size(), 0,
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
        (from == nullptr) != (from_length == nullptr)) {
        g_wsa_last_error = slot == nullptr ? kWsaENotSocket : kWsaEInvalidArgument;
        return -1;
    }
    sockaddr_in address{};
    socklen_t host_length = sizeof(address);
    std::vector<char> host_buffer;
    try {
        host_buffer.resize(static_cast<std::size_t>(length));
    } catch (const std::bad_alloc&) {
        g_wsa_last_error = ENOBUFS;
        return -1;
    }
    const ssize_t result = ::recvfrom(slot->fd, host_buffer.data(), host_buffer.size(), 0,
                                      reinterpret_cast<sockaddr*>(&address), &host_length);
    if (result < 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    if ((result > 0 && runtime::write_guest_memory(buffer, host_buffer.data(),
                                                   static_cast<std::size_t>(result)).status !=
                               runtime::GuestMemoryAccessStatus::Success) ||
        !copy_host_sockaddr(address, from, from_length)) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
    g_wsa_last_error = 0;
    return static_cast<int>(result);
}

TL_MSABI int tl_getsockname(const std::uintptr_t socket, void* name, int* name_length) noexcept {
    SocketSlot* slot = find_socket(socket);
    int capacity = 0;
    if (slot == nullptr || name == nullptr || name_length == nullptr ||
        !read_guest_value(name_length, capacity) ||
        capacity < static_cast<int>(sizeof(sockaddr_in))) {
        g_wsa_last_error = slot == nullptr ? kWsaENotSocket : kWsaEInvalidArgument;
        return -1;
    }
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    if (::getsockname(slot->fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    if (runtime::write_guest_memory(name, &address, sizeof(address)).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        !write_guest_value(name_length, static_cast<int>(sizeof(address)))) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
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
    std::string guest_node;
    std::string guest_service;
    if (result == nullptr ||
        (node != nullptr && !runtime::copy_guest_cstring(node, 4096U, guest_node)) ||
        (service != nullptr && !runtime::copy_guest_cstring(service, 4096U, guest_service)) ||
        !write_guest_value(static_cast<void**>(result), static_cast<void*>(nullptr))) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return kWsaEInvalidArgument;
    }
    int family = AF_INET;
    int type = SOCK_STREAM;
    int protocol = IPPROTO_TCP;
    if (hints != nullptr) {
        std::array<std::byte, 16> guest_hints{};
        if (runtime::read_guest_memory(hints, guest_hints.data(), guest_hints.size()).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            g_wsa_last_error = kWsaEInvalidArgument;
            return kWsaEInvalidArgument;
        }
        std::memcpy(&family, guest_hints.data(), sizeof(int));
        std::memcpy(&type, guest_hints.data() + 8, sizeof(int));
        std::memcpy(&protocol, guest_hints.data() + 12, sizeof(int));
        if (family != 0 && family != AF_INET) {
            g_wsa_last_error = kWsaEAIFlags;
            return kWsaEAIFlags;
        }
    }
    std::lock_guard<std::mutex> lock(g_addrinfos_mutex);
    unsigned long port = 0;
    if (service != nullptr) {
        const auto parsed = std::from_chars(guest_service.data(),
                                            guest_service.data() + guest_service.size(), port, 10);
        if (parsed.ec != std::errc{} || port > 65535U) {
            g_wsa_last_error = kWsaEInvalidArgument;
            return kWsaEInvalidArgument;
        }
    }
    struct in_addr resolved_addr{};
    resolved_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (node != nullptr && guest_node != "localhost" && guest_node != "127.0.0.1") {
        struct addrinfo hints_posix{};
        hints_posix.ai_family = family == 0 ? AF_INET : family;
        hints_posix.ai_socktype = type == 0 ? SOCK_STREAM : type;
        hints_posix.ai_protocol = protocol;
        struct addrinfo* res_posix = nullptr;
        if (::getaddrinfo(guest_node.c_str(), service == nullptr ? nullptr : guest_service.c_str(),
                          &hints_posix, &res_posix) == 0 && res_posix != nullptr) {
            bool found = false;
            for (struct addrinfo* p = res_posix; p != nullptr; p = p->ai_next) {
                if (p->ai_family == AF_INET && p->ai_addr != nullptr) {
                    const auto* sin = reinterpret_cast<const sockaddr_in*>(p->ai_addr);
                    resolved_addr = sin->sin_addr;
                    found = true;
                    break;
                }
            }
            ::freeaddrinfo(res_posix);
            if (!found) {
                g_wsa_last_error = kWsaENoData;
                return kWsaENoData;
            }
        } else {
            g_wsa_last_error = kWsaENoData;
            return kWsaENoData;
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
    info.socket_address.sin_addr = resolved_addr;
    info.socket_address.sin_port = htons(static_cast<std::uint16_t>(port));
    info.address = &info.socket_address;
    info.canon_name = node != nullptr ? guest_node : "localhost";
    info.canonname = info.canon_name.data();
    info.next = nullptr;
    if (!write_guest_value(static_cast<void**>(result), static_cast<void*>(&info))) {
        g_wsa_last_error = kWsaEFault;
        return kWsaEFault;
    }
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI void tl_freeaddrinfo(void* address_info) noexcept {
    if (address_info == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_addrinfos_mutex);
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
    std::string guest_address;
    if (!runtime::copy_guest_cstring(address, 4096U, guest_address)) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return 0xFFFFFFFFU;
    }
    in_addr parsed{};
    if (::inet_aton(guest_address.c_str(), &parsed) == 0) {
        g_wsa_last_error = kWsaENoData;
        return 0xFFFFFFFFU;
    }
    g_wsa_last_error = 0;
    return parsed.s_addr;
}

TL_MSABI int tl_WSAPoll(void* descriptors, const std::uint32_t count,
                        const int timeout) noexcept {
    if (count > 64 || (count != 0 && descriptors == nullptr)) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return -1;
    }
    std::array<GuestPollFd, 64> guest_descriptors{};
    if (count != 0 && runtime::read_guest_memory(descriptors, guest_descriptors.data(),
                                                  static_cast<std::size_t>(count) * sizeof(GuestPollFd)).status !=
                             runtime::GuestMemoryAccessStatus::Success) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
    std::array<pollfd, 64> host_descriptors{};
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
        auto* const guest_revents = static_cast<std::int16_t*>(guest_address_at(
            descriptors, index * sizeof(GuestPollFd) + offsetof(GuestPollFd, revents)));
        if (guest_revents == nullptr ||
            !write_guest_value(guest_revents, static_cast<std::int16_t>(host_descriptors[index].revents))) {
            g_wsa_last_error = kWsaEFault;
            return -1;
        }
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

    Win32FdSet r_win{};
    Win32FdSet w_win{};
    Win32FdSet e_win{};
    if ((readfds != nullptr && runtime::read_guest_memory(readfds, &r_win, sizeof(r_win)).status !=
                                  runtime::GuestMemoryAccessStatus::Success) ||
        (writefds != nullptr && runtime::read_guest_memory(writefds, &w_win, sizeof(w_win)).status !=
                                   runtime::GuestMemoryAccessStatus::Success) ||
        (exceptfds != nullptr && runtime::read_guest_memory(exceptfds, &e_win, sizeof(e_win)).status !=
                                    runtime::GuestMemoryAccessStatus::Success)) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }

    if (readfds != nullptr) {
        for (std::uint32_t i = 0; i < std::min<std::uint32_t>(r_win.fd_count, 64); ++i) {
            if (SocketSlot* slot = find_socket(r_win.fd_array[i])) {
                FD_SET(slot->fd, &rset);
                max_fd = std::max(max_fd, slot->fd);
            }
        }
    }
    if (writefds != nullptr) {
        for (std::uint32_t i = 0; i < std::min<std::uint32_t>(w_win.fd_count, 64); ++i) {
            if (SocketSlot* slot = find_socket(w_win.fd_array[i])) {
                FD_SET(slot->fd, &wset);
                max_fd = std::max(max_fd, slot->fd);
            }
        }
    }
    if (exceptfds != nullptr) {
        for (std::uint32_t i = 0; i < std::min<std::uint32_t>(e_win.fd_count, 64); ++i) {
            if (SocketSlot* slot = find_socket(e_win.fd_array[i])) {
                FD_SET(slot->fd, &eset);
                max_fd = std::max(max_fd, slot->fd);
            }
        }
    }

    struct timeval tv{};
    struct timeval* ptv = nullptr;
    if (timeout != nullptr) {
        Win32TimeVal guest_timeout{};
        if (runtime::read_guest_memory(timeout, &guest_timeout, sizeof(guest_timeout)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            g_wsa_last_error = kWsaEFault;
            return -1;
        }
        tv.tv_sec = guest_timeout.tv_sec;
        tv.tv_usec = guest_timeout.tv_usec;
        ptv = &tv;
    }

    const int result = ::select(max_fd + 1, (readfds ? &rset : nullptr), (writefds ? &wset : nullptr),
                                (exceptfds ? &eset : nullptr), ptv);
    if (result < 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }

    auto publish_fd_set = [](void* const guest, const Win32FdSet& source,
                             const fd_set& ready) noexcept {
        if (guest == nullptr) return true;
        Win32FdSet output{};
        for (std::uint32_t i = 0; i < std::min<std::uint32_t>(source.fd_count, 64); ++i) {
            if (SocketSlot* slot = find_socket(source.fd_array[i])) {
                if (!FD_ISSET(slot->fd, &ready)) continue;
                if (output.fd_count < 64) {
                    output.fd_array[output.fd_count++] = source.fd_array[i];
                }
            }
        }
        return runtime::write_guest_memory(guest, &output, sizeof(output)).status ==
               runtime::GuestMemoryAccessStatus::Success;
    };
    if (!publish_fd_set(readfds, r_win, rset) || !publish_fd_set(writefds, w_win, wset) ||
        !publish_fd_set(exceptfds, e_win, eset)) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }

    g_wsa_last_error = 0;
    return result;
}

TL_MSABI int tl_ioctlsocket(const std::uintptr_t socket, const std::int32_t cmd, std::uint32_t* argp) noexcept {
    SocketSlot* slot = find_socket(socket);
    std::uint32_t argument = 0;
    if (slot == nullptr || argp == nullptr || !read_guest_value(argp, argument)) {
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
        if (argument != 0U) {
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
    if (name == nullptr || namelen <= 0 || namelen > 4096) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return -1;
    }
    std::array<char, 4096> hostname{};
    if (::gethostname(hostname.data(), static_cast<std::size_t>(namelen)) != 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    if (runtime::write_guest_memory(name, hostname.data(), static_cast<std::size_t>(namelen)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI const char* tl_inet_ntop(const int af, const void* src, char* dst, const std::size_t size) noexcept {
    if (src == nullptr || dst == nullptr || size == 0) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return nullptr;
    }
    const int host_family = af == kAfInet ? AF_INET : AF_INET6;
    const std::size_t source_size = host_family == AF_INET ? sizeof(in_addr) : sizeof(in6_addr);
    std::array<std::byte, sizeof(in6_addr)> source{};
    if (runtime::read_guest_memory(src, source.data(), source_size).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        g_wsa_last_error = kWsaEFault;
        return nullptr;
    }
    std::array<char, INET6_ADDRSTRLEN> formatted{};
    const char* res = ::inet_ntop(host_family, source.data(), formatted.data(), formatted.size());
    if (res == nullptr) {
        g_wsa_last_error = errno_to_wsa(errno);
        return nullptr;
    }
    const std::size_t output_size = std::strlen(formatted.data()) + 1U;
    if (output_size > size ||
        runtime::write_guest_memory(dst, formatted.data(), output_size).status !=
            runtime::GuestMemoryAccessStatus::Success) {
        g_wsa_last_error = kWsaEFault;
        return nullptr;
    }
    g_wsa_last_error = 0;
    return dst;
}

TL_MSABI int tl_inet_pton(const int af, const char* src, void* dst) noexcept {
    if (src == nullptr || dst == nullptr) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return -1;
    }
    std::string guest_source;
    if (!runtime::copy_guest_cstring(src, 4096U, guest_source)) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
    const int host_family = af == kAfInet ? AF_INET : AF_INET6;
    std::array<std::byte, sizeof(in6_addr)> parsed{};
    const int res = ::inet_pton(host_family, guest_source.c_str(), parsed.data());
    if (res <= 0) {
        g_wsa_last_error = (res == 0) ? kWsaEInvalidArgument : errno_to_wsa(errno);
        return res;
    }
    const std::size_t output_size = host_family == AF_INET ? sizeof(in_addr) : sizeof(in6_addr);
    if (runtime::write_guest_memory(dst, parsed.data(), output_size).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
    g_wsa_last_error = 0;
    return res;
}

TL_MSABI int tl_WSAAddressToStringA(const void* const address, const std::uint32_t address_length,
                                    const void* const protocol_info, char* const address_string,
                                    std::uint32_t* const address_string_length) noexcept {
    (void)protocol_info;
    if (address == nullptr || address_length < sizeof(sockaddr_in) ||
        address_string_length == nullptr) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return -1;
    }
    sockaddr_in socket_address{};
    std::uint32_t capacity = 0;
    if (runtime::read_guest_memory(address, &socket_address, sizeof(socket_address)).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        !read_guest_value(address_string_length, capacity)) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
    if (socket_address.sin_family != AF_INET) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return -1;
    }
    char address_text[INET_ADDRSTRLEN]{};
    if (::inet_ntop(AF_INET, &socket_address.sin_addr, address_text, sizeof(address_text)) == nullptr) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    char formatted[INET_ADDRSTRLEN + 1U + 5U]{};
    const int written = std::snprintf(formatted, sizeof(formatted), "%s:%u", address_text,
                                      static_cast<unsigned>(ntohs(socket_address.sin_port)));
    if (written < 0 || static_cast<std::size_t>(written) >= sizeof(formatted)) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return -1;
    }
    const std::uint32_t required = static_cast<std::uint32_t>(written) + 1U;
    if (address_string == nullptr || capacity < required) {
        if (!write_guest_value(address_string_length, required)) {
            g_wsa_last_error = kWsaEFault;
            return -1;
        }
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
    if (runtime::write_guest_memory(address_string, formatted, required).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        !write_guest_value(address_string_length, required)) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
    g_wsa_last_error = 0;
    return 0;
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
    int capacity = 0;
    if (!read_guest_value(name_length, capacity) || capacity < 0) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
    const int copy_len = std::min(capacity, static_cast<int>(slen));
    if (copy_len > 0) {
        if (runtime::write_guest_memory(name, &ss, static_cast<std::size_t>(copy_len)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            g_wsa_last_error = kWsaEFault;
            return -1;
        }
    }
    if (!write_guest_value(name_length, copy_len)) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
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
    if (optlen < 0 || (optlen > 0 && optval == nullptr)) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return -1;
    }
    std::vector<char> host_option;
    try {
        host_option.resize(static_cast<std::size_t>(optlen));
    } catch (const std::bad_alloc&) {
        g_wsa_last_error = ENOBUFS;
        return -1;
    }
    if (optlen > 0 && runtime::read_guest_memory(optval, host_option.data(), host_option.size()).status !=
                         runtime::GuestMemoryAccessStatus::Success) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
    if (::setsockopt(slot->fd, host_level, optname, host_option.data(),
                     static_cast<socklen_t>(optlen)) != 0) {
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
    int capacity = 0;
    if (optlen == nullptr || !read_guest_value(optlen, capacity) || capacity < 0 ||
        (capacity > 0 && optval == nullptr)) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return -1;
    }
    std::vector<char> host_option;
    try {
        host_option.resize(static_cast<std::size_t>(capacity));
    } catch (const std::bad_alloc&) {
        g_wsa_last_error = ENOBUFS;
        return -1;
    }
    socklen_t slen = static_cast<socklen_t>(capacity);
    if (::getsockopt(slot->fd, host_level, optname, host_option.data(), &slen) != 0) {
        g_wsa_last_error = errno_to_wsa(errno);
        return -1;
    }
    if (slen > static_cast<socklen_t>(capacity) ||
        (slen > 0U && runtime::write_guest_memory(optval, host_option.data(), slen).status !=
                           runtime::GuestMemoryAccessStatus::Success) ||
        !write_guest_value(optlen, static_cast<int>(slen))) {
        g_wsa_last_error = kWsaEFault;
        return -1;
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
    std::lock_guard<std::mutex> lock(g_sockets_mutex);
    SocketSlot* const slot = find_socket(socket);
    WsaEventSlot* const event = event_handle != nullptr ? find_wsa_event(event_handle) : nullptr;
    if (slot == nullptr || network_events < 0 || (network_events & ~kSupportedNetworkEvents) != 0 ||
        (event_handle != nullptr && event == nullptr) ||
        (event_handle == nullptr && network_events != 0)) {
        g_wsa_last_error = slot == nullptr ? kWsaENotSocket : kWsaEInvalidArgument;
        return -1;
    }
    if (network_events != 0) {
        const int flags = fcntl(slot->fd, F_GETFL, 0);
        if (flags < 0 || fcntl(slot->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            g_wsa_last_error = errno_to_wsa(errno);
            return -1;
        }
    }
    slot->event_handle = event_handle;
    slot->network_events = network_events;
    slot->pending_events = 0;
    slot->event_errors.fill(0);
    if (event != nullptr) {
        event->signaled = false;
    }
    g_wsa_last_error = 0;
    return 0;
}

TL_MSABI void* tl_WSACreateEvent() noexcept {
    std::lock_guard<std::mutex> lock(g_sockets_mutex);
    auto free_it = std::find_if(g_wsa_events.begin(), g_wsa_events.end(),
                                [](const WsaEventSlot& event) { return !event.used; });
    if (free_it == g_wsa_events.end()) {
        g_wsa_last_error = ENOBUFS;
        return nullptr;
    }
    free_it->used = true;
    free_it->signaled = false;
    g_wsa_last_error = 0;
    return wsa_event_handle(*free_it);
}

TL_MSABI int tl_WSACloseEvent(void* const event_handle) noexcept {
    std::lock_guard<std::mutex> lock(g_sockets_mutex);
    WsaEventSlot* const event = find_wsa_event(event_handle);
    if (event == nullptr) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return 0;
    }
    for (SocketSlot& slot : g_sockets) {
        if (slot.event_handle == event_handle) {
            slot.event_handle = nullptr;
            slot.network_events = 0;
            slot.pending_events = 0;
            slot.event_errors.fill(0);
        }
    }
    *event = {};
    g_wsa_last_error = 0;
    return 1;
}

TL_MSABI int tl_WSASetEvent(void* const event_handle) noexcept {
    std::lock_guard<std::mutex> lock(g_sockets_mutex);
    WsaEventSlot* const event = find_wsa_event(event_handle);
    if (event == nullptr) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return 0;
    }
    event->signaled = true;
    g_wsa_last_error = 0;
    return 1;
}

TL_MSABI int tl_WSAResetEvent(void* const event_handle) noexcept {
    std::lock_guard<std::mutex> lock(g_sockets_mutex);
    WsaEventSlot* const event = find_wsa_event(event_handle);
    if (event == nullptr) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return 0;
    }
    event->signaled = false;
    g_wsa_last_error = 0;
    return 1;
}

TL_MSABI std::uint32_t tl_WSAWaitForMultipleEvents(const std::uint32_t count, const void* const* const events,
                                                  const int wait_all, const std::uint32_t timeout,
                                                  const int alertable) noexcept {
    (void)alertable;
    if (count == 0 || count > kMaxWsaEvents || events == nullptr ||
        (wait_all != 0 && wait_all != 1)) {
        g_wsa_last_error = kWsaEInvalidArgument;
        return kWsaWaitFailed;
    }
    std::array<void*, kMaxWsaEvents> guest_events{};
    if (runtime::read_guest_memory(events, guest_events.data(),
                                   static_cast<std::size_t>(count) * sizeof(void*)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        g_wsa_last_error = kWsaEFault;
        return kWsaWaitFailed;
    }
    const auto start = std::chrono::steady_clock::now();
    const bool infinite = timeout == 0xFFFFFFFFU;
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(g_sockets_mutex);
            refresh_wsa_events_locked();
            bool all_signaled = true;
            std::uint32_t first_signaled = count;
            for (std::uint32_t index = 0; index < count; ++index) {
                WsaEventSlot* const event = find_wsa_event(guest_events[index]);
                if (event == nullptr) {
                    g_wsa_last_error = kWsaEInvalidArgument;
                    return kWsaWaitFailed;
                }
                if (event->signaled) {
                    if (first_signaled == count) {
                        first_signaled = index;
                    }
                } else {
                    all_signaled = false;
                }
            }
            if ((wait_all && all_signaled) || (!wait_all && first_signaled != count)) {
                g_wsa_last_error = 0;
                return kWsaWaitEvent0 + (wait_all ? 0U : first_signaled);
            }
        }

        if (timeout == 0) {
            g_wsa_last_error = 0;
            return kWsaWaitTimeout;
        }
        if (!infinite) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            if (elapsed.count() >= timeout) {
                g_wsa_last_error = 0;
                return kWsaWaitTimeout;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    g_wsa_last_error = 0;
    return kWsaWaitTimeout;
}

TL_MSABI int tl_WSAEnumNetworkEvents(const std::uintptr_t socket, void* const event_handle,
                                    void* const network_events) noexcept {
    constexpr std::size_t kNetworkEventsSize = 4U + 10U * 4U;
    if (network_events == nullptr) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
    std::lock_guard<std::mutex> lock(g_sockets_mutex);
    SocketSlot* const slot = find_socket(socket);
    WsaEventSlot* const event = event_handle != nullptr ? find_wsa_event(event_handle) : nullptr;
    if (slot == nullptr || (event_handle != nullptr && event == nullptr) ||
        (event_handle != nullptr && slot->event_handle != event_handle)) {
        g_wsa_last_error = slot == nullptr ? kWsaENotSocket : kWsaEInvalidArgument;
        return -1;
    }
    std::array<std::byte, kNetworkEventsSize> output{};
    const std::int32_t pending = static_cast<std::int32_t>(slot->pending_events);
    std::memcpy(output.data(), &pending, sizeof(pending));
    for (int index = 0; index < 10; ++index) {
        std::memcpy(output.data() + sizeof(pending) + static_cast<std::size_t>(index) * sizeof(std::int32_t),
                    &slot->event_errors[static_cast<std::size_t>(index)], sizeof(std::int32_t));
    }
    if (runtime::write_guest_memory(network_events, output.data(), output.size()).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
    slot->pending_events = 0;
    slot->event_errors.fill(0);
    if (event != nullptr) {
        event->signaled = false;
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
    if (bytes_returned != nullptr && !write_guest_value(bytes_returned, std::uint32_t{0})) {
        g_wsa_last_error = kWsaEFault;
        return -1;
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
    if (host != nullptr && hostlen > 0U && !write_guest_text("127.0.0.1", host, hostlen)) {
        g_wsa_last_error = kWsaEFault;
        return -1;
    }
    if (serv != nullptr && servlen > 0U && !write_guest_text("80", serv, servlen)) {
        g_wsa_last_error = kWsaEFault;
        return -1;
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

namespace tradutorlinux::loader {

void register_ws2_32_module() {
    static const ExportedFunction kWs2_32Exports[] = {
        {"accept", 1, reinterpret_cast<std::uintptr_t>(&tl_accept), ExportSupport::Full},
        {"bind", 2, reinterpret_cast<std::uintptr_t>(&tl_bind), ExportSupport::Full},
        {"closesocket", 3, reinterpret_cast<std::uintptr_t>(&tl_closesocket), ExportSupport::Full},
        {"connect", 4, reinterpret_cast<std::uintptr_t>(&tl_connect), ExportSupport::Full},
        {"getpeername", 5, reinterpret_cast<std::uintptr_t>(&tl_getpeername), ExportSupport::Full},
        {"getsockname", 6, reinterpret_cast<std::uintptr_t>(&tl_getsockname), ExportSupport::Full},
        {"getsockopt", 7, reinterpret_cast<std::uintptr_t>(&tl_getsockopt), ExportSupport::Full},
        {"htonl", 8, reinterpret_cast<std::uintptr_t>(&tl_htonl), ExportSupport::Full},
        {"htons", 9, reinterpret_cast<std::uintptr_t>(&tl_htons), ExportSupport::Full},
        {"inet_addr", 10, reinterpret_cast<std::uintptr_t>(&tl_inet_addr), ExportSupport::Full},
        {"inet_ntoa", 11, reinterpret_cast<std::uintptr_t>(&tl_inet_ntoa), ExportSupport::Full},
        {"ioctlsocket", 12, reinterpret_cast<std::uintptr_t>(&tl_ioctlsocket), ExportSupport::Full},
        {"listen", 13, reinterpret_cast<std::uintptr_t>(&tl_listen), ExportSupport::Full},
        {"ntohl", 14, reinterpret_cast<std::uintptr_t>(&tl_ntohl), ExportSupport::Full},
        {"ntohs", 15, reinterpret_cast<std::uintptr_t>(&tl_ntohs), ExportSupport::Full},
        {"recv", 16, reinterpret_cast<std::uintptr_t>(&tl_recv), ExportSupport::Full},
        {"recvfrom", 17, reinterpret_cast<std::uintptr_t>(&tl_recvfrom), ExportSupport::Full},
        {"select", 18, reinterpret_cast<std::uintptr_t>(&tl_select), ExportSupport::Full},
        {"send", 19, reinterpret_cast<std::uintptr_t>(&tl_send), ExportSupport::Full},
        {"sendto", 20, reinterpret_cast<std::uintptr_t>(&tl_sendto), ExportSupport::Full},
        {"setsockopt", 21, reinterpret_cast<std::uintptr_t>(&tl_setsockopt), ExportSupport::Full},
        {"shutdown", 22, reinterpret_cast<std::uintptr_t>(&tl_shutdown), ExportSupport::Full},
        {"socket", 23, reinterpret_cast<std::uintptr_t>(&tl_socket), ExportSupport::Full},
        {"gethostbyaddr", 51, reinterpret_cast<std::uintptr_t>(&tl_gethostbyaddr), ExportSupport::Full},
        {"gethostbyname", 52, reinterpret_cast<std::uintptr_t>(&tl_gethostbyname), ExportSupport::Full},
        {"gethostname", 57, reinterpret_cast<std::uintptr_t>(&tl_gethostname), ExportSupport::Full},
        {"getservbyport", 56, reinterpret_cast<std::uintptr_t>(&tl_getservbyname), ExportSupport::Full},
        {"getservbyname", 55, reinterpret_cast<std::uintptr_t>(&tl_getservbyname), ExportSupport::Full},
        {"WSAGetLastError", 111, reinterpret_cast<std::uintptr_t>(&tl_WSAGetLastError), ExportSupport::Full},
        {"WSASetLastError", 112, reinterpret_cast<std::uintptr_t>(&tl_WSASetLastError), ExportSupport::Full},
        {"WSAAsyncSelect", 115, reinterpret_cast<std::uintptr_t>(&tl_WSAAsyncSelect), ExportSupport::Full},
        {"WSAAsyncGetHostByName", 116, reinterpret_cast<std::uintptr_t>(&tl_gethostbyname), ExportSupport::Full},
        {"__WSAFDIsSet", 151, reinterpret_cast<std::uintptr_t>(&tl___WSAFDIsSet), ExportSupport::Full},
        {"WSAStartup", 1001, reinterpret_cast<std::uintptr_t>(&tl_WSAStartup), ExportSupport::Full},
        {"WSACleanup", 1002, reinterpret_cast<std::uintptr_t>(&tl_WSACleanup), ExportSupport::Full},
        {"getaddrinfo", 1003, reinterpret_cast<std::uintptr_t>(&tl_getaddrinfo), ExportSupport::Full},
        {"freeaddrinfo", 1004, reinterpret_cast<std::uintptr_t>(&tl_freeaddrinfo), ExportSupport::Full},
        {"WSAPoll", 1005, reinterpret_cast<std::uintptr_t>(&tl_WSAPoll), ExportSupport::Full},
        {"inet_ntop", 1006, reinterpret_cast<std::uintptr_t>(&tl_inet_ntop), ExportSupport::Full},
        {"inet_pton", 1007, reinterpret_cast<std::uintptr_t>(&tl_inet_pton), ExportSupport::Full},
        {"WSAEventSelect", 1008, reinterpret_cast<std::uintptr_t>(&tl_WSAEventSelect), ExportSupport::Full},
        {"WSACreateEvent", 1009, reinterpret_cast<std::uintptr_t>(&tl_WSACreateEvent), ExportSupport::Full},
        {"WSACloseEvent", 1010, reinterpret_cast<std::uintptr_t>(&tl_WSACloseEvent), ExportSupport::Full},
        {"WSASetEvent", 1011, reinterpret_cast<std::uintptr_t>(&tl_WSASetEvent), ExportSupport::Full},
        {"WSAResetEvent", 1012, reinterpret_cast<std::uintptr_t>(&tl_WSAResetEvent), ExportSupport::Full},
        {"WSAWaitForMultipleEvents", 1013, reinterpret_cast<std::uintptr_t>(&tl_WSAWaitForMultipleEvents), ExportSupport::Full},
        {"WSAEnumNetworkEvents", 1014, reinterpret_cast<std::uintptr_t>(&tl_WSAEnumNetworkEvents), ExportSupport::Full},
        {"WSAIoctl", 1015, reinterpret_cast<std::uintptr_t>(&tl_WSAIoctl), ExportSupport::Full},
        {"getnameinfo", 1016, reinterpret_cast<std::uintptr_t>(&tl_getnameinfo), ExportSupport::Full},
        {"WSASocketA", 1017, reinterpret_cast<std::uintptr_t>(&tl_WSASocketA), ExportSupport::Full},
        {"WSAAddressToStringA", 1018, reinterpret_cast<std::uintptr_t>(&tl_WSAAddressToStringA), ExportSupport::Full},
    };
    static const InternalModule kWs2_32Module{"WS2_32.dll", kWs2_32Exports};
    register_module(kWs2_32Module);
}

}  // namespace tradutorlinux::loader
