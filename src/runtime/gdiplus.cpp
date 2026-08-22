#include "tradutorlinux/runtime/winapi.hpp"
#include "runtime_context.hpp"

#include <cstdlib>
#include <cstring>

namespace tradutorlinux {

extern "C" {

TL_MSABI int tl_GdiplusStartup(void* token, const void* input, void* output) noexcept {
    (void)input;
    (void)output;
    if (token == nullptr || !mapped_guest_range(token, sizeof(void*), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 1;
    }
    *static_cast<void**>(token) = reinterpret_cast<void*>(0x1);
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI void tl_GdiplusShutdown(void* token) noexcept {
    (void)token;
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI void* tl_GdipAlloc(std::size_t size) noexcept {
    void* ptr = ::malloc(size);
    set_last_error(ptr != nullptr ? abi::kErrorSuccess : abi::kErrorNotEnoughMemory);
    return ptr;
}

TL_MSABI void tl_GdipFree(void* ptr) noexcept {
    ::free(ptr);
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI int tl_GdipCreateBitmapFromStream(void* stream, void** bitmap) noexcept {
    (void)stream;
    if (bitmap == nullptr || !mapped_guest_range(bitmap, sizeof(void*), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 1;
    }
    static char dummy = 0;
    *bitmap = &dummy;
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI int tl_GdipCloneImage(void* image, void** clone) noexcept {
    if (image == nullptr || clone == nullptr || !mapped_guest_range(clone, sizeof(void*), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 1;
    }
    *clone = image;
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI int tl_GdipDisposeImage(void* image) noexcept {
    (void)image;
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI int tl_GdipCreateHBITMAPFromBitmap(void* bitmap, void** hbm, std::uint32_t background) noexcept {
    (void)bitmap;
    (void)background;
    if (hbm == nullptr || !mapped_guest_range(hbm, sizeof(void*), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 1;
    }
    static char hbm_dummy = 0;
    *hbm = &hbm_dummy;
    set_last_error(abi::kErrorSuccess);
    return 0;
}

} // extern "C"

} // namespace tradutorlinux
