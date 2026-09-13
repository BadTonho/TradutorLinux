#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "core/runtime_state_common.hpp"

namespace tradutorlinux {

extern "C" {

TL_MSABI int tl_GdiplusStartup(void* token, const void* input, void* output) noexcept {
    (void)input;
    (void)output;
    if (token == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 1;
    }
    if (!write_guest_value(token, static_cast<void*>(nullptr))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 1;
    }
    set_last_error(abi::kErrorNotSupported);
    return 1; // GenericError: GDI+ is not initialized by this runtime.
}

TL_MSABI void tl_GdiplusShutdown(void* token) noexcept {
    (void)token;
    set_last_error(abi::kErrorNotSupported);
}

TL_MSABI void* tl_GdipAlloc(std::size_t size) noexcept {
    (void)size;
    set_last_error(abi::kErrorNotSupported);
    return nullptr;
}

TL_MSABI void tl_GdipFree(void* ptr) noexcept {
    (void)ptr;
    set_last_error(abi::kErrorNotSupported);
}

TL_MSABI int tl_GdipCreateBitmapFromStream(void* stream, void** bitmap) noexcept {
    (void)stream;
    if (bitmap == nullptr || !write_guest_value(bitmap, static_cast<void*>(nullptr))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 1;
    }
    set_last_error(abi::kErrorNotSupported);
    return 1; // GenericError: no GDI+ image backend is installed.
}

TL_MSABI int tl_GdipCloneImage(void* image, void** clone) noexcept {
    (void)image;
    if (clone == nullptr || !write_guest_value(clone, static_cast<void*>(nullptr))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 1;
    }
    set_last_error(abi::kErrorNotSupported);
    return 1; // GenericError: no GDI+ image backend is installed.
}

TL_MSABI int tl_GdipDisposeImage(void* image) noexcept {
    (void)image;
    set_last_error(abi::kErrorNotSupported);
    return 1; // GenericError: there are no runtime-owned GDI+ images.
}

TL_MSABI int tl_GdipCreateHBITMAPFromBitmap(void* bitmap, void** hbm, std::uint32_t background) noexcept {
    (void)bitmap;
    (void)background;
    if (hbm == nullptr || !write_guest_value(hbm, static_cast<void*>(nullptr))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 1;
    }
    set_last_error(abi::kErrorNotSupported);
    return 1; // GenericError: bitmap conversion is not implemented.
}

} // extern "C"
} // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_gdiplus_module() {
    static const ExportedFunction kGdiplusExports[] = {
        {"GdiplusStartup", 1, reinterpret_cast<std::uintptr_t>(&tl_GdiplusStartup), ExportSupport::Stub},
        {"GdiplusShutdown", 2, reinterpret_cast<std::uintptr_t>(&tl_GdiplusShutdown), ExportSupport::Stub},
        {"GdipAlloc", 3, reinterpret_cast<std::uintptr_t>(&tl_GdipAlloc), ExportSupport::Stub},
        {"GdipFree", 4, reinterpret_cast<std::uintptr_t>(&tl_GdipFree), ExportSupport::Stub},
        {"GdipCreateBitmapFromStream", 5, reinterpret_cast<std::uintptr_t>(&tl_GdipCreateBitmapFromStream), ExportSupport::Stub},
        {"GdipCloneImage", 6, reinterpret_cast<std::uintptr_t>(&tl_GdipCloneImage), ExportSupport::Stub},
        {"GdipDisposeImage", 7, reinterpret_cast<std::uintptr_t>(&tl_GdipDisposeImage), ExportSupport::Stub},
        {"GdipCreateHBITMAPFromBitmap", 8, reinterpret_cast<std::uintptr_t>(&tl_GdipCreateHBITMAPFromBitmap), ExportSupport::Stub},
    };
    static const InternalModule kGdiplusModule{"gdiplus.dll", kGdiplusExports};
    register_module(kGdiplusModule);
}

} // namespace tradutorlinux::loader
