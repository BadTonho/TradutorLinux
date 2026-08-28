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

TL_COMCTL_MSABI void* tl_CreateStatusWindowW(const std::int32_t style, const std::uint16_t* const text,
                                             void* const parent, const std::uint32_t id) noexcept {
    (void)style;
    (void)text;
    (void)parent;
    (void)id;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x53544154ULL); // 'STAT'
}

TL_COMCTL_MSABI void* tl_CreateToolbarEx(void* const hwnd, const std::uint32_t style, const std::uint32_t id,
                                         const int num_bitmaps, void* const instance,
                                         const std::uintptr_t bitmap_id, const void* const buttons,
                                         const int num_buttons, const int cx_button, const int cy_button,
                                         const int cx_bitmap, const int cy_bitmap,
                                         const std::uint32_t struct_size) noexcept {
    (void)hwnd;
    (void)style;
    (void)id;
    (void)num_bitmaps;
    (void)instance;
    (void)bitmap_id;
    (void)buttons;
    (void)num_buttons;
    (void)cx_button;
    (void)cy_button;
    (void)cx_bitmap;
    (void)cy_bitmap;
    (void)struct_size;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x544F4F4CULL); // 'TOOL'
}

TL_COMCTL_MSABI int tl_ImageList_GetImageCount(void* const image_list) noexcept {
    if (image_list == nullptr) {
        return 0;
    }
    const auto* const list = static_cast<const InternalImageList*>(image_list);
    return list->used ? list->count : 0;
}

TL_COMCTL_MSABI std::intptr_t tl_PropertySheetW(const void* const header) noexcept {
    (void)header;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_COMCTL_MSABI std::int32_t tl_TaskDialogIndirect(const void* const config, int* const button,
                                                  int* const radio_button, int* const verification_flag_checked) noexcept {
    (void)config;
    if (button != nullptr && mapped_guest_range(button, sizeof(int), true)) {
        *button = 1; // IDOK
    }
    if (radio_button != nullptr && mapped_guest_range(radio_button, sizeof(int), true)) {
        *radio_button = 0;
    }
    if (verification_flag_checked != nullptr && mapped_guest_range(verification_flag_checked, sizeof(int), true)) {
        *verification_flag_checked = 0;
    }
    return 0; // S_OK
}

TL_COMCTL_MSABI std::int32_t tl_TaskDialog(void* const hwnd_parent, void* const instance,
                                           const std::uint16_t* const title,
                                           const std::uint16_t* const main_instruction,
                                           const std::uint16_t* const content,
                                           const std::uint32_t common_buttons,
                                           const std::uint16_t* const icon, int* const button) noexcept {
    (void)hwnd_parent;
    (void)instance;
    (void)title;
    (void)main_instruction;
    (void)content;
    (void)common_buttons;
    (void)icon;
    if (button != nullptr && mapped_guest_range(button, sizeof(int), true)) {
        *button = 1; // IDOK
    }
    return 0; // S_OK
}

TL_COMCTL_MSABI int tl_ImageList_Draw(void* const himl, const int i, void* const hdc_dst,
                                     const int x, const int y, const std::uint32_t flags) noexcept {
    (void)himl;
    (void)i;
    (void)hdc_dst;
    (void)x;
    (void)y;
    (void)flags;
    return 1;
}

TL_COMCTL_MSABI int tl_ImageList_DrawEx(void* const himl, const int i, void* const hdc_dst,
                                       const int x, const int y, const int dx, const int dy,
                                       const std::uint32_t rgb_bk, const std::uint32_t rgb_fg,
                                       const std::uint32_t flags) noexcept {
    (void)himl;
    (void)i;
    (void)hdc_dst;
    (void)x;
    (void)y;
    (void)dx;
    (void)dy;
    (void)rgb_bk;
    (void)rgb_fg;
    (void)flags;
    return 1;
}

TL_COMCTL_MSABI void* tl_ImageList_GetIcon(void* const himl, const int i, const std::uint32_t flags) noexcept {
    (void)himl;
    (void)i;
    (void)flags;
    return reinterpret_cast<void*>(0x49434F4EULL); // 'ICON'
}

TL_COMCTL_MSABI void* tl_ImageList_Duplicate(void* const himl) noexcept {
    return himl;
}

TL_COMCTL_MSABI std::uint32_t tl_ImageList_SetBkColor(void* const himl, const std::uint32_t clr_bk) noexcept {
    (void)himl;
    return clr_bk;
}

TL_COMCTL_MSABI std::uint32_t tl_ImageList_GetBkColor(void* const himl) noexcept {
    (void)himl;
    return 0xFFFFFFFF; // CLR_NONE
}

TL_COMCTL_MSABI int tl_ImageList_GetIconSize(void* const himl, int* const cx, int* const cy) noexcept {
    (void)himl;
    if (cx != nullptr && mapped_guest_range(cx, sizeof(int), true)) {
        *cx = 16;
    }
    if (cy != nullptr && mapped_guest_range(cy, sizeof(int), true)) {
        *cy = 16;
    }
    return 1;
}

TL_COMCTL_MSABI int tl_TrackMouseEvent_alias(void* const event_track) noexcept {
    (void)event_track;
    return 1;
}

TL_COMCTL_MSABI int tl_ImageList_GetImageInfo(void* const himl, const int i, void* const pImageInfo) noexcept {
    (void)himl;
    (void)i;
    if (pImageInfo != nullptr && mapped_guest_range(pImageInfo, 40, true)) {
        std::memset(pImageInfo, 0, 40);
    }
    return 1;
}

TL_COMCTL_MSABI int tl_ImageList_EndDrag() noexcept {
    return 1;
}

TL_COMCTL_MSABI int tl_ImageList_DragShowNolock(const int fShow) noexcept {
    (void)fShow;
    return 1;
}

TL_COMCTL_MSABI int tl_ImageList_DragEnter(void* const hwndLock, const int x, const int y) noexcept {
    (void)hwndLock;
    (void)x;
    (void)y;
    return 1;
}

TL_COMCTL_MSABI int tl_ImageList_DragMove(const int x, const int y) noexcept {
    (void)x;
    (void)y;
    return 1;
}

TL_COMCTL_MSABI int tl_ImageList_BeginDrag(void* const himlTrack, const int iTrack, const int dxHotspot, const int dyHotspot) noexcept {
    (void)himlTrack;
    (void)iTrack;
    (void)dxHotspot;
    (void)dyHotspot;
    return 1;
}

TL_COMCTL_MSABI int tl_ImageList_Remove(void* const himl, const int i) noexcept {
    (void)himl;
    (void)i;
    return 1;
}

TL_COMCTL_MSABI int tl_ImageList_SetIconSize(void* const himl, const int cx, const int cy) noexcept {
    (void)himl;
    (void)cx;
    (void)cy;
    return 1;
}

TL_COMCTL_MSABI int tl_LoadIconWithScaleDown(void* const hinst, const wchar_t* const pszName, const int cx, const int cy, void** const phico) noexcept {
    (void)cx;
    (void)cy;
    if (phico != nullptr && mapped_guest_range(phico, sizeof(void*), true)) {
        *phico = reinterpret_cast<void*>(tl_LoadIconW(hinst, reinterpret_cast<const std::uint16_t*>(pszName)));
        return *phico != nullptr ? 0 : static_cast<int>(0x80004005U);
    }
    return static_cast<int>(0x80070057U); // E_INVALIDARG
}

}  // extern "C"

}  // namespace tradutorlinux
