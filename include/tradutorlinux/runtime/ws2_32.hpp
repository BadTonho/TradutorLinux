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

}

}  // namespace tradutorlinux
