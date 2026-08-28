#pragma once

#include <cstddef>
#include <cstdint>

#include "tradutorlinux/runtime/winapi.hpp"

namespace tradutorlinux {

constexpr std::uintptr_t kInvalidSocket = ~static_cast<std::uintptr_t>(0);

extern "C" {

TL_MSABI int tl_WSAStartup(std::uint16_t version_requested, void* data) noexcept;
TL_MSABI int tl_WSACleanup() noexcept;
TL_MSABI int tl_WSAGetLastError() noexcept;
TL_MSABI std::uintptr_t tl_socket(int address_family, int type, int protocol) noexcept;
TL_MSABI int tl_closesocket(std::uintptr_t socket) noexcept;
TL_MSABI int tl_bind(std::uintptr_t socket, const void* name, int name_length) noexcept;
TL_MSABI int tl_listen(std::uintptr_t socket, int backlog) noexcept;
TL_MSABI std::uintptr_t tl_accept(std::uintptr_t socket, void* name,
                                  int* name_length) noexcept;
TL_MSABI int tl_connect(std::uintptr_t socket, const void* name, int name_length) noexcept;
TL_MSABI int tl_send(std::uintptr_t socket, const char* buffer, int length,
                     int flags) noexcept;
TL_MSABI int tl_recv(std::uintptr_t socket, char* buffer, int length, int flags) noexcept;
TL_MSABI int tl_sendto(std::uintptr_t socket, const char* buffer, int length, int flags,
                       const void* to, int to_length) noexcept;
TL_MSABI int tl_recvfrom(std::uintptr_t socket, char* buffer, int length, int flags,
                         void* from, int* from_length) noexcept;
TL_MSABI int tl_getsockname(std::uintptr_t socket, void* name, int* name_length) noexcept;
TL_MSABI int tl_shutdown(std::uintptr_t socket, int how) noexcept;
TL_MSABI int tl_getaddrinfo(const char* node, const char* service, const void* hints,
                            void* result) noexcept;
TL_MSABI void tl_freeaddrinfo(void* address_info) noexcept;
TL_MSABI std::uint16_t tl_htons(std::uint16_t host_short) noexcept;
TL_MSABI std::uint16_t tl_ntohs(std::uint16_t network_short) noexcept;
TL_MSABI std::uint32_t tl_htonl(std::uint32_t host_long) noexcept;
TL_MSABI std::uint32_t tl_ntohl(std::uint32_t network_long) noexcept;
TL_MSABI std::uint32_t tl_inet_addr(const char* address) noexcept;
TL_MSABI int tl_WSAPoll(void* descriptors, std::uint32_t count, int timeout) noexcept;
TL_MSABI int tl_select(int nfds, void* readfds, void* writefds, void* exceptfds, const void* timeout) noexcept;
TL_MSABI int tl_ioctlsocket(std::uintptr_t socket, std::int32_t cmd, std::uint32_t* argp) noexcept;
TL_MSABI int tl_gethostname(char* name, int namelen) noexcept;
TL_MSABI const char* tl_inet_ntop(int af, const void* src, char* dst, std::size_t size) noexcept;
TL_MSABI int tl_inet_pton(int af, const char* src, void* dst) noexcept;
TL_MSABI int tl_getpeername(std::uintptr_t socket, void* name, int* name_length) noexcept;
TL_MSABI int tl_setsockopt(std::uintptr_t socket, int level, int optname, const char* optval, int optlen) noexcept;
TL_MSABI int tl_getsockopt(std::uintptr_t socket, int level, int optname, char* optval, int* optlen) noexcept;
TL_MSABI int tl_WSAAsyncSelect(std::uintptr_t socket, void* hwnd, unsigned int msg, long events) noexcept;
TL_MSABI int tl_WSAEventSelect(std::uintptr_t socket, void* event_handle, long network_events) noexcept;
TL_MSABI void* tl_WSACreateEvent() noexcept;
TL_MSABI int tl_WSACloseEvent(void* event_handle) noexcept;
TL_MSABI int tl_WSASetEvent(void* event_handle) noexcept;
TL_MSABI int tl_WSAResetEvent(void* event_handle) noexcept;
TL_MSABI std::uint32_t tl_WSAWaitForMultipleEvents(std::uint32_t count, const void* const* events,
                                                  int wait_all, std::uint32_t timeout, int alertable) noexcept;
TL_MSABI int tl_WSAEnumNetworkEvents(std::uintptr_t socket, void* event_handle, void* network_events) noexcept;
TL_MSABI void* tl_gethostbyname(const char* name) noexcept;
TL_MSABI void* tl_getservbyname(const char* name, const char* proto) noexcept;
TL_MSABI void tl_WSASetLastError(int error) noexcept;
TL_MSABI int tl___WSAFDIsSet(std::uintptr_t socket, void* set) noexcept;

}

}  // namespace tradutorlinux
