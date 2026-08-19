#pragma once

#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_COMDLG_MSABI __attribute__((ms_abi))
#else
#error "TL_COMDLG_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

extern "C" {

TL_COMDLG_MSABI int tl_GetOpenFileNameA(void* open_filename) noexcept;
TL_COMDLG_MSABI int tl_GetOpenFileNameW(void* open_filename) noexcept;
TL_COMDLG_MSABI int tl_GetSaveFileNameA(void* open_filename) noexcept;
TL_COMDLG_MSABI int tl_GetSaveFileNameW(void* open_filename) noexcept;
TL_COMDLG_MSABI int tl_ChooseColorA(void* choose_color) noexcept;
TL_COMDLG_MSABI int tl_ChooseColorW(void* choose_color) noexcept;

}  // extern "C"

}  // namespace tradutorlinux
