#pragma once

#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_IMM_MSABI __attribute__((ms_abi))
#else
#error "TL_IMM_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

extern "C" {

TL_IMM_MSABI void* tl_ImmGetContext(const void* window) noexcept;
TL_IMM_MSABI int tl_ImmReleaseContext(const void* window, void* context) noexcept;
TL_IMM_MSABI int tl_ImmSetCompositionWindow(void* context, const void* comp_form) noexcept;
TL_IMM_MSABI std::int32_t tl_ImmGetCompositionStringA(void* context, std::uint32_t index, void* buf, std::uint32_t buflen) noexcept;
TL_IMM_MSABI std::int32_t tl_ImmGetCompositionStringW(void* context, std::uint32_t index, void* buf, std::uint32_t buflen) noexcept;
TL_IMM_MSABI void* tl_ImmAssociateContext(const void* window, void* context) noexcept;

}  // extern "C"

}  // namespace tradutorlinux
