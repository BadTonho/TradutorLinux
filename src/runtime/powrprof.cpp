#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "core/runtime_state_common.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace tradutorlinux {

namespace {

constexpr std::size_t kPowerBufferChunkSize = 4096;

[[nodiscard]] bool read_power_buffer(const void* const buffer, const std::size_t size) noexcept {
    if (buffer == nullptr) {
        return size == 0;
    }

    std::array<std::byte, kPowerBufferChunkSize> scratch{};
    const auto* const source = static_cast<const std::byte*>(buffer);
    for (std::size_t offset = 0; offset < size;) {
        const std::size_t chunk = std::min(kPowerBufferChunkSize, size - offset);
        if (runtime::read_guest_memory(source + offset, scratch.data(), chunk).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

[[nodiscard]] bool zero_power_buffer(void* const buffer, const std::size_t size) noexcept {
    std::array<std::byte, kPowerBufferChunkSize> zeroes{};
    auto* const destination = static_cast<std::byte*>(buffer);
    for (std::size_t offset = 0; offset < size;) {
        const std::size_t chunk = std::min(kPowerBufferChunkSize, size - offset);
        if (runtime::write_guest_memory(destination + offset, zeroes.data(), chunk).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

} // namespace

extern "C" {

TL_MSABI std::uint32_t tl_PowerGetActiveScheme(void* UserRootPowerKey, void** ActivePolicyGuid) noexcept {
    (void)UserRootPowerKey;
    if (ActivePolicyGuid == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87; // ERROR_INVALID_PARAMETER
    }
    // O contrato de PowerGetActiveScheme exige que o GUID seja liberável por
    // LocalFree; use o mesmo alocador rastreado pela implementação de KERNEL32.
    void* guid = tl_LocalAlloc(abi::kGmemZeroinit, 16);
    if (guid == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 8;
    }
    // GUID dummy: 381b4222-f694-41f0-9685-ff5bb260df2e (Balanced)
    static const unsigned char kBalanced[16] = {0x22,0x42,0x1b,0x38,0x94,0xf6,0xf0,0x41,0x96,0x85,0xff,0x5b,0xb2,0x60,0xdf,0x2e};
    std::memcpy(guid, kBalanced, 16);
    if (!write_guest_value(ActivePolicyGuid, guid)) {
        static_cast<void>(tl_LocalFree(guid));
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    set_last_error(abi::kErrorSuccess);
    return 0; // ERROR_SUCCESS
}

TL_MSABI std::uint32_t tl_PowerSetActiveScheme(void* UserRootPowerKey, const void* SchemeGuid) noexcept {
    (void)UserRootPowerKey;
    if (SchemeGuid != nullptr && !read_power_buffer(SchemeGuid, 16)) {
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
    if (InputBuffer != nullptr && InputBufferLength > 0 &&
        !read_power_buffer(InputBuffer, InputBufferLength)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    if (OutputBuffer != nullptr && OutputBufferLength > 0) {
        if (!zero_power_buffer(OutputBuffer, OutputBufferLength)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 87;
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 0; // STATUS_SUCCESS
}

} // extern "C"
} // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_powrprof_module() {
    static const ExportedFunction kPowrProfExports[] = {
        {"PowerGetActiveScheme", 1, reinterpret_cast<std::uintptr_t>(&tl_PowerGetActiveScheme), ExportSupport::Full},
        {"PowerSetActiveScheme", 2, reinterpret_cast<std::uintptr_t>(&tl_PowerSetActiveScheme), ExportSupport::Full},
        {"CallNtPowerInformation", 3, reinterpret_cast<std::uintptr_t>(&tl_CallNtPowerInformation), ExportSupport::Full},
    };
    static const InternalModule kPowrProfModule{"POWRPROF.dll", kPowrProfExports};
    register_module(kPowrProfModule);
}

} // namespace tradutorlinux::loader
