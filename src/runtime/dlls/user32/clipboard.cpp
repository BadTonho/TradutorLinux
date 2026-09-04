#include "user32_internal.hpp"
namespace tradutorlinux {
extern "C" {

TL_MSABI int tl_OpenClipboard(void* const hwnd_new_owner) noexcept {
    (void)hwnd_new_owner;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_CloseClipboard(void) noexcept {
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_SetClipboardData(const std::uint32_t format, void* const mem) noexcept {
    (void)format;
    set_last_error(abi::kErrorSuccess);
    return mem;
}

TL_MSABI int tl_EmptyClipboard(void) noexcept {
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_RegisterClipboardFormatW(const std::uint16_t* const format_name) noexcept {
    (void)format_name;
    set_last_error(abi::kErrorSuccess);
    return 0xC001; // Custom format ID
}

TL_MSABI void* tl_GetClipboardData(const std::uint32_t format) noexcept {
    (void)format;
    return nullptr;
}

TL_MSABI int tl_IsClipboardFormatAvailable(const std::uint32_t format) noexcept {
    (void)format;
    return 0;
}

TL_MSABI std::uint32_t tl_RegisterClipboardFormatA(const char* const format_name) noexcept {
    (void)format_name;
    set_last_error(abi::kErrorSuccess);
    return 0xC002;
}

TL_MSABI int tl_CountClipboardFormats() noexcept {
    return 0;
}

TL_MSABI std::uint32_t tl_EnumClipboardFormats(const std::uint32_t format) noexcept {
    (void)format;
    return 0;
}

TL_MSABI void* tl_GetClipboardOwner() noexcept {
    return nullptr;
}

TL_MSABI void* tl_SetClipboardViewer(void* const hWndNewViewer) noexcept {
    (void)hWndNewViewer;
    return nullptr;
}

TL_MSABI int tl_ChangeClipboardChain(void* const hWndRemove, void* const hWndNewNext) noexcept {
    (void)hWndRemove;
    (void)hWndNewNext;
    return 1;
}

}  // extern "C"
}  // namespace tradutorlinux
