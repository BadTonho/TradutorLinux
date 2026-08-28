#include "tradutorlinux/runtime/winapi.hpp"
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

} // extern "C"

} // namespace tradutorlinux
