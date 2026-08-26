#pragma once

#include "tradutorlinux/runtime/crypt32.hpp"

#include <cstddef>
#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_WINTRUST_MSABI __attribute__((ms_abi))
#else
#error "TL_WINTRUST_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

struct GuestWintrustBlobInfo {
    std::uint32_t cb_struct{};
    std::uint8_t subject[16]{};
    std::uint32_t cb_mem_object{};
    std::uint8_t* pb_mem_object{};
    std::uint16_t* display_name{};
};
static_assert(sizeof(GuestWintrustBlobInfo) == 40);

struct GuestWintrustData {
    std::uint32_t cb_struct{};
    void* policy_callback_data{};
    void* sip_client_data{};
    std::uint32_t ui_choice{};
    std::uint32_t revocation_checks{};
    std::uint32_t union_choice{};
    void* union_data{};
    std::uint32_t state_action{};
    void* state_data{};
    std::uint16_t* url_reference{};
    std::uint32_t provider_flags{};
    std::uint32_t ui_context{};
    void* signature_settings{};
};
static_assert(sizeof(GuestWintrustData) == 88);

// Prefixos compatíveis com os registros que os três WTHelper* devolvem. O
// runtime preenche somente a cadeia criada por WTD_STATEACTION_VERIFY; os
// demais callbacks, stores e campos de política permanecem nulos.
struct GuestWintrustProviderCert;
struct GuestWintrustSigner;

struct GuestWintrustProviderData {
    std::uint32_t cb_struct{};
    std::uint32_t reserved0{};
    void* wintrust_data{};
    std::uint32_t opened_file{};
    std::uint32_t reserved1{};
    void* parent_window{};
    void* action_id{};
    void* provider{};
    std::uint32_t error{};
    std::uint32_t security_settings{};
    std::uint32_t policy_settings{};
    void* provider_functions{};
    std::uint32_t trust_step_errors{};
    std::uint32_t reserved2{};
    std::uint32_t* trust_step_error_values{};
    std::uint32_t store_count{};
    std::uint32_t reserved3{};
    void* stores{};
    std::uint32_t encoding{};
    std::uint32_t reserved4{};
    void* message{};
    std::uint32_t signer_count{};
    std::uint32_t reserved5{};
    GuestWintrustSigner* signers{};
    std::uint32_t private_data_count{};
    std::uint32_t reserved6{};
    void* private_data{};
    std::uint32_t subject_choice{};
    std::uint32_t reserved7{};
    void* sip_data{};
    char* usage_oid{};
    std::uint32_t recall_with_state{};
    std::uint32_t system_time_low{};
    std::uint32_t system_time_high{};
    char* ctl_signer_usage_oid{};
    std::uint32_t provider_flags{};
    std::uint32_t final_error{};
    void* request_usage{};
    std::uint32_t trust_public_settings{};
    std::uint32_t ui_state_flags{};
};
static_assert(sizeof(GuestWintrustProviderData) == 224);

struct GuestWintrustSigner {
    std::uint32_t cb_struct{};
    std::uint32_t verify_time_low{};
    std::uint32_t verify_time_high{};
    std::uint32_t cert_count{};
    GuestWintrustProviderCert* cert_chain{};
    std::uint32_t signer_type{};
    std::uint32_t reserved1{};
    void* signer_info{};
    std::uint32_t error{};
    std::uint32_t counter_signer_count{};
    GuestWintrustSigner* counter_signers{};
    void* chain_context{};
};
static_assert(sizeof(GuestWintrustSigner) == 64);

struct GuestWintrustProviderCert {
    std::uint32_t cb_struct{};
    std::uint32_t reserved0{};
    GuestCertContext* cert_context{};
    std::uint32_t commercial{};
    std::uint32_t trusted_root{};
    std::uint32_t self_signed{};
    std::uint32_t test_cert{};
    std::uint32_t revoked_reason{};
    std::uint32_t confidence{};
    std::uint32_t error{};
    void* trust_list_context{};
    std::uint32_t trust_list_signer{};
    std::uint32_t reserved1{};
    void* ctl_context{};
    std::uint32_t ctl_error{};
    std::uint32_t cyclic{};
    void* chain_element{};
};
static_assert(sizeof(GuestWintrustProviderCert) == 88);

constexpr std::uint32_t kWtdUiNone = 2;
constexpr std::uint32_t kWtdRevokeNone = 0;
constexpr std::uint32_t kWtdChoiceBlob = 3;
constexpr std::uint32_t kWtdStateActionIgnore = 0;
constexpr std::uint32_t kWtdStateActionVerify = 1;
constexpr std::uint32_t kWtdStateActionClose = 2;
constexpr std::int32_t kTrustSuccess = 0;
constexpr std::int32_t kTrustInvalidParameter = static_cast<std::int32_t>(0x80070057U);
constexpr std::int32_t kTrustUntrustedRoot = static_cast<std::int32_t>(0x800B0109U);
constexpr std::int32_t kTrustProviderUnknown = static_cast<std::int32_t>(0x800B0001U);

extern "C" {

TL_WINTRUST_MSABI std::int32_t tl_WinVerifyTrust(void* hwnd, const void* action,
                                                  GuestWintrustData* data) noexcept;

TL_WINTRUST_MSABI GuestWintrustProviderData* tl_WTHelperProvDataFromStateData(
    void* state_data) noexcept;

TL_WINTRUST_MSABI GuestWintrustSigner* tl_WTHelperGetProvSignerFromChain(
    GuestWintrustProviderData* provider_data, std::uint32_t signer_index,
    std::int32_t counter_signer, std::uint32_t counter_signer_index) noexcept;

TL_WINTRUST_MSABI GuestWintrustProviderCert* tl_WTHelperGetProvCertFromChain(
    GuestWintrustSigner* signer, std::uint32_t cert_index) noexcept;

}  // extern "C"

}  // namespace tradutorlinux
