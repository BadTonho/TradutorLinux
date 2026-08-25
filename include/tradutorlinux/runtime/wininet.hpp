#pragma once

#include <cstddef>
#include <cstdint>

#include "tradutorlinux/runtime/winapi.hpp"

namespace tradutorlinux {

using HInternet = std::uintptr_t;

// Layout AMD64 de URL_COMPONENTS (wininet.h). O runtime usa a variante W e
// ponteiros para memória convidada, portanto os campos são validados antes de
// qualquer leitura ou escrita.
struct GuestUrlComponentsW {
    std::uint32_t dw_struct_size{};
    std::uint16_t* lpsz_scheme{};
    std::uint32_t dw_scheme_length{};
    std::uint32_t n_scheme{};
    std::uint16_t* lpsz_host_name{};
    std::uint32_t dw_host_name_length{};
    std::uint16_t n_port{};
    std::uint16_t reserved{};
    std::uint16_t* lpsz_user_name{};
    std::uint32_t dw_user_name_length{};
    std::uint16_t* lpsz_password{};
    std::uint32_t dw_password_length{};
    std::uint16_t* lpsz_url_path{};
    std::uint32_t dw_url_path_length{};
    std::uint16_t* lpsz_extra_info{};
    std::uint32_t dw_extra_info_length{};
};
static_assert(sizeof(GuestUrlComponentsW) == 104);
static_assert(offsetof(GuestUrlComponentsW, lpsz_scheme) == 8);
static_assert(offsetof(GuestUrlComponentsW, lpsz_host_name) == 24);
static_assert(offsetof(GuestUrlComponentsW, lpsz_url_path) == 72);

constexpr std::uint32_t kInternetOpenTypeDirect = 1;
constexpr std::uint32_t kInternetServiceHttp = 3;
constexpr std::uint32_t kInternetFlagSecure = 0x00800000U;
constexpr std::uint32_t kInternetOptionConnectTimeout = 2;
constexpr std::uint32_t kInternetOptionSendTimeout = 5;
constexpr std::uint32_t kInternetOptionReceiveTimeout = 6;

constexpr std::uint32_t kHttpAddReqFlagAdd = 0x20000000U;
constexpr std::uint32_t kHttpAddReqFlagReplace = 0x80000000U;
constexpr std::uint32_t kHttpAddReqFlagAddIfNew = 0x10000000U;

constexpr std::uint32_t kHttpQueryContentType = 1;
constexpr std::uint32_t kHttpQueryContentLength = 5;
constexpr std::uint32_t kHttpQueryStatusCode = 19;
constexpr std::uint32_t kHttpQueryRawHeaders = 21;
constexpr std::uint32_t kHttpQueryRawHeadersCrlf = 22;
constexpr std::uint32_t kHttpQueryFlagNumber = 0x20000000U;

// WinINet-specific errors used by this bounded implementation.
constexpr std::uint32_t kErrorInternetIncorrectHandleType = 12018;
constexpr std::uint32_t kErrorInternetInvalidOperation = 12016;
constexpr std::uint32_t kErrorInternetUnrecognizedScheme = 12006;
constexpr std::uint32_t kErrorInternetNameNotResolved = 12007;
constexpr std::uint32_t kErrorInternetCannotConnect = 12029;
constexpr std::uint32_t kErrorInternetTimeout = 12002;
constexpr std::uint32_t kErrorInternetSecCertInvalid = 12037;
constexpr std::uint32_t kErrorInternetInvalidUrl = 12005;

extern "C" {

TL_MSABI HInternet tl_InternetOpenW(const std::uint16_t* user_agent,
                                     std::uint32_t access_type,
                                     const std::uint16_t* proxy_name,
                                     const std::uint16_t* proxy_bypass,
                                     std::uint32_t flags) noexcept;
TL_MSABI HInternet tl_InternetConnectW(HInternet internet,
                                        const std::uint16_t* server_name,
                                        std::uint16_t server_port,
                                        const std::uint16_t* user_name,
                                        const std::uint16_t* password,
                                        std::uint32_t service,
                                        std::uint32_t flags,
                                        std::uintptr_t context) noexcept;
TL_MSABI HInternet tl_HttpOpenRequestW(HInternet connect,
                                       const std::uint16_t* verb,
                                       const std::uint16_t* object_name,
                                       const std::uint16_t* version,
                                       const std::uint16_t* referrer,
                                       const std::uint16_t* const* accept_types,
                                       std::uint32_t flags,
                                       std::uintptr_t context) noexcept;
TL_MSABI int tl_HttpAddRequestHeadersW(HInternet request,
                                       const std::uint16_t* headers,
                                       std::int32_t headers_length,
                                       std::uint32_t modifiers) noexcept;
TL_MSABI int tl_HttpSendRequestW(HInternet request,
                                 const std::uint16_t* optional_headers,
                                 std::uint32_t optional_headers_length,
                                 const void* optional_data,
                                 std::uint32_t optional_data_length) noexcept;
TL_MSABI int tl_InternetReadFile(HInternet file,
                                 void* buffer,
                                 std::uint32_t number_of_bytes_to_read,
                                 std::uint32_t* number_of_bytes_read) noexcept;
TL_MSABI int tl_InternetQueryDataAvailable(HInternet file,
                                           std::uint32_t* number_of_bytes_available,
                                           std::uint32_t flags,
                                           std::uintptr_t context) noexcept;
TL_MSABI int tl_HttpQueryInfoW(HInternet request,
                               std::uint32_t info_level,
                               void* buffer,
                               std::uint32_t* buffer_length,
                               std::uint32_t* index) noexcept;
TL_MSABI int tl_InternetSetOptionW(HInternet internet,
                                   std::uint32_t option,
                                   void* buffer,
                                   std::uint32_t buffer_length) noexcept;
TL_MSABI int tl_InternetCloseHandle(HInternet internet) noexcept;
TL_MSABI int tl_InternetCrackUrlW(const std::uint16_t* url,
                                  std::uint32_t url_length,
                                  std::uint32_t flags,
                                  GuestUrlComponentsW* components) noexcept;

}

}  // namespace tradutorlinux
