#include "user32_internal.hpp"
namespace tradutorlinux {
extern "C" {

TL_MSABI void* tl_BeginPaint(const void* const window, void* const paint_struct) noexcept {
    if (!user32_gui_thread_allowed("BeginPaint")) {
        return nullptr;
    }
    if (paint_struct == nullptr ||
        !mapped_guest_range(paint_struct, sizeof(abi::GuestPaintStruct), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr || window_drawing_target(window).native == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }
    auto* const ps = static_cast<abi::GuestPaintStruct*>(paint_struct);
    *ps = {};
    ps->hdc = const_cast<void*>(window);
    ps->f_erase = 1;
    ps->rc_paint = {0, 0, slot->width, slot->height};
    slot->painting = true;
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "BeginPaint"},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("BeginPaint", fields, 2);
    set_last_error(abi::kErrorSuccess);
    return ps->hdc;
}

TL_MSABI int tl_EndPaint(const void* const window, const void* const paint_struct) noexcept {
    if (!user32_gui_thread_allowed("EndPaint")) {
        return 0;
    }
    if (paint_struct == nullptr ||
        !mapped_guest_range(paint_struct, sizeof(abi::GuestPaintStruct), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    slot->painting = false;
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "EndPaint"},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("EndPaint", fields, 2);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetCursorPos(void* point) noexcept {
    if (point == nullptr || !mapped_guest_range(point, sizeof(std::int32_t) * 2U, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    auto* coordinates = static_cast<std::int32_t*>(point);
    coordinates[0] = 0;
    coordinates[1] = 0;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_InvalidateRect(const void* window, const void* rect, int erase) noexcept {
    if (!user32_gui_thread_allowed("InvalidateRect")) {
        return 0;
    }
    (void)rect;
    (void)erase;
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (slot->native != nullptr) {
        gui::platform::flush_window(slot->native);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uintptr_t tl_LoadCursorA(const void* instance, const char* name) noexcept {
    (void)instance;
    (void)name;
    return 1;
}

TL_MSABI std::uintptr_t tl_LoadCursorW(const void* instance, const std::uint16_t* name) noexcept {
    (void)instance;
    if (name != nullptr && !mapped_guest_wstring(name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uintptr_t tl_LoadIconA(const void* instance, const char* name) noexcept {
    (void)instance;
    (void)name;
    return 1;
}

TL_MSABI std::uintptr_t tl_LoadIconW(const void* instance, const std::uint16_t* name) noexcept {
    (void)instance;
    if (name != nullptr && !mapped_guest_wstring(name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CopyImage(const void* image, const std::uint32_t image_type, const int width,
                            const int height, const std::uint32_t flags) noexcept {
    if (!user32_gui_thread_allowed("CopyImage")) {
        return nullptr;
    }
    (void)flags;
    if (image == nullptr || image_type != kImageIcon || width < 0 || height < 0 ||
        (image != reinterpret_cast<const void*>(1U) &&
         std::none_of(g_image_slots.begin(), g_image_slots.end(),
                      [image](const ImageSlot& slot) { return slot.used && &slot == image; }))) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const auto free_it = std::find_if(g_image_slots.begin(), g_image_slots.end(),
                                      [](const ImageSlot& slot) { return !slot.used; });
    if (free_it == g_image_slots.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    free_it->used = true;
    free_it->source = image;
    set_last_error(abi::kErrorSuccess);
    return &*free_it;
}

TL_MSABI int tl_DestroyIcon(const void* icon) noexcept {
    if (!user32_gui_thread_allowed("DestroyIcon")) {
        return 0;
    }
    if (icon == reinterpret_cast<const void*>(1U)) {
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    const auto it = std::find_if(g_image_slots.begin(), g_image_slots.end(),
                                 [icon](const ImageSlot& slot) { return slot.used && &slot == icon; });
    if (it == g_image_slots.end()) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    *it = {};
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetSystemMetrics(const int index) noexcept {
    switch (index) {
        case 0: return 1920; // SM_CXSCREEN
        case 1: return 1080; // SM_CYSCREEN
        case 2: return 16;   // SM_CXVSCROLL
        case 3: return 16;   // SM_CYHSCROLL
        case 4: return 24;   // SM_CYCAPTION
        case 5: return 2;    // SM_CXBORDER
        case 6: return 2;    // SM_CYBORDER
        case 7: return 4;    // SM_CXDLGFRAME
        case 8: return 4;    // SM_CYDLGFRAME
        case 11: return 32;  // SM_CXICON
        case 12: return 32;  // SM_CYICON
        case 13: return 32;  // SM_CXCURSOR
        case 14: return 32;  // SM_CYCURSOR
        case 15: return 20;  // SM_CYMENU
        case 16: return 1920; // SM_CXFULLSCREEN
        case 17: return 1040; // SM_CYFULLSCREEN
        case 43: return 1;   // SM_CMOUSEBUTTONS
        case 74: return 0;   // SM_REMOTESESSION
        case 75: return 0;   // SM_SHUTTINGDOWN
        case 80: return 1;   // SM_CMONITORS
        default: return 0;
    }
}

TL_MSABI void* tl_GetDC(const void* window) noexcept {
    if (!user32_gui_thread_allowed("GetDC")) {
        return nullptr;
    }
    if (window == nullptr) {
        static char g_screen_dc_token = 0;
        set_last_error(abi::kErrorSuccess);
        return &g_screen_dc_token;
    }
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr || window_drawing_target(window).native == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return const_cast<void*>(window);
}

TL_MSABI int tl_ReleaseDC(const void* window, const void* dc) noexcept {
    (void)window;
    (void)dc;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_SetCursor(const void* cursor) noexcept {
    (void)cursor;
    static char g_cursor_token = 0;
    return &g_cursor_token;
}

TL_MSABI int tl_ShowCursor(const int show) noexcept {
    return show >= 0 ? 0 : -1;
}

TL_MSABI int tl_SetCursorPos(const int, const int) noexcept {
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_LoadStringA(void* instance, const std::uint32_t id, char* buffer, const int buffer_max) noexcept {
    if (buffer == nullptr || buffer_max <= 0 || !mapped_guest_range(buffer, static_cast<std::size_t>(buffer_max), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::vector<std::uint16_t> wide(static_cast<std::size_t>(buffer_max), 0);
    const int length = tl_LoadStringW(instance, id, wide.data(), buffer_max);
    if (length <= 0) {
        buffer[0] = '\0';
        return 0;
    }
    const std::string utf8 = util::wide_to_utf8(wide.data(), static_cast<std::size_t>(length));
    const std::size_t copied = std::min(utf8.size(), static_cast<std::size_t>(buffer_max - 1));
    std::memcpy(buffer, utf8.data(), copied);
    buffer[copied] = '\0';
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(copied);
}

TL_MSABI int tl_LoadStringW(void* instance, const std::uint32_t id, std::uint16_t* buffer, const int buffer_max) noexcept {
    if (buffer == nullptr || buffer_max <= 0 || !mapped_guest_range(buffer, static_cast<std::size_t>(buffer_max) * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // IMAGE_RESOURCE_DATA_ENTRY type STRING stores 16 strings in a block.
    const auto type = reinterpret_cast<const std::uint16_t*>(static_cast<std::uintptr_t>(6U));
    const auto block = reinterpret_cast<const std::uint16_t*>(
        static_cast<std::uintptr_t>(id / 16U + 1U));
    void* const resource = tl_FindResourceW(instance, block, type);
    void* const loaded = resource == nullptr ? nullptr : tl_LoadResource(instance, resource);
    const auto* const data = loaded == nullptr
                                 ? nullptr
                                 : static_cast<const std::uint16_t*>(tl_LockResource(loaded));
    const std::uint32_t byte_size = resource == nullptr ? 0U : tl_SizeofResource(instance, resource);
    if (data == nullptr || byte_size < sizeof(std::uint16_t)) {
        buffer[0] = 0;
        set_last_error(abi::kErrorResourceNameNotFound);
        return 0;
    }

    const std::size_t unit_count = byte_size / sizeof(std::uint16_t);
    std::size_t offset = 0;
    const std::size_t index = id % 16U;
    for (std::size_t current = 0; current <= index; ++current) {
        if (offset >= unit_count) {
            buffer[0] = 0;
            set_last_error(abi::kErrorResourceDataNotFound);
            return 0;
        }
        const std::size_t length = data[offset++];
        if (length > unit_count - offset) {
            buffer[0] = 0;
            set_last_error(abi::kErrorResourceDataNotFound);
            return 0;
        }
        if (current == index) {
            const std::size_t copied = std::min(length, static_cast<std::size_t>(buffer_max - 1));
            std::memcpy(buffer, data + offset, copied * sizeof(std::uint16_t));
            buffer[copied] = 0;
            set_last_error(abi::kErrorSuccess);
            return static_cast<int>(copied);
        }
        offset += length;
    }
    buffer[0] = 0;
    set_last_error(abi::kErrorResourceNameNotFound);
    return 0;
}

TL_MSABI int tl_PtInRect(const void* const rect, const std::int32_t x, const std::int32_t y) noexcept {
    if (rect == nullptr || !mapped_guest_range(rect, sizeof(abi::GuestRect), false)) {
        return 0;
    }
    const auto* const r = static_cast<const abi::GuestRect*>(rect);
    return (x >= r->left && x < r->right && y >= r->top && y < r->bottom) ? 1 : 0;
}

TL_MSABI int tl_CopyRect(void* const dest_rect, const void* const src_rect) noexcept {
    if (dest_rect == nullptr || src_rect == nullptr ||
        !mapped_guest_range(dest_rect, sizeof(abi::GuestRect), true) ||
        !mapped_guest_range(src_rect, sizeof(abi::GuestRect), false)) {
        return 0;
    }
    *static_cast<abi::GuestRect*>(dest_rect) = *static_cast<const abi::GuestRect*>(src_rect);
    return 1;
}

TL_MSABI std::uint32_t tl_GetSysColor(const int index) noexcept {
    switch (index) {
        case kColorWindow: return 0x00FFFFFFU;
        case kColorWindowText:
        case kColorBtnText:
        case kColorCaptionText:
        case kColorMenuText:
        case kColorInfoText: return 0x00000000U;
        case kColorBtnFace:
        case kColor3dLight:
        case kColorMenu: return 0x00F0F0F0U;
        case kColorHighlight: return 0x00D77800U;
        case kColorHighlightText: return 0x00FFFFFFU;
        case kColorBtnShadow:
        case kColorGrayText: return 0x00A0A0A0U;
        case kColor3dDkShadow:
        case kColorWindowFrame: return 0x00696969U;
        case kColorInfoBk: return 0x00E1FFFFU;
        default: return 0x00FFFFFFU;
    }
}

TL_MSABI std::uint16_t* tl_CharUpperW(std::uint16_t* const str) noexcept {
    if (reinterpret_cast<std::uintptr_t>(str) <= 0xFFFFU) {
        auto ch = static_cast<char16_t>(reinterpret_cast<std::uintptr_t>(str));
        if (ch >= u'a' && ch <= u'z') {
            ch = static_cast<char16_t>(ch - u'a' + u'A');
        }
        return reinterpret_cast<std::uint16_t*>(static_cast<std::uintptr_t>(ch));
    }
    if (!mapped_guest_wstring(str)) return str;
    for (std::uint16_t* p = str; *p != 0; ++p) {
        if (*p >= u'a' && *p <= u'z') {
            *p = static_cast<std::uint16_t>(*p - u'a' + u'A');
        }
    }
    return str;
}

TL_MSABI std::uint16_t* tl_CharLowerW(std::uint16_t* const str) noexcept {
    if (reinterpret_cast<std::uintptr_t>(str) <= 0xFFFFU) {
        auto ch = static_cast<char16_t>(reinterpret_cast<std::uintptr_t>(str));
        if (ch >= u'A' && ch <= u'Z') {
            ch = static_cast<char16_t>(ch - u'A' + u'a');
        }
        return reinterpret_cast<std::uint16_t*>(static_cast<std::uintptr_t>(ch));
    }
    if (!mapped_guest_wstring(str)) return str;
    for (std::uint16_t* p = str; *p != 0; ++p) {
        if (*p >= u'A' && *p <= u'Z') {
            *p = static_cast<std::uint16_t>(*p - u'A' + u'a');
        }
    }
    return str;
}

TL_MSABI const char* tl_CharPrevExA(const std::uint32_t code_page, const char* const start,
                                     const char* const current, const std::uint32_t flags) noexcept {
    (void)code_page;
    (void)flags;
    if (start == nullptr || current == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return start;
    }
    if (!mapped_guest_cstring(start)) {
        set_last_error(abi::kErrorInvalidParameter);
        return start;
    }
    if (current <= start) {
        set_last_error(abi::kErrorSuccess);
        return start;
    }
    const std::uintptr_t start_addr = reinterpret_cast<std::uintptr_t>(start);
    const std::uintptr_t cur_addr = reinterpret_cast<std::uintptr_t>(current);
    if (cur_addr - start_addr > 1U << 20U) {
        set_last_error(abi::kErrorInvalidParameter);
        return start;
    }
    if (!mapped_guest_range(current - 1, 1, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return start;
    }
    // DBCS não suportado (CP 932/936 etc. sempre retorna FALSE em IsDBCSLeadByteEx),
    // então o char anterior é sempre current-1 para o 7z.dll.
    set_last_error(abi::kErrorSuccess);
    return current - 1;
}

TL_MSABI int tl_DrawTextA(const void* const dc, const char* const text, const int count,
                          void* const rect, const std::uint32_t format) noexcept {
    if (text == nullptr || rect == nullptr || !mapped_guest_range(rect, sizeof(abi::GuestRect), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::size_t len = (count < 0) ? std::strlen(text) : static_cast<std::size_t>(count);
    auto* const r = static_cast<abi::GuestRect*>(rect);
    constexpr int kLineHeight = 16;
    constexpr int kCharWidth = 8;
    if ((format & kDtCalcRect) != 0) {
        r->right = r->left + static_cast<std::int32_t>(len * kCharWidth);
        r->bottom = r->top + kLineHeight;
        return kLineHeight;
    }
    if (dc != nullptr) {
        (void)tl_TextOut(dc, r->left, r->top, text, static_cast<int>(len));
    }
    return kLineHeight;
}

TL_MSABI int tl_DrawTextW(const void* const dc, const std::uint16_t* const text, const int count,
                          void* const rect, const std::uint32_t format) noexcept {
    if (text == nullptr || rect == nullptr || !mapped_guest_range(rect, sizeof(abi::GuestRect), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::size_t len = 0;
    if (count < 0) {
        while (text[len] != 0) ++len;
    } else {
        len = static_cast<std::size_t>(count);
    }
    auto* const r = static_cast<abi::GuestRect*>(rect);
    constexpr int kLineHeight = 16;
    constexpr int kCharWidth = 8;
    if ((format & kDtCalcRect) != 0) {
        r->right = r->left + static_cast<std::int32_t>(len * kCharWidth);
        r->bottom = r->top + kLineHeight;
        return kLineHeight;
    }
    if (dc != nullptr) {
        const std::string utf8 = util::wide_to_utf8(text, len);
        (void)tl_TextOut(dc, r->left, r->top, utf8.c_str(), static_cast<int>(utf8.size()));
    }
    return kLineHeight;
}

TL_MSABI int tl_SetUserObjectInformationW(void* const obj, const int index, void* const info,
                                          const std::uint32_t length) noexcept {
    (void)obj;
    (void)index;
    (void)info;
    (void)length;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetProcessDefaultLayout(const std::uint32_t default_layout) noexcept {
    (void)default_layout;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_DrawIconEx(void* const hdc, const int x_left, const int y_top, void* const hicon,
                           const int cx_width, const int cy_width, const std::uint32_t step_if_ani_cur,
                           void* const hbr_flicker_free_draw, const std::uint32_t flags) noexcept {
    (void)hdc;
    (void)x_left;
    (void)y_top;
    (void)hicon;
    (void)cx_width;
    (void)cy_width;
    (void)step_if_ani_cur;
    (void)hbr_flicker_free_draw;
    (void)flags;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_LoadImageW(void* const hinst, const std::uint16_t* const name, const std::uint32_t type,
                             const int cx, const int cy, const std::uint32_t fu_load) noexcept {
    (void)hinst;
    (void)name;
    (void)type;
    (void)cx;
    (void)cy;
    (void)fu_load;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x1000);
}

TL_MSABI int tl_ClientToScreen(void* const hwnd, void* const point) noexcept {
    (void)hwnd;
    (void)point;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetMonitorInfoA(void* const monitor, void* const mi) noexcept {
    (void)monitor;
    (void)mi;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SystemParametersInfoW(const std::uint32_t action, const std::uint32_t param1,
                                      void* const param2, const std::uint32_t win_ini) noexcept {
    (void)action;
    (void)param1;
    (void)param2;
    (void)win_ini;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_LoadBitmapW(void* const instance, const std::uint16_t* const bitmap_name) noexcept {
    (void)instance;
    (void)bitmap_name;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x424D50ULL); // 'BMP'
}

TL_MSABI std::uint32_t tl_MapVirtualKeyW(const std::uint32_t code, const std::uint32_t map_type) noexcept {
    (void)map_type;
    return code;
}

TL_MSABI int tl_ScreenToClient(void* const hwnd, void* const point) noexcept {
    (void)hwnd;
    (void)point;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_CreateCaret(void* const hwnd, void* const bitmap, const int width, const int height) noexcept {
    (void)hwnd;
    (void)bitmap;
    (void)width;
    (void)height;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_DestroyCaret() noexcept {
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetCaretPos(const int x, const int y) noexcept {
    (void)x;
    (void)y;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_ShowCaret(void* const hwnd) noexcept {
    (void)hwnd;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_HideCaret(void* const hwnd) noexcept {
    (void)hwnd;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetCaretPos(void* const point) noexcept {
    if (point != nullptr && mapped_guest_range(point, 8, true)) {
        std::memset(point, 0, 8);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetScrollInfo(void* const hwnd, const int bar, const void* const scroll_info, const int redraw) noexcept {
    (void)hwnd;
    (void)bar;
    (void)scroll_info;
    (void)redraw;
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI int tl_GetScrollInfo(void* const hwnd, const int bar, void* const scroll_info) noexcept {
    (void)hwnd;
    (void)bar;
    (void)scroll_info;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_ShowScrollBar(void* const hwnd, const int bar, const int show) noexcept {
    (void)hwnd;
    (void)bar;
    (void)show;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_EnableScrollBar(void* const hwnd, const std::uint32_t flags, const std::uint32_t arrows) noexcept {
    (void)hwnd;
    (void)flags;
    (void)arrows;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetScrollPos(void* const hwnd, const int bar, const int pos, const int redraw) noexcept {
    (void)hwnd;
    (void)bar;
    (void)pos;
    (void)redraw;
    return pos;
}

TL_MSABI int tl_GetScrollPos(void* const hwnd, const int bar) noexcept {
    (void)hwnd;
    (void)bar;
    return 0;
}

TL_MSABI int tl_SetScrollRange(void* const hwnd, const int bar, const int min_pos, const int max_pos, const int redraw) noexcept {
    (void)hwnd;
    (void)bar;
    (void)min_pos;
    (void)max_pos;
    (void)redraw;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetScrollRange(void* const hwnd, const int bar, int* const min_pos, int* const max_pos) noexcept {
    (void)hwnd;
    (void)bar;
    if (min_pos != nullptr && mapped_guest_range(min_pos, sizeof(int), true)) {
        *min_pos = 0;
    }
    if (max_pos != nullptr && mapped_guest_range(max_pos, sizeof(int), true)) {
        *max_pos = 100;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetSysColors(const int count, const int* const elements, const std::uint32_t* const colors) noexcept {
    (void)count;
    (void)elements;
    (void)colors;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetDpiForSystem() noexcept {
    return 96;
}

TL_MSABI int tl_SetProcessDpiAwarenessContext(void* const dpi_context) noexcept {
    (void)dpi_context;
    return 1;
}

TL_MSABI int tl_SetProcessDPIAware() noexcept {
    return 1;
}

TL_MSABI int tl_GetSystemMetricsForDpi(const int index, const std::uint32_t dpi) noexcept {
    (void)dpi;
    return tl_GetSystemMetrics(index);
}

TL_MSABI void* tl_CreateIconIndirect(const void* const icon_info) noexcept {
    (void)icon_info;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x49434F4EULL); // 'ICON'
}

TL_MSABI int tl_GetIconInfo(void* const icon, void* const icon_info) noexcept {
    (void)icon;
    if (icon_info != nullptr && mapped_guest_range(icon_info, 32, true)) {
        std::memset(icon_info, 0, 32);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetIconInfoExW(void* const icon, void* const icon_info_ex) noexcept {
    (void)icon;
    if (icon_info_ex != nullptr && mapped_guest_range(icon_info_ex, 40, true)) {
        std::memset(icon_info_ex, 0, 40);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_DrawIcon(void* const hdc, const int x, const int y, void* const icon) noexcept {
    (void)hdc;
    (void)x;
    (void)y;
    (void)icon;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CopyIcon(void* const icon) noexcept {
    return icon;
}

TL_MSABI int tl_DrawEdge(void* const hdc, void* const rect, const std::uint32_t edge, const std::uint32_t flags) noexcept {
    (void)hdc;
    (void)rect;
    (void)edge;
    (void)flags;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_DrawFrameControl(void* const hdc, void* const rect, const std::uint32_t type, const std::uint32_t state) noexcept {
    (void)hdc;
    (void)rect;
    (void)type;
    (void)state;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_FrameRect(void* const hdc, const void* const rect, void* const brush) noexcept {
    (void)hdc;
    (void)rect;
    (void)brush;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_InvertRect(void* const hdc, const void* const rect) noexcept {
    (void)hdc;
    (void)rect;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetUpdateRect(void* const hwnd, void* const rect, const int erase) noexcept {
    (void)hwnd;
    (void)erase;
    if (rect != nullptr && mapped_guest_range(rect, 16, true)) {
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(rect) + 0) = 0;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(rect) + 4) = 0;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(rect) + 8) = 1024;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(rect) + 12) = 768;
    }
    return 1;
}

TL_MSABI int tl_GetUpdateRgn(void* const hwnd, void* const rgn, const int erase) noexcept {
    (void)hwnd;
    (void)rgn;
    (void)erase;
    return 2; // SIMPLEREGION
}

TL_MSABI int tl_InvalidateRgn(void* const hwnd, void* const rgn, const int erase) noexcept {
    (void)hwnd;
    (void)rgn;
    (void)erase;
    return 1;
}

TL_MSABI int tl_ValidateRgn(void* const hwnd, void* const rgn) noexcept {
    (void)hwnd;
    (void)rgn;
    return 1;
}

TL_MSABI int tl_GetUserObjectInformationW(void* const handle, const int index,
                                          void* const info, const std::uint32_t length,
                                          std::uint32_t* const length_needed) noexcept {
    (void)handle;
    (void)index;
    if (length_needed != nullptr && mapped_guest_range(length_needed, sizeof(std::uint32_t), true)) {
        *length_needed = sizeof(std::uint32_t);
    }
    if (info != nullptr && length >= sizeof(std::uint32_t) && mapped_guest_range(info, sizeof(std::uint32_t), true)) {
        *reinterpret_cast<std::uint32_t*>(info) = 1; // WSF_VISIBLE
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_EnumDisplayDevicesA(const char* const device, const std::uint32_t dev_num,
                                    void* const display_device, const std::uint32_t flags) noexcept {
    (void)device;
    (void)flags;
    if (dev_num > 0 || display_device == nullptr || !mapped_guest_range(display_device, 40, true)) {
        set_last_error(abi::kErrorSuccess);
        return 0;
    }
    // DISPLAY_DEVICEA: cb(4), DeviceName[32], DeviceString[128], StateFlags(4), DeviceID[128], DeviceKey[128]
    struct DummyDisplayDeviceA {
        std::uint32_t cb;
        char DeviceName[32];
        char DeviceString[128];
        std::uint32_t StateFlags;
        char DeviceID[128];
        char DeviceKey[128];
    }* dd = reinterpret_cast<DummyDisplayDeviceA*>(display_device);
    const std::uint32_t cb = dd->cb;
    std::memset(display_device, 0, std::min<std::size_t>(cb, sizeof(DummyDisplayDeviceA)));
    dd->cb = cb;
    std::strncpy(dd->DeviceName, "\\\\.\\DISPLAY1", sizeof(dd->DeviceName) - 1);
    std::strncpy(dd->DeviceString, "Generic PnP Monitor", sizeof(dd->DeviceString) - 1);
    dd->StateFlags = 1 | 4; // DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetCaretBlinkTime() noexcept {
    return 530; // standard 530 ms
}

TL_MSABI std::uint32_t tl_GetDoubleClickTime() noexcept {
    return 500; // standard 500 ms
}

TL_MSABI void* tl_GetSysColorBrush(const int index) noexcept {
    (void)index;
    return tl_GetStockObject(0); // WHITE_BRUSH
}

TL_MSABI void* tl_LoadImageA(void* const instance, const char* const name, const std::uint32_t type, const int cx, const int cy, const std::uint32_t load) noexcept {
    (void)instance;
    (void)name;
    (void)type;
    (void)cx;
    (void)cy;
    (void)load;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x494D4147ULL); // 'IMAG'
}

TL_MSABI int tl_OffsetRect(void* const rect, const int dx, const int dy) noexcept {
    if (rect != nullptr && mapped_guest_range(rect, 16, true)) {
        auto* const r = reinterpret_cast<std::int32_t*>(rect);
        r[0] += dx; // left
        r[1] += dy; // top
        r[2] += dx; // right
        r[3] += dy; // bottom
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    set_last_error(abi::kErrorInvalidParameter);
    return 0;
}

TL_MSABI int tl_SystemParametersInfoA(const std::uint32_t action, const std::uint32_t param1, void* const param2, const std::uint32_t winini) noexcept {
    return tl_SystemParametersInfoW(action, param1, param2, winini);
}

TL_MSABI int tl_ValidateRect(void* const hWnd, const void* const lpRect) noexcept {
    (void)hWnd;
    (void)lpRect;
    return 1;
}

TL_MSABI int tl_DestroyCursor(void* const hCursor) noexcept {
    (void)hCursor;
    return 1;
}

TL_MSABI void* tl_MonitorFromPoint(const int x, const int y, const std::uint32_t dwFlags) noexcept {
    (void)x;
    (void)y;
    (void)dwFlags;
    return reinterpret_cast<void*>(0x10001);
}

TL_MSABI void* tl_MonitorFromRect(const void* const lprc, const std::uint32_t dwFlags) noexcept {
    (void)lprc;
    (void)dwFlags;
    return reinterpret_cast<void*>(0x10001);
}

TL_MSABI int tl_GetMonitorInfoW(void* const hMonitor, void* const lpmi) noexcept {
    (void)hMonitor;
    if (lpmi != nullptr && mapped_guest_range(lpmi, 40, true)) {
        std::memset(lpmi, 0, 40);
        *reinterpret_cast<std::uint32_t*>(lpmi) = 40;
        auto* rects = reinterpret_cast<std::int32_t*>(static_cast<char*>(lpmi) + 4);
        rects[0] = 0; rects[1] = 0; rects[2] = 1920; rects[3] = 1080;
        rects[4] = 0; rects[5] = 0; rects[6] = 1920; rects[7] = 1080;
        rects[8] = 1;
    }
    return 1;
}

TL_MSABI int tl_DrawTextExW(void* const hdc, wchar_t* const lpchText, const int cchText, void* const lprc, const std::uint32_t format, void* const lpdtp) noexcept {
    (void)format;
    (void)lpdtp;
    return tl_DrawTextW(hdc, reinterpret_cast<const std::uint16_t*>(lpchText), cchText, lprc, format);
}

TL_MSABI int tl_IsCharLowerW(const wchar_t ch) noexcept {
    return std::iswlower(static_cast<wint_t>(ch)) != 0 ? 1 : 0;
}

TL_MSABI int tl_IsCharAlphaNumericW(const wchar_t ch) noexcept {
    return std::iswalnum(static_cast<wint_t>(ch)) != 0 ? 1 : 0;
}

TL_MSABI int tl_IsCharAlphaW(const wchar_t ch) noexcept {
    return std::iswalpha(static_cast<wint_t>(ch)) != 0 ? 1 : 0;
}

TL_MSABI int tl_InflateRect(void* const lprc, const int dx, const int dy) noexcept {
    if (lprc == nullptr || !mapped_guest_range(lprc, 16, true)) return 0;
    auto* const r = reinterpret_cast<std::int32_t*>(lprc);
    r[0] -= dx;
    r[1] -= dy;
    r[2] += dx;
    r[3] += dy;
    return 1;
}

TL_MSABI int tl_IntersectRect(void* const lprcDst, const void* const lprcSrc1, const void* const lprcSrc2) noexcept {
    if (lprcDst == nullptr || lprcSrc1 == nullptr || lprcSrc2 == nullptr ||
        !mapped_guest_range(lprcDst, 16, true) ||
        !mapped_guest_range(lprcSrc1, 16, false) ||
        !mapped_guest_range(lprcSrc2, 16, false)) return 0;
    const auto* const s1 = reinterpret_cast<const std::int32_t*>(lprcSrc1);
    const auto* const s2 = reinterpret_cast<const std::int32_t*>(lprcSrc2);
    auto* const d = reinterpret_cast<std::int32_t*>(lprcDst);
    d[0] = std::max(s1[0], s2[0]);
    d[1] = std::max(s1[1], s2[1]);
    d[2] = std::min(s1[2], s2[2]);
    d[3] = std::min(s1[3], s2[3]);
    if (d[0] >= d[2] || d[1] >= d[3]) {
        std::memset(lprcDst, 0, 16);
        return 0;
    }
    return 1;
}

TL_MSABI int tl_SetRectEmpty(void* const lprc) noexcept {
    if (lprc == nullptr || !mapped_guest_range(lprc, 16, true)) return 0;
    std::memset(lprc, 0, 16);
    return 1;
}

TL_MSABI int tl_GetComboBoxInfo(void* const hwndCombo, void* const pcbi) noexcept {
    (void)hwndCombo;
    if (pcbi != nullptr && mapped_guest_range(pcbi, 64, true)) {
        std::memset(pcbi, 0, 64);
        *reinterpret_cast<std::uint32_t*>(pcbi) = 64;
    }
    return 1;
}

TL_MSABI int tl_wsprintfW(wchar_t* const lpOut, const wchar_t* const lpFmt, ...) noexcept {
    if (lpOut == nullptr || lpFmt == nullptr) return 0;
    std::size_t i = 0;
    while (lpFmt[i] != 0) {
        lpOut[i] = lpFmt[i];
        ++i;
    }
    lpOut[i] = 0;
    return static_cast<int>(i);
}

TL_MSABI void* tl_GetDCEx(void* const hWnd, void* const hrgnClip, const std::uint32_t flags) noexcept {
    (void)hrgnClip;
    (void)flags;
    return tl_GetDC(hWnd);
}

TL_MSABI int tl_EnumDisplaySettingsA(const char* const device, const std::uint32_t mode, void* const dev_mode) noexcept {
    (void)device;
    if (mode != 0) {
        return 0;
    }
    if (dev_mode != nullptr && mapped_guest_range(dev_mode, 124, true)) {
        std::memset(dev_mode, 0, 124);
        *reinterpret_cast<std::uint32_t*>(dev_mode) = 124;
        // dmPelsWidth/Height at offset 104/108
        *reinterpret_cast<std::uint32_t*>(static_cast<char*>(dev_mode) + 104) = 1920;
        *reinterpret_cast<std::uint32_t*>(static_cast<char*>(dev_mode) + 108) = 1080;
        *reinterpret_cast<std::uint32_t*>(static_cast<char*>(dev_mode) + 112) = 32;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_IsRectEmpty(const void* const rect) noexcept {
    if (rect == nullptr || !mapped_guest_range(rect, 16, false)) {
        return 1;
    }
    const auto* r = static_cast<const std::int32_t*>(rect);
    return (r[2] <= r[0] || r[3] <= r[1]) ? 1 : 0;
}

TL_MSABI int tl_SubtractRect(void* const dest, const void* const src1, const void* const src2) noexcept {
    if (dest == nullptr || src1 == nullptr || src2 == nullptr ||
        !mapped_guest_range(dest, 16, true) || !mapped_guest_range(src1, 16, false) ||
        !mapped_guest_range(src2, 16, false)) {
        return 0;
    }
    const auto* s1 = static_cast<const std::int32_t*>(src1);
    const auto* s2 = static_cast<const std::int32_t*>(src2);
    auto* d = static_cast<std::int32_t*>(dest);
    // Simplificado: dest = src1 - intersecção
    d[0] = s1[0];
    d[1] = s1[1];
    d[2] = s1[2];
    d[3] = s1[3];
    // Se há intersecção, retorna 1
    const int left = std::max(s1[0], s2[0]);
    const int top = std::max(s1[1], s2[1]);
    const int right = std::min(s1[2], s2[2]);
    const int bottom = std::min(s1[3], s2[3]);
    return (left < right && top < bottom) ? 1 : 0;
}

}  // extern "C"
}  // namespace tradutorlinux
