#include "tradutorlinux/runtime/version.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"

#include <cstring>

namespace tradutorlinux {

namespace {

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

}  // namespace

extern "C" {

TL_VER_MSABI std::uint32_t tl_GetFileVersionInfoSizeA(const char* const filename, std::uint32_t* const handle) noexcept {
    (void)filename;
    if (handle != nullptr && mapped_range(handle, sizeof(std::uint32_t), true)) {
        *handle = 0;
    }
    return 512; // Standard dummy VS_VERSIONINFO size
}

TL_VER_MSABI std::uint32_t tl_GetFileVersionInfoSizeW(const std::uint16_t* const filename, std::uint32_t* const handle) noexcept {
    (void)filename;
    if (handle != nullptr && mapped_range(handle, sizeof(std::uint32_t), true)) {
        *handle = 0;
    }
    return 512;
}

TL_VER_MSABI int tl_GetFileVersionInfoA(const char* const filename, const std::uint32_t handle,
                                        const std::uint32_t len, void* const data) noexcept {
    (void)filename;
    (void)handle;
    if (data != nullptr && mapped_range(data, len, true)) {
        std::memset(data, 0, len);
    }
    return 1;
}

TL_VER_MSABI int tl_GetFileVersionInfoW(const std::uint16_t* const filename, const std::uint32_t handle,
                                        const std::uint32_t len, void* const data) noexcept {
    (void)filename;
    (void)handle;
    if (data != nullptr && mapped_range(data, len, true)) {
        std::memset(data, 0, len);
    }
    return 1;
}

TL_VER_MSABI int tl_VerQueryValueA(const void* const block, const char* const sub_block,
                                   void** const buffer, std::uint32_t* const len) noexcept {
    (void)block;
    (void)sub_block;
    static const char kDummyVersion[] = "1.0.0.0";
    if (buffer != nullptr && mapped_range(buffer, sizeof(void*), true)) {
        *buffer = const_cast<char*>(kDummyVersion);
    }
    if (len != nullptr && mapped_range(len, sizeof(std::uint32_t), true)) {
        *len = sizeof(kDummyVersion);
    }
    return 1;
}

TL_VER_MSABI int tl_VerQueryValueW(const void* const block, const std::uint16_t* const sub_block,
                                   void** const buffer, std::uint32_t* const len) noexcept {
    (void)block;
    (void)sub_block;
    static const std::uint16_t kDummyVersionW[] = {'1', '.', '0', '.', '0', '.', '0', 0};
    if (buffer != nullptr && mapped_range(buffer, sizeof(void*), true)) {
        *buffer = const_cast<std::uint16_t*>(kDummyVersionW);
    }
    if (len != nullptr && mapped_range(len, sizeof(std::uint32_t), true)) {
        *len = sizeof(kDummyVersionW);
    }
    return 1;
}

TL_VER_MSABI std::uint32_t tl_GetFileVersionInfoSizeExA(const std::uint32_t flags, const char* const filename,
                                                        std::uint32_t* const handle) noexcept {
    (void)flags;
    return tl_GetFileVersionInfoSizeA(filename, handle);
}

TL_VER_MSABI std::uint32_t tl_GetFileVersionInfoSizeExW(const std::uint32_t flags, const std::uint16_t* const filename,
                                                        std::uint32_t* const handle) noexcept {
    (void)flags;
    return tl_GetFileVersionInfoSizeW(filename, handle);
}

TL_VER_MSABI int tl_GetFileVersionInfoExA(const std::uint32_t flags, const char* const filename,
                                          const std::uint32_t handle, const std::uint32_t len,
                                          void* const data) noexcept {
    (void)flags;
    return tl_GetFileVersionInfoA(filename, handle, len, data);
}

TL_VER_MSABI int tl_GetFileVersionInfoExW(const std::uint32_t flags, const std::uint16_t* const filename,
                                          const std::uint32_t handle, const std::uint32_t len,
                                          void* const data) noexcept {
    (void)flags;
    return tl_GetFileVersionInfoW(filename, handle, len, data);
}

}  // extern "C"
}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_version_module() {
    static const ExportedFunction kVersionExports[] = {
        {"GetFileVersionInfoSizeA", 1, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoSizeA)},
        {"GetFileVersionInfoSizeW", 2, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoSizeW)},
        {"GetFileVersionInfoA", 3, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoA)},
        {"GetFileVersionInfoW", 4, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoW)},
        {"VerQueryValueA", 5, reinterpret_cast<std::uintptr_t>(&tl_VerQueryValueA)},
        {"VerQueryValueW", 6, reinterpret_cast<std::uintptr_t>(&tl_VerQueryValueW)},
        {"GetFileVersionInfoSizeExA", 7, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoSizeExA)},
        {"GetFileVersionInfoSizeExW", 8, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoSizeExW)},
        {"GetFileVersionInfoExA", 9, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoExA)},
        {"GetFileVersionInfoExW", 10, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoExW)},
    };
    static const InternalModule kVersionModule{"version.dll", kVersionExports};
    register_module(kVersionModule);
}

}  // namespace tradutorlinux::loader
