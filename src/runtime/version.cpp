#include "tradutorlinux/runtime/version.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"
#include "runtime_context.hpp"

namespace tradutorlinux {

namespace {

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

std::uint32_t reject_version_size(std::uint32_t* const handle) noexcept {
    if (handle != nullptr) {
        if (!mapped_range(handle, sizeof(std::uint32_t), true)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        *handle = 0;
    }
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

int reject_version_info(const std::uint32_t len, void* const data) noexcept {
    if (data == nullptr || len == 0 || !mapped_range(data, len, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

int reject_version_query(void** const buffer, std::uint32_t* const len) noexcept {
    if (buffer == nullptr || len == nullptr ||
        !mapped_range(buffer, sizeof(void*), true) ||
        !mapped_range(len, sizeof(std::uint32_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    *buffer = nullptr;
    *len = 0;
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

}  // namespace

extern "C" {

TL_VER_MSABI std::uint32_t tl_GetFileVersionInfoSizeA(const char* const filename, std::uint32_t* const handle) noexcept {
    (void)filename;
    return reject_version_size(handle);
}

TL_VER_MSABI std::uint32_t tl_GetFileVersionInfoSizeW(const std::uint16_t* const filename, std::uint32_t* const handle) noexcept {
    (void)filename;
    return reject_version_size(handle);
}

TL_VER_MSABI int tl_GetFileVersionInfoA(const char* const filename, const std::uint32_t handle,
                                        const std::uint32_t len, void* const data) noexcept {
    (void)filename;
    (void)handle;
    return reject_version_info(len, data);
}

TL_VER_MSABI int tl_GetFileVersionInfoW(const std::uint16_t* const filename, const std::uint32_t handle,
                                        const std::uint32_t len, void* const data) noexcept {
    (void)filename;
    (void)handle;
    return reject_version_info(len, data);
}

TL_VER_MSABI int tl_VerQueryValueA(const void* const block, const char* const sub_block,
                                   void** const buffer, std::uint32_t* const len) noexcept {
    (void)block;
    (void)sub_block;
    return reject_version_query(buffer, len);
}

TL_VER_MSABI int tl_VerQueryValueW(const void* const block, const std::uint16_t* const sub_block,
                                   void** const buffer, std::uint32_t* const len) noexcept {
    (void)block;
    (void)sub_block;
    return reject_version_query(buffer, len);
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
        {"GetFileVersionInfoSizeA", 1, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoSizeA), ExportSupport::Stub},
        {"GetFileVersionInfoSizeW", 2, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoSizeW), ExportSupport::Stub},
        {"GetFileVersionInfoA", 3, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoA), ExportSupport::Stub},
        {"GetFileVersionInfoW", 4, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoW), ExportSupport::Stub},
        {"VerQueryValueA", 5, reinterpret_cast<std::uintptr_t>(&tl_VerQueryValueA), ExportSupport::Stub},
        {"VerQueryValueW", 6, reinterpret_cast<std::uintptr_t>(&tl_VerQueryValueW), ExportSupport::Stub},
        {"GetFileVersionInfoSizeExA", 7, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoSizeExA), ExportSupport::Stub},
        {"GetFileVersionInfoSizeExW", 8, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoSizeExW), ExportSupport::Stub},
        {"GetFileVersionInfoExA", 9, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoExA), ExportSupport::Stub},
        {"GetFileVersionInfoExW", 10, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoExW), ExportSupport::Stub},
    };
    static const InternalModule kVersionModule{"version.dll", kVersionExports};
    register_module(kVersionModule);
}

}  // namespace tradutorlinux::loader
