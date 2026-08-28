#pragma once

#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_DWM_MSABI __attribute__((ms_abi))
#else
#error "TL_DWM_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

extern "C" {

TL_DWM_MSABI std::int32_t tl_DwmSetWindowAttribute(void* hwnd, std::uint32_t attr, const void* attr_val, std::uint32_t attr_sz) noexcept;
TL_DWM_MSABI std::int32_t tl_DwmGetWindowAttribute(void* hwnd, std::uint32_t attr, void* attr_val, std::uint32_t attr_sz) noexcept;
TL_DWM_MSABI std::int32_t tl_DwmIsCompositionEnabled(int* enabled) noexcept;
TL_DWM_MSABI int tl_DwmDefWindowProc(void* hwnd, std::uint32_t msg, std::uintptr_t wparam, std::intptr_t lparam, std::intptr_t* lresult) noexcept;
TL_DWM_MSABI std::int32_t tl_DwmExtendFrameIntoClientArea(void* hwnd, const void* margins) noexcept;
TL_DWM_MSABI std::int32_t tl_DwmEnableBlurBehindWindow(void* hwnd, const void* blur_behind) noexcept;
TL_DWM_MSABI std::int32_t tl_DwmFlush() noexcept;
TL_DWM_MSABI std::int32_t tl_DwmGetColorizationColor(std::uint32_t* color, int* opaque) noexcept;

}  // extern "C"

}  // namespace tradutorlinux

