#include "tradutorlinux/runtime/comctl32.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "tradutorlinux/runtime/winapi.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

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
    if (value->classes == 0U) {
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
    if (parent == nullptr || (text != nullptr && !mapped_guest_wstring(text))) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    WindowSlot* const parent_slot = find_window_slot(parent);
    if (parent_slot == nullptr || parent_slot->is_control) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }
    const std::string utf8_text = text == nullptr ? std::string{} : util::wide_to_utf8(text);
    constexpr int kStatusHeight = 24;
    WindowSlot* const slot = create_logical_control(
        *parent_slot, "msctls_statusbar32", utf8_text, static_cast<std::uint32_t>(style), id, 0,
        std::max(parent_slot->height - kStatusHeight, 0), std::max(parent_slot->width, 1),
        kStatusHeight);
    if (slot == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "CreateStatusWindowW"},
        diagnostics::TraceField{"class", slot->class_name},
        diagnostics::TraceField{"status", "logical-child"},
        diagnostics::TraceField{"id", std::to_string(id)},
    };
    runtime_trace("CreateStatusWindowW", fields, 4);
    return slot;
}

TL_COMCTL_MSABI void* tl_CreateToolbarEx(void* const hwnd, const std::uint32_t style, const std::uint32_t id,
                                         const int num_bitmaps, void* const instance,
                                         const std::uintptr_t bitmap_id, const void* const buttons,
                                         const int num_buttons, const int cx_button, const int cy_button,
                                         const int cx_bitmap, const int cy_bitmap,
                                         const std::uint32_t struct_size) noexcept {
    (void)num_bitmaps;
    (void)instance;
    (void)bitmap_id;
    (void)cx_bitmap;
    (void)cy_bitmap;
    if (hwnd == nullptr || num_buttons < 0 || num_buttons > 128 ||
        (num_buttons > 0 &&
         (buttons == nullptr || struct_size < sizeof(std::int32_t) * 2U || struct_size > 64U ||
          static_cast<std::size_t>(num_buttons) >
              std::numeric_limits<std::size_t>::max() / struct_size))) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    WindowSlot* const parent_slot = find_window_slot(hwnd);
    if (parent_slot == nullptr || parent_slot->is_control) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }
    if (num_buttons > 0 &&
        !mapped_range(buttons, static_cast<std::size_t>(num_buttons) * struct_size, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    constexpr int kMaxControlDimension = 8192;
    const int button_width = std::clamp(cx_button > 0 ? cx_button : 72, 1,
                                        kMaxControlDimension);
    const int toolbar_height = std::clamp(cy_button > 0 ? cy_button : 24, 1,
                                          kMaxControlDimension);
    WindowSlot* const slot = create_logical_control(
        *parent_slot, "ToolbarWindow32", {}, style, id, 0, 0, std::max(parent_slot->width, 1),
        toolbar_height);
    if (slot == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    slot->toolbar_button_width = button_width;
    slot->toolbar_button_struct_size = struct_size == 0U ? 32U : struct_size;
    slot->toolbar_buttons.reserve(static_cast<std::size_t>(num_buttons));
    for (int index = 0; index < num_buttons; ++index) {
        std::int32_t command_id = 0;
        const auto* const entry = static_cast<const std::byte*>(buttons) +
                                  static_cast<std::size_t>(index) * struct_size;
        std::memcpy(&command_id, entry + sizeof(std::int32_t), sizeof(command_id));
        slot->toolbar_buttons.push_back(ToolbarButton{command_id});
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "CreateToolbarEx"},
        diagnostics::TraceField{"class", slot->class_name},
        diagnostics::TraceField{"status", "logical-child"},
        diagnostics::TraceField{"buttons", std::to_string(num_buttons)},
    };
    runtime_trace("CreateToolbarEx", fields, 4);
    return slot;
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

namespace tradutorlinux::loader {

void register_comctl32_module() {
    static const ExportedFunction kComctl32Exports[] = {
        {"InitCommonControls", 1, reinterpret_cast<std::uintptr_t>(&tl_InitCommonControls)},
        {"InitCommonControlsEx", 2, reinterpret_cast<std::uintptr_t>(&tl_InitCommonControlsEx)},
        {"ImageList_Create", 3, reinterpret_cast<std::uintptr_t>(&tl_ImageList_Create)},
        {"ImageList_Destroy", 4, reinterpret_cast<std::uintptr_t>(&tl_ImageList_Destroy)},
        {"ImageList_Add", 5, reinterpret_cast<std::uintptr_t>(&tl_ImageList_Add)},
        {"ImageList_AddMasked", 6, reinterpret_cast<std::uintptr_t>(&tl_ImageList_AddMasked)},
        {"ImageList_ReplaceIcon", 7, reinterpret_cast<std::uintptr_t>(&tl_ImageList_ReplaceIcon)},
        {"CreateStatusWindowW", 8, reinterpret_cast<std::uintptr_t>(&tl_CreateStatusWindowW)},
        {"CreateToolbarEx", 9, reinterpret_cast<std::uintptr_t>(&tl_CreateToolbarEx)},
        {"ImageList_GetImageCount", 10, reinterpret_cast<std::uintptr_t>(&tl_ImageList_GetImageCount)},
        {"PropertySheetW", 11, reinterpret_cast<std::uintptr_t>(&tl_PropertySheetW)},
        {"TaskDialogIndirect", 12, reinterpret_cast<std::uintptr_t>(&tl_TaskDialogIndirect)},
        {"TaskDialog", 13, reinterpret_cast<std::uintptr_t>(&tl_TaskDialog)},
        {"ImageList_Draw", 14, reinterpret_cast<std::uintptr_t>(&tl_ImageList_Draw)},
        {"ImageList_DrawEx", 15, reinterpret_cast<std::uintptr_t>(&tl_ImageList_DrawEx)},
        {"ImageList_GetIcon", 16, reinterpret_cast<std::uintptr_t>(&tl_ImageList_GetIcon)},
        {"ImageList_Duplicate", 17, reinterpret_cast<std::uintptr_t>(&tl_ImageList_Duplicate)},
        {"ImageList_SetBkColor", 18, reinterpret_cast<std::uintptr_t>(&tl_ImageList_SetBkColor)},
        {"ImageList_GetBkColor", 19, reinterpret_cast<std::uintptr_t>(&tl_ImageList_GetBkColor)},
        {"ImageList_GetIconSize", 20, reinterpret_cast<std::uintptr_t>(&tl_ImageList_GetIconSize)},
        {"_TrackMouseEvent", 21, reinterpret_cast<std::uintptr_t>(&tl_TrackMouseEvent_alias)},
        {"ImageList_GetImageInfo", 22, reinterpret_cast<std::uintptr_t>(&tl_ImageList_GetImageInfo)},
        {"ImageList_EndDrag", 23, reinterpret_cast<std::uintptr_t>(&tl_ImageList_EndDrag)},
        {"ImageList_DragShowNolock", 24, reinterpret_cast<std::uintptr_t>(&tl_ImageList_DragShowNolock)},
        {"ImageList_DragEnter", 25, reinterpret_cast<std::uintptr_t>(&tl_ImageList_DragEnter)},
        {"ImageList_DragMove", 26, reinterpret_cast<std::uintptr_t>(&tl_ImageList_DragMove)},
        {"ImageList_BeginDrag", 27, reinterpret_cast<std::uintptr_t>(&tl_ImageList_BeginDrag)},
        {"ImageList_Remove", 28, reinterpret_cast<std::uintptr_t>(&tl_ImageList_Remove)},
        {"ImageList_SetIconSize", 29, reinterpret_cast<std::uintptr_t>(&tl_ImageList_SetIconSize)},
        {"CreateToolbarEx", 30, reinterpret_cast<std::uintptr_t>(&tl_CreateToolbarEx)},
        {"LoadIconWithScaleDown", 381, reinterpret_cast<std::uintptr_t>(&tl_LoadIconWithScaleDown)},
        {"SetWindowSubclass", 410, reinterpret_cast<std::uintptr_t>(&tl_SetWindowSubclass)},
        {"RemoveWindowSubclass", 412, reinterpret_cast<std::uintptr_t>(&tl_RemoveWindowSubclass)},
        {"DefSubclassProc", 413, reinterpret_cast<std::uintptr_t>(&tl_DefSubclassProc)},
        {"", 17, reinterpret_cast<std::uintptr_t>(&tl_InitCommonControls)},
        {"", 381, reinterpret_cast<std::uintptr_t>(&tl_LoadIconWithScaleDown)},
        {"", 410, reinterpret_cast<std::uintptr_t>(&tl_SetWindowSubclass)},
        {"", 411, reinterpret_cast<std::uintptr_t>(&tl_TaskDialogIndirect)},
        {"", 412, reinterpret_cast<std::uintptr_t>(&tl_RemoveWindowSubclass)},
        {"", 413, reinterpret_cast<std::uintptr_t>(&tl_DefSubclassProc)},
    };
    static const InternalModule kComctl32Module{"COMCTL32.dll", kComctl32Exports};
    register_module(kComctl32Module);
}

}  // namespace tradutorlinux::loader
