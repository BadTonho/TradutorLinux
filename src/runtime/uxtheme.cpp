#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "core/runtime_state_common.hpp"
#include "core/runtime_gui_state.hpp"

#include <array>
#include <cstring>

namespace tradutorlinux {

namespace {

constexpr std::int32_t kThemeENotImpl = static_cast<std::int32_t>(0x80004001U);
constexpr std::int32_t kThemeEInvalidArg = static_cast<std::int32_t>(0x80070057U);

std::int32_t reject_theme() noexcept {
    set_last_error(abi::kErrorNotSupported);
    return kThemeENotImpl;
}

void* reject_theme_handle() noexcept {
    set_last_error(abi::kErrorNotSupported);
    return nullptr;
}

bool clear_guest_buffer(void* const destination, const std::size_t size) noexcept {
    std::array<std::byte, 92> zeroes{};
    return runtime::write_guest_memory(destination, zeroes.data(), size).status ==
           runtime::GuestMemoryAccessStatus::Success;
}

}  // namespace

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
    return reject_theme();
}

TL_MSABI void* tl_OpenThemeData(void* const hwnd, const std::uint16_t* const class_list) noexcept {
    (void)hwnd;
    if (class_list != nullptr && !mapped_guest_wstring(class_list)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    return reject_theme_handle();
}

TL_MSABI std::int32_t tl_CloseThemeData(void* const theme) noexcept {
    (void)theme;
    return reject_theme();
}

TL_MSABI std::int32_t tl_DrawThemeBackground(void* const theme, void* const hdc, const int part_id,
                                             const int state_id, const void* const rect, const void* const clip_rect) noexcept {
    (void)theme;
    (void)hdc;
    (void)part_id;
    (void)state_id;
    (void)rect;
    (void)clip_rect;
    return reject_theme();
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
    return reject_theme();
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
    return reject_theme();
}

TL_MSABI std::int32_t tl_GetThemeColor(void* const theme, const int part_id, const int state_id,
                                       const int prop_id, std::uint32_t* const color) noexcept {
    (void)theme;
    (void)part_id;
    (void)state_id;
    (void)prop_id;
    if (color == nullptr || !write_guest_value(color, std::uint32_t{0})) {
        set_last_error(abi::kErrorInvalidParameter);
        return kThemeEInvalidArg;
    }
    return reject_theme();
}

TL_MSABI std::int32_t tl_GetThemeFont(void* const theme, void* const hdc, const int part_id,
                                      const int state_id, const int prop_id, void* const font) noexcept {
    (void)theme;
    (void)hdc;
    (void)part_id;
    (void)state_id;
    (void)prop_id;
    if (font == nullptr || !clear_guest_buffer(font, 92)) { // LOGFONTW size
        set_last_error(abi::kErrorInvalidParameter);
        return kThemeEInvalidArg;
    }
    return reject_theme();
}

TL_MSABI std::int32_t tl_GetThemeMetric(void* const theme, void* const hdc, const int part_id,
                                        const int state_id, const int prop_id, int* const val) noexcept {
    (void)theme;
    (void)hdc;
    (void)part_id;
    (void)state_id;
    (void)prop_id;
    if (val == nullptr || !write_guest_value(val, 0)) {
        set_last_error(abi::kErrorInvalidParameter);
        return kThemeEInvalidArg;
    }
    return reject_theme();
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
    if (size == nullptr || !clear_guest_buffer(size, 8)) { // SIZE struct
        set_last_error(abi::kErrorInvalidParameter);
        return kThemeEInvalidArg;
    }
    return reject_theme();
}

TL_MSABI std::uint32_t tl_GetThemeSysColor(void* const theme, const int color_id) noexcept {
    (void)theme;
    (void)color_id;
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_MSABI void* tl_GetThemeSysColorBrush(void* const theme, const int color_id) noexcept {
    (void)theme;
    (void)color_id;
    return reject_theme_handle();
}

TL_MSABI int tl_IsThemeActive() noexcept {
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_MSABI int tl_IsAppThemed() noexcept {
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_MSABI int tl_IsThemeBackgroundPartiallyTransparent(void* const theme, const int part_id, const int state_id) noexcept {
    (void)theme;
    (void)part_id;
    (void)state_id;
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_MSABI std::int32_t tl_BufferedPaintInit() noexcept {
    return reject_theme();
}

TL_MSABI std::int32_t tl_BufferedPaintUnInit() noexcept {
    return reject_theme();
}

TL_MSABI void* tl_BeginBufferedPaint(void* const hdc_target, const void* const target_rect, const int format,
                                     const void* const animation_params, void** const hdc_out) noexcept {
    (void)target_rect;
    (void)format;
    (void)animation_params;
    (void)hdc_target;
    if (hdc_out != nullptr && !write_guest_value(hdc_out, static_cast<void*>(nullptr))) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    return reject_theme_handle();
}

TL_MSABI std::int32_t tl_EndBufferedPaint(void* const buffered_paint, const int update_target) noexcept {
    (void)buffered_paint;
    (void)update_target;
    return reject_theme();
}

TL_MSABI std::int32_t tl_DrawThemeParentBackground(void* const hwnd, void* const hdc, const void* const rect) noexcept {
    (void)hwnd;
    (void)hdc;
    (void)rect;
    return reject_theme();
}

TL_MSABI int tl_EndBufferedAnimation(void* const hbpAnimation, const int fUpdateTarget) noexcept {
    (void)hbpAnimation;
    (void)fUpdateTarget;
    return reject_theme();
}

TL_MSABI int tl_GetThemeTransitionDuration(void* const hTheme, const int iPartId, const int iStateIdFrom, const int iStateIdTo, const int iPropId, int* const pdwDuration) noexcept {
    (void)hTheme;
    (void)iPartId;
    (void)iStateIdFrom;
    (void)iStateIdTo;
    (void)iPropId;
    if (pdwDuration == nullptr || !write_guest_value(pdwDuration, 0)) {
        set_last_error(abi::kErrorInvalidParameter);
        return kThemeEInvalidArg;
    }
    return reject_theme();
}

TL_MSABI int tl_GetThemeBackgroundContentRect(void* const hTheme, void* const hdc, const int iPartId, const int iStateId, const void* const pBoundingRect, void* const pContentRect) noexcept {
    (void)hTheme;
    (void)hdc;
    (void)iPartId;
    (void)iStateId;
    (void)pBoundingRect;
    if (pContentRect == nullptr || !clear_guest_buffer(pContentRect, 16)) {
        set_last_error(abi::kErrorInvalidParameter);
        return kThemeEInvalidArg;
    }
    return reject_theme();
}

TL_MSABI int tl_EnableThemeDialogTexture(void* const hwnd, const std::uint32_t dwFlags) noexcept {
    (void)hwnd;
    (void)dwFlags;
    return reject_theme();
}

TL_MSABI void tl_BufferedPaintStopAllAnimations(void* const hwnd) noexcept {
    (void)hwnd;
    set_last_error(abi::kErrorNotSupported);
}

TL_MSABI void* tl_BeginBufferedAnimation(void* const hwnd, void* const hdcTarget, const void* const rcTarget, const int dwFormat, void* const pPaintParams, void* const pAnimationParams, void** const phdcFrom, void** const phdcTo) noexcept {
    (void)hwnd;
    (void)rcTarget;
    (void)dwFormat;
    (void)pPaintParams;
    (void)pAnimationParams;
    (void)hdcTarget;
    if (phdcFrom != nullptr && !write_guest_value(phdcFrom, static_cast<void*>(nullptr))) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (phdcTo != nullptr && !write_guest_value(phdcTo, static_cast<void*>(nullptr))) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    return reject_theme_handle();
}

TL_MSABI int tl_BufferedPaintRenderAnimation(void* const hwnd, void* const hdcTarget) noexcept {
    (void)hwnd;
    (void)hdcTarget;
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

} // extern "C"
} // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_uxtheme_module() {
    static const ExportedFunction kUxThemeExports[] = {
        {"SetWindowTheme", 1, reinterpret_cast<std::uintptr_t>(&tl_SetWindowTheme), ExportSupport::Stub},
        {"OpenThemeData", 2, reinterpret_cast<std::uintptr_t>(&tl_OpenThemeData), ExportSupport::Stub},
        {"CloseThemeData", 3, reinterpret_cast<std::uintptr_t>(&tl_CloseThemeData), ExportSupport::Stub},
        {"DrawThemeBackground", 4, reinterpret_cast<std::uintptr_t>(&tl_DrawThemeBackground), ExportSupport::Stub},
        {"DrawThemeText", 5, reinterpret_cast<std::uintptr_t>(&tl_DrawThemeText), ExportSupport::Stub},
        {"DrawThemeTextEx", 6, reinterpret_cast<std::uintptr_t>(&tl_DrawThemeTextEx), ExportSupport::Stub},
        {"GetThemeColor", 7, reinterpret_cast<std::uintptr_t>(&tl_GetThemeColor), ExportSupport::Stub},
        {"GetThemeFont", 8, reinterpret_cast<std::uintptr_t>(&tl_GetThemeFont), ExportSupport::Stub},
        {"GetThemeMetric", 9, reinterpret_cast<std::uintptr_t>(&tl_GetThemeMetric), ExportSupport::Stub},
        {"GetThemePartSize", 10, reinterpret_cast<std::uintptr_t>(&tl_GetThemePartSize), ExportSupport::Stub},
        {"GetThemeSysColor", 11, reinterpret_cast<std::uintptr_t>(&tl_GetThemeSysColor), ExportSupport::Stub},
        {"GetThemeSysColorBrush", 12, reinterpret_cast<std::uintptr_t>(&tl_GetThemeSysColorBrush), ExportSupport::Stub},
        {"IsThemeActive", 13, reinterpret_cast<std::uintptr_t>(&tl_IsThemeActive), ExportSupport::Stub},
        {"IsAppThemed", 14, reinterpret_cast<std::uintptr_t>(&tl_IsAppThemed), ExportSupport::Stub},
        {"IsThemeBackgroundPartiallyTransparent", 15, reinterpret_cast<std::uintptr_t>(&tl_IsThemeBackgroundPartiallyTransparent), ExportSupport::Stub},
        {"BufferedPaintInit", 16, reinterpret_cast<std::uintptr_t>(&tl_BufferedPaintInit), ExportSupport::Stub},
        {"BufferedPaintUnInit", 17, reinterpret_cast<std::uintptr_t>(&tl_BufferedPaintUnInit), ExportSupport::Stub},
        {"BeginBufferedPaint", 18, reinterpret_cast<std::uintptr_t>(&tl_BeginBufferedPaint), ExportSupport::Stub},
        {"EndBufferedPaint", 19, reinterpret_cast<std::uintptr_t>(&tl_EndBufferedPaint), ExportSupport::Stub},
        {"DrawThemeParentBackground", 20, reinterpret_cast<std::uintptr_t>(&tl_DrawThemeParentBackground), ExportSupport::Stub},
        {"EndBufferedAnimation", 21, reinterpret_cast<std::uintptr_t>(&tl_EndBufferedAnimation), ExportSupport::Stub},
        {"GetThemeTransitionDuration", 22, reinterpret_cast<std::uintptr_t>(&tl_GetThemeTransitionDuration), ExportSupport::Stub},
        {"GetThemeBackgroundContentRect", 23, reinterpret_cast<std::uintptr_t>(&tl_GetThemeBackgroundContentRect), ExportSupport::Stub},
        {"EnableThemeDialogTexture", 24, reinterpret_cast<std::uintptr_t>(&tl_EnableThemeDialogTexture), ExportSupport::Stub},
        {"BufferedPaintStopAllAnimations", 25, reinterpret_cast<std::uintptr_t>(&tl_BufferedPaintStopAllAnimations), ExportSupport::Stub},
        {"BeginBufferedAnimation", 26, reinterpret_cast<std::uintptr_t>(&tl_BeginBufferedAnimation), ExportSupport::Stub},
        {"BufferedPaintRenderAnimation", 27, reinterpret_cast<std::uintptr_t>(&tl_BufferedPaintRenderAnimation), ExportSupport::Stub},
    };
    static const InternalModule kUxThemeModule{"UxTheme.dll", kUxThemeExports};
    register_module(kUxThemeModule);
}

} // namespace tradutorlinux::loader
