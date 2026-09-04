#include "tradutorlinux/runtime/dwmapi.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"

#include <cstring>

namespace tradutorlinux {

namespace {

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

}  // namespace

extern "C" {

TL_DWM_MSABI std::int32_t tl_DwmSetWindowAttribute(void* const hwnd, const std::uint32_t attr,
                                                  const void* const attr_val, const std::uint32_t attr_sz) noexcept {
    (void)hwnd;
    (void)attr;
    (void)attr_val;
    (void)attr_sz;
    return 0; // S_OK
}

TL_DWM_MSABI std::int32_t tl_DwmGetWindowAttribute(void* const hwnd, const std::uint32_t attr,
                                                  void* const attr_val, const std::uint32_t attr_sz) noexcept {
    (void)hwnd;
    (void)attr;
    if (attr_val != nullptr && mapped_range(attr_val, attr_sz, true)) {
        std::memset(attr_val, 0, attr_sz);
    }
    return 0; // S_OK
}

TL_DWM_MSABI std::int32_t tl_DwmIsCompositionEnabled(int* const enabled) noexcept {
    if (enabled != nullptr && mapped_range(enabled, sizeof(int), true)) {
        *enabled = 1; // Composition enabled
    }
    return 0; // S_OK
}

TL_DWM_MSABI int tl_DwmDefWindowProc(void* const hwnd, const std::uint32_t msg, const std::uintptr_t wparam,
                                     const std::intptr_t lparam, std::intptr_t* const lresult) noexcept {
    (void)hwnd;
    (void)msg;
    (void)wparam;
    (void)lparam;
    if (lresult != nullptr && mapped_range(lresult, sizeof(std::intptr_t), true)) {
        *lresult = 0;
    }
    return 0; // Not handled by DWM
}

TL_DWM_MSABI std::int32_t tl_DwmExtendFrameIntoClientArea(void* const hwnd, const void* const margins) noexcept {
    (void)hwnd;
    (void)margins;
    return 0; // S_OK
}

TL_DWM_MSABI std::int32_t tl_DwmEnableBlurBehindWindow(void* const hwnd, const void* const blur_behind) noexcept {
    (void)hwnd;
    (void)blur_behind;
    return 0; // S_OK
}

TL_DWM_MSABI std::int32_t tl_DwmFlush() noexcept {
    return 0; // S_OK
}

TL_DWM_MSABI std::int32_t tl_DwmGetColorizationColor(std::uint32_t* const color, int* const opaque) noexcept {
    if (color != nullptr && mapped_range(color, sizeof(std::uint32_t), true)) {
        *color = 0xAA0078D7; // Windows Accent Blue
    }
    if (opaque != nullptr && mapped_range(opaque, sizeof(int), true)) {
        *opaque = 1;
    }
    return 0; // S_OK
}

}  // extern "C"
}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_dwmapi_module() {
    static const ExportedFunction kDwmApiExports[] = {
        {"DwmSetWindowAttribute", 1, reinterpret_cast<std::uintptr_t>(&tl_DwmSetWindowAttribute)},
        {"DwmGetWindowAttribute", 2, reinterpret_cast<std::uintptr_t>(&tl_DwmGetWindowAttribute)},
        {"DwmIsCompositionEnabled", 3, reinterpret_cast<std::uintptr_t>(&tl_DwmIsCompositionEnabled)},
        {"DwmDefWindowProc", 4, reinterpret_cast<std::uintptr_t>(&tl_DwmDefWindowProc)},
        {"DwmExtendFrameIntoClientArea", 5, reinterpret_cast<std::uintptr_t>(&tl_DwmExtendFrameIntoClientArea)},
        {"DwmEnableBlurBehindWindow", 6, reinterpret_cast<std::uintptr_t>(&tl_DwmEnableBlurBehindWindow)},
        {"DwmFlush", 7, reinterpret_cast<std::uintptr_t>(&tl_DwmFlush)},
        {"DwmGetColorizationColor", 8, reinterpret_cast<std::uintptr_t>(&tl_DwmGetColorizationColor)},
    };
    static const InternalModule kDwmApiModule{"DWMAPI.dll", kDwmApiExports};
    register_module(kDwmApiModule);
}

}  // namespace tradutorlinux::loader

