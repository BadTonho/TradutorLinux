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

constexpr std::uint32_t kCryptENotFound = 0x80092004U;
constexpr std::uint32_t kErrorMoreData = 234U;

constexpr std::uintptr_t kCertStoreProvMemory = 2;
constexpr std::uintptr_t kCertStoreProvSystemA = 9;
constexpr std::uintptr_t kCertStoreProvSystemW = 10;

constexpr std::uint32_t kCertCloseStoreForceFlag = 0x00000001U;
constexpr std::uint32_t kCertCloseStoreCheckFlag = 0x00000002U;

constexpr std::uint32_t kCertFindAny = 0;
constexpr std::uint32_t kCertFindSha1Hash = 0x00010000U;
constexpr std::uint32_t kCertFindMd5Hash = 0x00040000U;
constexpr std::uint32_t kCertFindProperty = 0x00050000U;
constexpr std::uint32_t kCertFindSubjectStrA = 0x00070007U;
constexpr std::uint32_t kCertFindSubjectStrW = 0x00080007U;
constexpr std::uint32_t kCertFindIssuerStrA = 0x00070004U;
constexpr std::uint32_t kCertFindIssuerStrW = 0x00080004U;

constexpr std::uint32_t kCertSha1HashPropId = 3;
constexpr std::uint32_t kCertMd5HashPropId = 4;
constexpr std::uint32_t kCertFriendlyNamePropId = 11;

struct GuestDataBlob {
    std::uint32_t size{};
    std::uint8_t* data{};
};

extern "C" {

TL_CRYPT32_MSABI std::uint32_t tl_CertGetNameStringW(
    const GuestCertContext* cert_context, std::uint32_t type, std::uint32_t flags,
    const void* type_parameter, std::uint16_t* name_string,
    std::uint32_t name_string_capacity) noexcept;

TL_CRYPT32_MSABI const GuestCertContext* tl_CertDuplicateCertificateContext(
    const GuestCertContext* cert_context) noexcept;

TL_CRYPT32_MSABI std::uint32_t tl_CertFreeCertificateContext(
    const GuestCertContext* cert_context) noexcept;

TL_CRYPT32_MSABI void* tl_CertOpenStore(
    const char* store_provider, std::uint32_t encoding_type, void* crypt_prov,
    std::uint32_t flags, const void* para) noexcept;

TL_CRYPT32_MSABI std::uint32_t tl_CertCloseStore(
    void* cert_store, std::uint32_t flags) noexcept;

TL_CRYPT32_MSABI const GuestCertContext* tl_CertEnumCertificatesInStore(
    void* cert_store, const GuestCertContext* prev_cert_context) noexcept;

TL_CRYPT32_MSABI const GuestCertContext* tl_CertFindCertificateInStore(
    void* cert_store, std::uint32_t encoding_type, std::uint32_t find_flags,
    std::uint32_t find_type, const void* find_para,
    const GuestCertContext* prev_cert_context) noexcept;

TL_CRYPT32_MSABI std::uint32_t tl_CertGetCertificateContextProperty(
    const GuestCertContext* cert_context, std::uint32_t prop_id, void* data,
    std::uint32_t* data_size) noexcept;

TL_CRYPT32_MSABI void* tl_CertOpenSystemStoreA(
    void* crypt_prov, const char* system_store_name) noexcept;

TL_CRYPT32_MSABI void* tl_CertOpenSystemStoreW(
    void* crypt_prov, const std::uint16_t* system_store_name) noexcept;

}  // extern "C"

}  // namespace tradutorlinux
