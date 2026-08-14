#pragma once

#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_MSABI __attribute__((ms_abi))
#else
#error "TL_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {
namespace abi {

// Tipos mínimos do Win32 usados pelas APIs suportadas.
using Handle = void*;
using Bool = int;
using Dword = std::uint32_t;
using Uint = std::uint32_t;

constexpr Dword kStdInputHandle = 0xFFFFFFF6U;   // STD_INPUT_HANDLE (-10)
constexpr Dword kStdOutputHandle = 0xFFFFFFF5U;  // STD_OUTPUT_HANDLE (-11)
constexpr Dword kStdErrorHandle = 0xFFFFFFF4U;   // STD_ERROR_HANDLE (-12)

}  // namespace abi

// Fronteira de ABI: funções hospedeiras chamadas por código PE32+ x86-64.
// Todas usam a convenção Microsoft x64 (TL_MSABI) e não propagam exceções
// C++. Na Fase 3 são stubs placeholders com comportamento seguro e
// documentado; a semântica real é implementada na Fase 4.
extern "C" {

TL_MSABI void* tl_GetStdHandle(std::uint32_t n_std_handle) noexcept;
TL_MSABI int tl_WriteFile(void* file, const void* buffer, std::uint32_t bytes_to_write,
                          std::uint32_t* bytes_written, void* overlapped) noexcept;
TL_MSABI void tl_ExitProcess(std::uint32_t exit_code) noexcept;

}  // extern "C"
}  // namespace tradutorlinux
