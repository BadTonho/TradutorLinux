#pragma once

#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_SHLWAPI_MSABI __attribute__((ms_abi))
#else
#error "TL_SHLWAPI_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

extern "C" {

TL_SHLWAPI_MSABI int tl_PathFileExistsA(const char* path) noexcept;
TL_SHLWAPI_MSABI int tl_PathFileExistsW(const std::uint16_t* path) noexcept;
TL_SHLWAPI_MSABI int tl_PathIsDirectoryA(const char* path) noexcept;
TL_SHLWAPI_MSABI int tl_PathIsDirectoryW(const std::uint16_t* path) noexcept;
TL_SHLWAPI_MSABI char* tl_PathCombineA(char* dest, const char* dir, const char* file) noexcept;
TL_SHLWAPI_MSABI std::uint16_t* tl_PathCombineW(std::uint16_t* dest, const std::uint16_t* dir, const std::uint16_t* file) noexcept;
TL_SHLWAPI_MSABI char* tl_PathFindFileNameA(const char* path) noexcept;
TL_SHLWAPI_MSABI std::uint16_t* tl_PathFindFileNameW(const std::uint16_t* path) noexcept;
TL_SHLWAPI_MSABI char* tl_PathFindExtensionA(const char* path) noexcept;
TL_SHLWAPI_MSABI std::uint16_t* tl_PathFindExtensionW(const std::uint16_t* path) noexcept;
TL_SHLWAPI_MSABI int tl_PathRemoveFileSpecA(char* path) noexcept;
TL_SHLWAPI_MSABI int tl_PathRemoveFileSpecW(std::uint16_t* path) noexcept;
TL_SHLWAPI_MSABI char* tl_PathAddBackslashA(char* path) noexcept;
TL_SHLWAPI_MSABI std::uint16_t* tl_PathAddBackslashW(std::uint16_t* path) noexcept;
TL_SHLWAPI_MSABI char* tl_PathRemoveBackslashA(char* path) noexcept;
TL_SHLWAPI_MSABI std::uint16_t* tl_PathRemoveBackslashW(std::uint16_t* path) noexcept;

TL_SHLWAPI_MSABI char* tl_StrStrIA(const char* first, const char* srch) noexcept;
TL_SHLWAPI_MSABI std::uint16_t* tl_StrStrIW(const std::uint16_t* first, const std::uint16_t* srch) noexcept;
TL_SHLWAPI_MSABI int tl_StrCmpIA(const char* string1, const char* string2) noexcept;
TL_SHLWAPI_MSABI int tl_StrCmpIW(const std::uint16_t* string1, const std::uint16_t* string2) noexcept;

}  // extern "C"

}  // namespace tradutorlinux
