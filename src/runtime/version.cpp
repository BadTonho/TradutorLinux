#include "tradutorlinux/runtime/version.hpp"

#include <algorithm>
#include <cstring>
#include <string>

#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/util/unicode.hpp"

namespace tradutorlinux {

namespace {

struct GuestVsFixedFileInfo {
    std::uint32_t dwSignature{0xFEEF04BDU};
    std::uint32_t dwStrucVersion{0x00010000U};
    std::uint32_t dwFileVersionMS{0x00010000U};
    std::uint32_t dwFileVersionLS{0x00000000U};
    std::uint32_t dwProductVersionMS{0x00010000U};
    std::uint32_t dwProductVersionLS{0x00000000U};
    std::uint32_t dwFileFlagsMask{0x0000003FU};
    std::uint32_t dwFileFlags{0};
    std::uint32_t dwFileOS{0x00040004U}; // VOS_NT_WINDOWS32
    std::uint32_t dwFileType{0x00000001U}; // VFT_APP
    std::uint32_t dwFileSubtype{0};
    std::uint32_t dwFileDateMS{0};
    std::uint32_t dwFileDateLS{0};
};

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

inline bool mapped_cstring(const char* value) noexcept {
    return runtime::validate_mapped_cstring(value);
}

inline bool mapped_wstring(const std::uint16_t* value) noexcept {
    return runtime::validate_mapped_wstring(value);
}

}  // namespace

extern "C" {

TL_VERSION_MSABI std::uint32_t tl_GetFileVersionInfoSizeA(const char* filename, std::uint32_t* handle) noexcept {
    if (filename == nullptr || !mapped_cstring(filename)) {
        return 0;
    }
    if (handle != nullptr && mapped_range(handle, sizeof(std::uint32_t), true)) {
        *handle = 0;
    }
    return 512;
}

TL_VERSION_MSABI std::uint32_t tl_GetFileVersionInfoSizeW(const std::uint16_t* filename, std::uint32_t* handle) noexcept {
    if (filename == nullptr || !mapped_wstring(filename)) {
        return 0;
    }
    if (handle != nullptr && mapped_range(handle, sizeof(std::uint32_t), true)) {
        *handle = 0;
    }
    return 512;
}

TL_VERSION_MSABI int tl_GetFileVersionInfoA(const char* filename, const std::uint32_t handle,
                                            const std::uint32_t len, void* data) noexcept {
    (void)handle;
    if (filename == nullptr || data == nullptr || len < sizeof(GuestVsFixedFileInfo) ||
        !mapped_range(data, len, true)) {
        return 0;
    }
    std::memset(data, 0, len);
    GuestVsFixedFileInfo info{};
    std::memcpy(data, &info, sizeof(info));
    return 1;
}

TL_VERSION_MSABI int tl_GetFileVersionInfoW(const std::uint16_t* filename, const std::uint32_t handle,
                                            const std::uint32_t len, void* data) noexcept {
    (void)handle;
    if (filename == nullptr || data == nullptr || len < sizeof(GuestVsFixedFileInfo) ||
        !mapped_range(data, len, true)) {
        return 0;
    }
    std::memset(data, 0, len);
    GuestVsFixedFileInfo info{};
    std::memcpy(data, &info, sizeof(info));
    return 1;
}

TL_VERSION_MSABI int tl_VerQueryValueA(const void* block, const char* sub_block, void** buffer, std::uint32_t* len) noexcept {
    if (block == nullptr || sub_block == nullptr || buffer == nullptr || len == nullptr ||
        !mapped_cstring(sub_block) || !mapped_range(buffer, sizeof(void*), true) ||
        !mapped_range(len, sizeof(std::uint32_t), true)) {
        return 0;
    }
    *buffer = const_cast<void*>(block);
    *len = sizeof(GuestVsFixedFileInfo);
    return 1;
}

TL_VERSION_MSABI int tl_VerQueryValueW(const void* block, const std::uint16_t* sub_block, void** buffer, std::uint32_t* len) noexcept {
    if (block == nullptr || sub_block == nullptr || buffer == nullptr || len == nullptr ||
        !mapped_wstring(sub_block) || !mapped_range(buffer, sizeof(void*), true) ||
        !mapped_range(len, sizeof(std::uint32_t), true)) {
        return 0;
    }
    *buffer = const_cast<void*>(block);
    *len = sizeof(GuestVsFixedFileInfo);
    return 1;
}

}  // extern "C"

}  // namespace tradutorlinux
