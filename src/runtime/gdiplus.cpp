#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "core/runtime_state_common.hpp"

#include <cstdlib>

namespace tradutorlinux {

extern "C" {

TL_MSABI int tl_GdiplusStartup(void* token, const void* input, void* output) noexcept {
    (void)input;
    (void)output;
    if (token == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 2; // InvalidParameter
    }
    static constexpr std::uintptr_t kGdiplusToken = 0x47444950ULL; // "GDIP"
    if (!write_guest_value(token, reinterpret_cast<void*>(kGdiplusToken))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 2; // InvalidParameter
    }
    set_last_error(abi::kErrorSuccess);
    return 0; // Ok
}

TL_MSABI void tl_GdiplusShutdown(void* token) noexcept {
    (void)token;
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI void* tl_GdipAlloc(std::size_t size) noexcept {
    if (size == 0) {
        return nullptr;
    }
    void* const ptr = std::malloc(size);
    if (ptr == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return ptr;
}

TL_MSABI void tl_GdipFree(void* ptr) noexcept {
    if (ptr != nullptr) {
        std::free(ptr);
    }
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI int tl_GdipCreateBitmapFromStream(void* stream, void** bitmap) noexcept {
    if (bitmap == nullptr || !write_guest_value(bitmap, static_cast<void*>(nullptr))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 2; // InvalidParameter
    }
    if (stream == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 2; // InvalidParameter
    }
    void* const dummy_bitmap = std::malloc(sizeof(std::uintptr_t));
    if (dummy_bitmap == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 3; // OutOfMemory
    }
    *reinterpret_cast<std::uintptr_t*>(dummy_bitmap) = 0x424d5031ULL; // "BMP1"
    if (!write_guest_value(bitmap, dummy_bitmap)) {
        std::free(dummy_bitmap);
        set_last_error(abi::kErrorInvalidParameter);
        return 2;
    }
    set_last_error(abi::kErrorSuccess);
    return 0; // Ok
}

TL_MSABI int tl_GdipCloneImage(void* image, void** clone) noexcept {
    if (clone == nullptr || !write_guest_value(clone, static_cast<void*>(nullptr))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 2; // InvalidParameter
    }
    if (image == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 2; // InvalidParameter
    }
    void* const dummy_clone = std::malloc(sizeof(std::uintptr_t));
    if (dummy_clone == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 3; // OutOfMemory
    }
    *reinterpret_cast<std::uintptr_t*>(dummy_clone) = *reinterpret_cast<const std::uintptr_t*>(image);
    if (!write_guest_value(clone, dummy_clone)) {
        std::free(dummy_clone);
        set_last_error(abi::kErrorInvalidParameter);
        return 2;
    }
    set_last_error(abi::kErrorSuccess);
    return 0; // Ok
}

TL_MSABI int tl_GdipDisposeImage(void* image) noexcept {
    if (image == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 2; // InvalidParameter
    }
    std::free(image);
    set_last_error(abi::kErrorSuccess);
    return 0; // Ok
}

TL_MSABI int tl_GdipCreateHBITMAPFromBitmap(void* bitmap, void** hbm, std::uint32_t background) noexcept {
    (void)background;
    if (hbm == nullptr || !write_guest_value(hbm, static_cast<void*>(nullptr))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 2; // InvalidParameter
    }
    if (bitmap == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 2; // InvalidParameter
    }
    static std::uintptr_t s_next_hbm = 0x8000;
    const auto handle = reinterpret_cast<void*>(++s_next_hbm);
    if (!write_guest_value(hbm, handle)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 2;
    }
    set_last_error(abi::kErrorSuccess);
    return 0; // Ok
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
