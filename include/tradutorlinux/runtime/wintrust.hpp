#pragma once

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

constexpr std::uint32_t kWtdUiNone = 2;
constexpr std::uint32_t kWtdRevokeNone = 0;
constexpr std::uint32_t kWtdChoiceBlob = 3;
constexpr std::uint32_t kWtdStateActionIgnore = 0;
constexpr std::int32_t kTrustSuccess = 0;
constexpr std::int32_t kTrustInvalidParameter = static_cast<std::int32_t>(0x80070057U);
constexpr std::int32_t kTrustUntrustedRoot = static_cast<std::int32_t>(0x800B0109U);
constexpr std::int32_t kTrustProviderUnknown = static_cast<std::int32_t>(0x800B0001U);

extern "C" {

TL_WINTRUST_MSABI std::int32_t tl_WinVerifyTrust(void* hwnd, const void* action,
                                                  GuestWintrustData* data) noexcept;

}  // extern "C"

}  // namespace tradutorlinux
