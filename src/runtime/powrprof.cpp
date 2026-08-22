#include "tradutorlinux/runtime/winapi.hpp"
#include "runtime_context.hpp"

#include <cstdlib>
#include <cstring>

namespace tradutorlinux {

extern "C" {

TL_MSABI std::uint32_t tl_PowerGetActiveScheme(void* UserRootPowerKey, void** ActivePolicyGuid) noexcept {
    (void)UserRootPowerKey;
    if (ActivePolicyGuid == nullptr || !mapped_guest_range(ActivePolicyGuid, sizeof(void*), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87; // ERROR_INVALID_PARAMETER
    }
    // Aloca GUID dummy via CoTaskMemAlloc (malloc)
    void* guid = ::malloc(16);
    if (guid == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 8;
    }
    std::memset(guid, 0, 16);
    // GUID dummy: 381b4222-f694-41f0-9685-ff5bb260df2e (Balanced)
    static const unsigned char kBalanced[16] = {0x22,0x42,0x1b,0x38,0x94,0xf6,0xf0,0x41,0x96,0x85,0xff,0x5b,0xb2,0x60,0xdf,0x2e};
    std::memcpy(guid, kBalanced, 16);
    *ActivePolicyGuid = guid;
    set_last_error(abi::kErrorSuccess);
    return 0; // ERROR_SUCCESS
}

TL_MSABI std::uint32_t tl_PowerSetActiveScheme(void* UserRootPowerKey, const void* SchemeGuid) noexcept {
    (void)UserRootPowerKey;
    if (SchemeGuid != nullptr && !mapped_guest_range(SchemeGuid, 16, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI std::uint32_t tl_CallNtPowerInformation(int InformationLevel, void* InputBuffer,
                                                 std::uint32_t InputBufferLength, void* OutputBuffer,
                                                 std::uint32_t OutputBufferLength) noexcept {
    (void)InformationLevel;
    if (InputBuffer != nullptr && InputBufferLength > 0 && !mapped_guest_range(InputBuffer, InputBufferLength, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    if (OutputBuffer != nullptr && OutputBufferLength > 0 && !mapped_guest_range(OutputBuffer, OutputBufferLength, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    if (OutputBuffer != nullptr && OutputBufferLength > 0) {
        std::memset(OutputBuffer, 0, OutputBufferLength);
    }
    set_last_error(abi::kErrorSuccess);
    return 0; // STATUS_SUCCESS
}

} // extern "C"

} // namespace tradutorlinux
