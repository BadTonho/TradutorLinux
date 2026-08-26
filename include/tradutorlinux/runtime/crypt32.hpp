#pragma once

#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_CRYPT32_MSABI __attribute__((ms_abi))
#else
#error "TL_CRYPT32_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

// Layout mínimo de CERT_CONTEXT no convidado. O runtime lê somente o blob
// DER apontado por pbCertEncoded; CERT_INFO e HCERTSTORE permanecem opacos.
struct GuestCertContext {
    std::uint32_t encoding_type{};
    std::uint8_t* encoded{};
    std::uint32_t encoded_size{};
    void* cert_info{};
    void* cert_store{};
};
static_assert(sizeof(GuestCertContext) == 40);

constexpr std::uint32_t kCertNameEmailType = 1;
constexpr std::uint32_t kCertNameRdnType = 2;
constexpr std::uint32_t kCertNameAttrType = 3;
constexpr std::uint32_t kCertNameSimpleDisplayType = 4;
constexpr std::uint32_t kCertNameFriendlyDisplayType = 5;
constexpr std::uint32_t kCertNameDnsType = 6;
constexpr std::uint32_t kCertNameIssuerFlag = 0x00000001U;

extern "C" {

TL_CRYPT32_MSABI std::uint32_t tl_CertGetNameStringW(
    const GuestCertContext* cert_context, std::uint32_t type, std::uint32_t flags,
    const void* type_parameter, std::uint16_t* name_string,
    std::uint32_t name_string_capacity) noexcept;

}  // extern "C"

}  // namespace tradutorlinux
