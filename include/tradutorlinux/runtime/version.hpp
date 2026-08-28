#pragma once

#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_VER_MSABI __attribute__((ms_abi))
#else
#error "TL_VER_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

extern "C" {

TL_VER_MSABI std::uint32_t tl_GetFileVersionInfoSizeA(const char* filename, std::uint32_t* handle) noexcept;
TL_VER_MSABI std::uint32_t tl_GetFileVersionInfoSizeW(const std::uint16_t* filename, std::uint32_t* handle) noexcept;
TL_VER_MSABI int tl_GetFileVersionInfoA(const char* filename, std::uint32_t handle, std::uint32_t len, void* data) noexcept;
TL_VER_MSABI int tl_GetFileVersionInfoW(const std::uint16_t* filename, std::uint32_t handle, std::uint32_t len, void* data) noexcept;
TL_VER_MSABI int tl_VerQueryValueA(const void* block, const char* sub_block, void** buffer, std::uint32_t* len) noexcept;
TL_VER_MSABI int tl_VerQueryValueW(const void* block, const std::uint16_t* sub_block, void** buffer, std::uint32_t* len) noexcept;
TL_VER_MSABI std::uint32_t tl_GetFileVersionInfoSizeExA(std::uint32_t flags, const char* filename, std::uint32_t* handle) noexcept;
TL_VER_MSABI std::uint32_t tl_GetFileVersionInfoSizeExW(std::uint32_t flags, const std::uint16_t* filename, std::uint32_t* handle) noexcept;
TL_VER_MSABI int tl_GetFileVersionInfoExA(std::uint32_t flags, const char* filename, std::uint32_t handle, std::uint32_t len, void* data) noexcept;
TL_VER_MSABI int tl_GetFileVersionInfoExW(std::uint32_t flags, const std::uint16_t* filename, std::uint32_t handle, std::uint32_t len, void* data) noexcept;

}  // extern "C"

}  // namespace tradutorlinux
