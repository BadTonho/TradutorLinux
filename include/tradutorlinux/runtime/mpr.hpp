#pragma once

#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_MPR_MSABI __attribute__((ms_abi))
#else
#error "TL_MPR_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

extern "C" {

// MPR.dll: WNet Network Provider APIs
TL_MPR_MSABI std::uint32_t tl_WNetAddConnection2W(const void* net_resource, const std::uint16_t* password,
                                                  const std::uint16_t* user_name, std::uint32_t flags) noexcept;
TL_MPR_MSABI std::uint32_t tl_WNetOpenEnumW(std::uint32_t scope, std::uint32_t type, std::uint32_t usage,
                                            const void* net_resource, void** enum_handle) noexcept;
TL_MPR_MSABI std::uint32_t tl_WNetEnumResourceW(void* enum_handle, std::uint32_t* count, void* buffer,
                                                std::uint32_t* buffer_size) noexcept;
TL_MPR_MSABI std::uint32_t tl_WNetCloseEnum(void* enum_handle) noexcept;
TL_MPR_MSABI std::uint32_t tl_WNetGetResourceInformationW(const void* net_resource, void* buffer,
                                                          std::uint32_t* buffer_size, std::uint16_t** system) noexcept;
TL_MPR_MSABI std::uint32_t tl_WNetGetResourceParentW(const void* net_resource, void* buffer,
                                                     std::uint32_t* buffer_size) noexcept;

}  // extern "C"

}  // namespace tradutorlinux
