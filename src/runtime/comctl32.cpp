#include "tradutorlinux/runtime/comctl32.hpp"
#include "tradutorlinux/runtime/winapi.hpp"

#include <array>
#include <cstdint>

#include "runtime_context.hpp"

namespace tradutorlinux {

namespace {

struct InternalImageList {
    bool used{false};
    int cx{0};
    int cy{0};
    int count{0};
};

std::array<InternalImageList, 64> g_image_lists{};

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

}  // namespace

extern "C" {

TL_COMCTL_MSABI void tl_InitCommonControls() noexcept {
}

TL_COMCTL_MSABI int tl_InitCommonControlsEx(const void* init_controls) noexcept {
    if (init_controls == nullptr ||
        !mapped_range(init_controls, sizeof(abi::GuestInitCommonControlsEx), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto* const value = static_cast<const abi::GuestInitCommonControlsEx*>(init_controls);
    if (value->size != sizeof(abi::GuestInitCommonControlsEx)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // ICC_* values currently defined by the SDK occupy the low 16 bits.  The
    // runtime accepts initialization of those logical classes, while actual
    // child controls remain limited to the USER32 renderer.
    if ((value->classes & 0xFFFF0000U) != 0U || value->classes == 0U) {
        set_last_error(abi::kErrorNotSupported);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_COMCTL_MSABI void* tl_ImageList_Create(const int cx, const int cy, const std::uint32_t flags,
                                          const int initial, const int grow) noexcept {
    (void)flags;
    (void)initial;
    (void)grow;
    for (auto& list : g_image_lists) {
        if (!list.used) {
            list.used = true;
            list.cx = cx;
            list.cy = cy;
            list.count = 0;
            return &list;
        }
    }
    return nullptr;
}

TL_COMCTL_MSABI int tl_ImageList_Destroy(void* image_list) noexcept {
    if (image_list == nullptr) {
        return 0;
    }
    auto* list = static_cast<InternalImageList*>(image_list);
    list->used = false;
    list->count = 0;
    return 1;
}

TL_COMCTL_MSABI int tl_ImageList_Add(void* image_list, void* image, void* mask) noexcept {
    (void)image;
    (void)mask;
    if (image_list == nullptr) {
        return -1;
    }
    auto* list = static_cast<InternalImageList*>(image_list);
    if (!list->used) {
        return -1;
    }
    return list->count++;
}

TL_COMCTL_MSABI int tl_ImageList_AddMasked(void* image_list, void* bitmap, const std::uint32_t mask_color) noexcept {
    (void)bitmap;
    (void)mask_color;
    if (image_list == nullptr) {
        return -1;
    }
    auto* list = static_cast<InternalImageList*>(image_list);
    if (!list->used) {
        return -1;
    }
    return list->count++;
}

TL_COMCTL_MSABI int tl_ImageList_ReplaceIcon(void* image_list, const int index, void* icon) noexcept {
    (void)icon;
    if (image_list == nullptr) {
        return -1;
    }
    auto* list = static_cast<InternalImageList*>(image_list);
    if (!list->used) {
        return -1;
    }
    if (index < 0) {
        return list->count++;
    }
    return index;
}

TL_COMCTL_MSABI int tl_SetWindowSubclass(void* const hwnd, void* const subclass_proc,
                                         const std::uintptr_t subclass_id,
                                         const std::uintptr_t ref_data) noexcept {
    (void)hwnd;
    (void)subclass_proc;
    (void)subclass_id;
    (void)ref_data;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_COMCTL_MSABI int tl_RemoveWindowSubclass(void* const hwnd, void* const subclass_proc,
                                            const std::uintptr_t subclass_id) noexcept {
    (void)hwnd;
    (void)subclass_proc;
    (void)subclass_id;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_COMCTL_MSABI std::intptr_t tl_DefSubclassProc(void* const hwnd, const std::uint32_t msg,
                                                 const std::uintptr_t wparam,
                                                 const std::intptr_t lparam) noexcept {
    (void)hwnd;
    (void)msg;
    (void)wparam;
    (void)lparam;
    return 0;
}

}  // extern "C"

}  // namespace tradutorlinux
