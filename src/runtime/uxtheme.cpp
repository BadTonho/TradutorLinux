#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "runtime_context.hpp"

#include <string>

namespace tradutorlinux {

extern "C" {

TL_MSABI int tl_SetWindowTheme(void* hwnd, const std::uint16_t* subAppName, const std::uint16_t* subIdList) noexcept {
    if (hwnd != nullptr && find_window_slot(hwnd) == nullptr) {
        // Permitir hwnd
    }
    if (subAppName != nullptr && !mapped_guest_wstring(subAppName)) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80070057); // E_INVALIDARG
    }
    if (subIdList != nullptr && !mapped_guest_wstring(subIdList)) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80070057);
    }
    set_last_error(abi::kErrorSuccess);
    return 0; // S_OK
}

TL_MSABI void* tl_OpenThemeData(void* const hwnd, const std::uint16_t* const class_list) noexcept {
    (void)hwnd;
    (void)class_list;
    return reinterpret_cast<void*>(0x5448454DULL); // 'THEM'
}

TL_MSABI std::int32_t tl_CloseThemeData(void* const theme) noexcept {
    (void)theme;
    return 0; // S_OK
}

TL_MSABI std::int32_t tl_DrawThemeBackground(void* const theme, void* const hdc, const int part_id,
                                             const int state_id, const void* const rect, const void* const clip_rect) noexcept {
    (void)theme;
    (void)hdc;
    (void)part_id;
    (void)state_id;
    (void)rect;
    (void)clip_rect;
    return 0; // S_OK
}

TL_MSABI std::int32_t tl_DrawThemeText(void* const theme, void* const hdc, const int part_id, const int state_id,
                                      const std::uint16_t* const text, const int char_count,
                                      const std::uint32_t text_flags, const std::uint32_t text_flags2,
                                      const void* const rect) noexcept {
    (void)theme;
    (void)hdc;
    (void)part_id;
    (void)state_id;
    (void)text;
    (void)char_count;
    (void)text_flags;
    (void)text_flags2;
    (void)rect;
    return 0; // S_OK
}

TL_MSABI std::int32_t tl_DrawThemeTextEx(void* const theme, void* const hdc, const int part_id, const int state_id,
                                        const std::uint16_t* const text, const int char_count,
                                        const std::uint32_t text_flags, void* const rect, const void* const options) noexcept {
    (void)theme;
    (void)hdc;
    (void)part_id;
    (void)state_id;
    (void)text;
    (void)char_count;
    (void)text_flags;
    (void)rect;
    (void)options;
    return 0; // S_OK
}

TL_MSABI std::int32_t tl_GetThemeColor(void* const theme, const int part_id, const int state_id,
                                       const int prop_id, std::uint32_t* const color) noexcept {
    (void)theme;
    (void)part_id;
    (void)state_id;
    (void)prop_id;
    if (color != nullptr && mapped_guest_range(color, sizeof(std::uint32_t), true)) {
        *color = 0x00FFFFFF; // White / default
    }
    return 0; // S_OK
}

TL_MSABI std::int32_t tl_GetThemeFont(void* const theme, void* const hdc, const int part_id,
                                      const int state_id, const int prop_id, void* const font) noexcept {
    (void)theme;
    (void)hdc;
    (void)part_id;
    (void)state_id;
    (void)prop_id;
    if (font != nullptr && mapped_guest_range(font, 92, true)) { // LOGFONTW size
        std::memset(font, 0, 92);
    }
    return 0; // S_OK
}

TL_MSABI std::int32_t tl_GetThemeMetric(void* const theme, void* const hdc, const int part_id,
                                        const int state_id, const int prop_id, int* const val) noexcept {
    (void)theme;
    (void)hdc;
    (void)part_id;
    (void)state_id;
    (void)prop_id;
    if (val != nullptr && mapped_guest_range(val, sizeof(int), true)) {
        *val = 0;
    }
    return 0; // S_OK
}

TL_MSABI std::int32_t tl_GetThemePartSize(void* const theme, void* const hdc, const int part_id,
                                          const int state_id, const void* const rect, const int type,
                                          void* const size) noexcept {
    (void)theme;
    (void)hdc;
    (void)part_id;
    (void)state_id;
    (void)rect;
    (void)type;
    if (size != nullptr && mapped_guest_range(size, 8, true)) { // SIZE struct
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(size) + 0) = 16;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(size) + 4) = 16;
    }
    return 0; // S_OK
}

TL_MSABI std::uint32_t tl_GetThemeSysColor(void* const theme, const int color_id) noexcept {
    (void)theme;
    (void)color_id;
    return 0x00FFFFFF;
}

TL_MSABI void* tl_GetThemeSysColorBrush(void* const theme, const int color_id) noexcept {
    (void)theme;
    (void)color_id;
    return reinterpret_cast<void*>(0x42525348ULL); // 'BRSH'
}

TL_MSABI int tl_IsThemeActive() noexcept {
    return 1;
}

TL_MSABI int tl_IsAppThemed() noexcept {
    return 1;
}

TL_MSABI int tl_IsThemeBackgroundPartiallyTransparent(void* const theme, const int part_id, const int state_id) noexcept {
    (void)theme;
    (void)part_id;
    (void)state_id;
    return 0;
}

TL_MSABI std::int32_t tl_BufferedPaintInit() noexcept {
    return 0; // S_OK
}

TL_MSABI std::int32_t tl_BufferedPaintUnInit() noexcept {
    return 0; // S_OK
}

TL_MSABI void* tl_BeginBufferedPaint(void* const hdc_target, const void* const target_rect, const int format,
                                     const void* const animation_params, void** const hdc_out) noexcept {
    (void)target_rect;
    (void)format;
    (void)animation_params;
    if (hdc_out != nullptr && mapped_guest_range(hdc_out, sizeof(void*), true)) {
        *hdc_out = hdc_target;
    }
    return reinterpret_cast<void*>(0x42504E54ULL); // 'BPNT'
}

TL_MSABI std::int32_t tl_EndBufferedPaint(void* const buffered_paint, const int update_target) noexcept {
    (void)buffered_paint;
    (void)update_target;
    return 0; // S_OK
}

TL_MSABI std::int32_t tl_DrawThemeParentBackground(void* const hwnd, void* const hdc, const void* const rect) noexcept {
    (void)hwnd;
    (void)hdc;
    (void)rect;
    return 0; // S_OK
}

TL_MSABI int tl_EndBufferedAnimation(void* const hbpAnimation, const int fUpdateTarget) noexcept {
    (void)hbpAnimation;
    (void)fUpdateTarget;
    return 0; // S_OK
}

TL_MSABI int tl_GetThemeTransitionDuration(void* const hTheme, const int iPartId, const int iStateIdFrom, const int iStateIdTo, const int iPropId, int* const pdwDuration) noexcept {
    (void)hTheme;
    (void)iPartId;
    (void)iStateIdFrom;
    (void)iStateIdTo;
    (void)iPropId;
    if (pdwDuration != nullptr && mapped_guest_range(pdwDuration, sizeof(int), true)) {
        *pdwDuration = 0;
    }
    return 0; // S_OK
}

TL_MSABI int tl_GetThemeBackgroundContentRect(void* const hTheme, void* const hdc, const int iPartId, const int iStateId, const void* const pBoundingRect, void* const pContentRect) noexcept {
    (void)hTheme;
    (void)hdc;
    (void)iPartId;
    (void)iStateId;
    if (pBoundingRect != nullptr && pContentRect != nullptr &&
        mapped_guest_range(pBoundingRect, 16, false) && mapped_guest_range(pContentRect, 16, true)) {
        std::memcpy(pContentRect, pBoundingRect, 16);
    }
    return 0; // S_OK
}

TL_MSABI int tl_EnableThemeDialogTexture(void* const hwnd, const std::uint32_t dwFlags) noexcept {
    (void)hwnd;
    (void)dwFlags;
    return 0; // S_OK
}

TL_MSABI void tl_BufferedPaintStopAllAnimations(void* const hwnd) noexcept {
    (void)hwnd;
}

TL_MSABI void* tl_BeginBufferedAnimation(void* const hwnd, void* const hdcTarget, const void* const rcTarget, const int dwFormat, void* const pPaintParams, void* const pAnimationParams, void** const phdcFrom, void** const phdcTo) noexcept {
    (void)hwnd;
    (void)rcTarget;
    (void)dwFormat;
    (void)pPaintParams;
    (void)pAnimationParams;
    if (phdcFrom != nullptr && mapped_guest_range(phdcFrom, sizeof(void*), true)) {
        *phdcFrom = hdcTarget;
    }
    if (phdcTo != nullptr && mapped_guest_range(phdcTo, sizeof(void*), true)) {
        *phdcTo = hdcTarget;
    }
    return reinterpret_cast<void*>(0x414E494DULL); // 'ANIM'
}

TL_MSABI int tl_BufferedPaintRenderAnimation(void* const hwnd, void* const hdcTarget) noexcept {
    (void)hwnd;
    (void)hdcTarget;
    return 1;
}

} // extern "C"
} // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_uxtheme_module() {
    static const ExportedFunction kUxThemeExports[] = {
        {"SetWindowTheme", 1, reinterpret_cast<std::uintptr_t>(&tl_SetWindowTheme)},
        {"OpenThemeData", 2, reinterpret_cast<std::uintptr_t>(&tl_OpenThemeData)},
        {"CloseThemeData", 3, reinterpret_cast<std::uintptr_t>(&tl_CloseThemeData)},
        {"DrawThemeBackground", 4, reinterpret_cast<std::uintptr_t>(&tl_DrawThemeBackground)},
        {"DrawThemeText", 5, reinterpret_cast<std::uintptr_t>(&tl_DrawThemeText)},
        {"DrawThemeTextEx", 6, reinterpret_cast<std::uintptr_t>(&tl_DrawThemeTextEx)},
        {"GetThemeColor", 7, reinterpret_cast<std::uintptr_t>(&tl_GetThemeColor)},
        {"GetThemeFont", 8, reinterpret_cast<std::uintptr_t>(&tl_GetThemeFont)},
        {"GetThemeMetric", 9, reinterpret_cast<std::uintptr_t>(&tl_GetThemeMetric)},
        {"GetThemePartSize", 10, reinterpret_cast<std::uintptr_t>(&tl_GetThemePartSize)},
        {"GetThemeSysColor", 11, reinterpret_cast<std::uintptr_t>(&tl_GetThemeSysColor)},
        {"GetThemeSysColorBrush", 12, reinterpret_cast<std::uintptr_t>(&tl_GetThemeSysColorBrush)},
        {"IsThemeActive", 13, reinterpret_cast<std::uintptr_t>(&tl_IsThemeActive)},
        {"IsAppThemed", 14, reinterpret_cast<std::uintptr_t>(&tl_IsAppThemed)},
        {"IsThemeBackgroundPartiallyTransparent", 15, reinterpret_cast<std::uintptr_t>(&tl_IsThemeBackgroundPartiallyTransparent)},
        {"BufferedPaintInit", 16, reinterpret_cast<std::uintptr_t>(&tl_BufferedPaintInit)},
        {"BufferedPaintUnInit", 17, reinterpret_cast<std::uintptr_t>(&tl_BufferedPaintUnInit)},
        {"BeginBufferedPaint", 18, reinterpret_cast<std::uintptr_t>(&tl_BeginBufferedPaint)},
        {"EndBufferedPaint", 19, reinterpret_cast<std::uintptr_t>(&tl_EndBufferedPaint)},
        {"DrawThemeParentBackground", 20, reinterpret_cast<std::uintptr_t>(&tl_DrawThemeParentBackground)},
        {"EndBufferedAnimation", 21, reinterpret_cast<std::uintptr_t>(&tl_EndBufferedAnimation)},
        {"GetThemeTransitionDuration", 22, reinterpret_cast<std::uintptr_t>(&tl_GetThemeTransitionDuration)},
        {"GetThemeBackgroundContentRect", 23, reinterpret_cast<std::uintptr_t>(&tl_GetThemeBackgroundContentRect)},
        {"EnableThemeDialogTexture", 24, reinterpret_cast<std::uintptr_t>(&tl_EnableThemeDialogTexture)},
        {"BufferedPaintStopAllAnimations", 25, reinterpret_cast<std::uintptr_t>(&tl_BufferedPaintStopAllAnimations)},
        {"BeginBufferedAnimation", 26, reinterpret_cast<std::uintptr_t>(&tl_BeginBufferedAnimation)},
        {"BufferedPaintRenderAnimation", 27, reinterpret_cast<std::uintptr_t>(&tl_BufferedPaintRenderAnimation)},
    };
    static const InternalModule kUxThemeModule{"UxTheme.dll", kUxThemeExports};
    register_module(kUxThemeModule);
}

} // namespace tradutorlinux::loader
