#include "tradutorlinux/runtime/mpr.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"

namespace tradutorlinux {

namespace {

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

}  // namespace

extern "C" {

TL_MPR_MSABI std::uint32_t tl_WNetAddConnection2W(const void* const net_resource,
                                                  const std::uint16_t* const password,
                                                  const std::uint16_t* const user_name,
                                                  const std::uint32_t flags) noexcept {
    (void)net_resource;
    (void)password;
    (void)user_name;
    (void)flags;
    tl_SetLastError(abi::kErrorSuccess);
    return 0; // NO_ERROR
}

TL_MPR_MSABI std::uint32_t tl_WNetOpenEnumW(const std::uint32_t scope, const std::uint32_t type,
                                            const std::uint32_t usage, const void* const net_resource,
                                            void** const enum_handle) noexcept {
    (void)scope;
    (void)type;
    (void)usage;
    (void)net_resource;
    if (enum_handle != nullptr && mapped_range(enum_handle, sizeof(void*), true)) {
        *enum_handle = reinterpret_cast<void*>(0x574E6574ULL); // 'WNet'
    }
    tl_SetLastError(abi::kErrorSuccess);
    return 0; // NO_ERROR
}

TL_MPR_MSABI std::uint32_t tl_WNetEnumResourceW(void* const enum_handle, std::uint32_t* const count,
                                                void* const buffer, std::uint32_t* const buffer_size) noexcept {
    (void)enum_handle;
    (void)buffer;
    (void)buffer_size;
    if (count != nullptr && mapped_range(count, sizeof(std::uint32_t), true)) {
        *count = 0;
    }
    tl_SetLastError(abi::kErrorSuccess);
    return 259; // ERROR_NO_MORE_ITEMS
}

TL_MPR_MSABI std::uint32_t tl_WNetCloseEnum(void* const enum_handle) noexcept {
    (void)enum_handle;
    tl_SetLastError(abi::kErrorSuccess);
    return 0; // NO_ERROR
}

TL_MPR_MSABI std::uint32_t tl_WNetGetResourceInformationW(const void* const net_resource, void* const buffer,
                                                          std::uint32_t* const buffer_size,
                                                          std::uint16_t** const system) noexcept {
    (void)net_resource;
    (void)buffer;
    (void)buffer_size;
    (void)system;
    tl_SetLastError(abi::kErrorSuccess);
    return 0; // NO_ERROR
}

TL_MPR_MSABI std::uint32_t tl_WNetGetResourceParentW(const void* const net_resource, void* const buffer,
                                                     std::uint32_t* const buffer_size) noexcept {
    (void)net_resource;
    (void)buffer;
    (void)buffer_size;
    tl_SetLastError(abi::kErrorSuccess);
    return 0; // NO_ERROR
}

}  // extern "C"
}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_mpr_module() {
    static const ExportedFunction kMprExports[] = {
        {"WNetAddConnection2W", 1, reinterpret_cast<std::uintptr_t>(&tl_WNetAddConnection2W)},
        {"WNetOpenEnumW", 2, reinterpret_cast<std::uintptr_t>(&tl_WNetOpenEnumW)},
        {"WNetEnumResourceW", 3, reinterpret_cast<std::uintptr_t>(&tl_WNetEnumResourceW)},
        {"WNetCloseEnum", 4, reinterpret_cast<std::uintptr_t>(&tl_WNetCloseEnum)},
        {"WNetGetResourceInformationW", 5,
         reinterpret_cast<std::uintptr_t>(&tl_WNetGetResourceInformationW)},
        {"WNetGetResourceParentW", 6,
         reinterpret_cast<std::uintptr_t>(&tl_WNetGetResourceParentW)},
    };
    static const InternalModule kMprModule{"MPR.dll", kMprExports};
    register_module(kMprModule);
}

}  // namespace tradutorlinux::loader

