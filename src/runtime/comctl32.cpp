#include "tradutorlinux/runtime/comctl32.hpp"

#include <array>
#include <cstdint>

#include "tradutorlinux/runtime/memory_validator.hpp"

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
    (void)init_controls;
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

}  // extern "C"

}  // namespace tradutorlinux
