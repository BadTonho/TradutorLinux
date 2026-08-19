#pragma once

#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_COMCTL_MSABI __attribute__((ms_abi))
#else
#error "TL_COMCTL_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

extern "C" {

TL_COMCTL_MSABI void tl_InitCommonControls() noexcept;
TL_COMCTL_MSABI int tl_InitCommonControlsEx(const void* init_controls) noexcept;
TL_COMCTL_MSABI void* tl_ImageList_Create(int cx, int cy, std::uint32_t flags, int initial, int grow) noexcept;
TL_COMCTL_MSABI int tl_ImageList_Destroy(void* image_list) noexcept;
TL_COMCTL_MSABI int tl_ImageList_Add(void* image_list, void* image, void* mask) noexcept;
TL_COMCTL_MSABI int tl_ImageList_AddMasked(void* image_list, void* bitmap, std::uint32_t mask_color) noexcept;
TL_COMCTL_MSABI int tl_ImageList_ReplaceIcon(void* image_list, int index, void* icon) noexcept;

}  // extern "C"

}  // namespace tradutorlinux
