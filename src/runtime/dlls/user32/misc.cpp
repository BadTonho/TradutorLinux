#include "user32_internal.hpp"
namespace tradutorlinux {
extern "C" {

TL_MSABI void* tl_BeginPaint(const void* const window, void* const paint_struct) noexcept {
    if (!user32_gui_thread_allowed("BeginPaint")) {
        return nullptr;
    }
    if (paint_struct == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr || window_drawing_target(window).native == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }
    abi::GuestPaintStruct ps{};
    ps.hdc = const_cast<void*>(window);
    ps.f_erase = 1;
    ps.rc_paint = {0, 0, slot->width, slot->height};
    if (!write_guest_value(paint_struct, ps)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    slot->painting = true;
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "BeginPaint"},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("BeginPaint", fields, 2);
    set_last_error(abi::kErrorSuccess);
    return ps.hdc;
}

TL_MSABI int tl_EndPaint(const void* const window, const void* const paint_struct) noexcept {
    if (!user32_gui_thread_allowed("EndPaint")) {
        return 0;
    }
    abi::GuestPaintStruct ps{};
    if (paint_struct == nullptr || !read_guest_value(paint_struct, ps)) {
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
    if (point == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::array<std::int32_t, 2> coordinates{};
    if (runtime::write_guest_memory(point, coordinates.data(), sizeof(coordinates)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_InvalidateRect(const void* window, const void* rect, int erase) noexcept {
    if (!user32_gui_thread_allowed("InvalidateRect")) {
        return 0;
    }
    (void)erase;
    abi::GuestRect rect_copy{};
    if (rect != nullptr && !read_guest_value(rect, rect_copy)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    const bool paint_already_queued = std::any_of(
        slot->queued_messages.begin(), slot->queued_messages.end(),
        [](const abi::GuestMsg& message) { return message.message == abi::kWmPaint; });
    if (!paint_already_queued) {
        queue_window_message(*slot, abi::kWmPaint, 0, 0);
    }
    const WindowDrawingTarget target = window_drawing_target(window);
    if (target.native != nullptr) {
        gui::platform::flush_window(target.native);
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
    std::u16string name_copy;
    if (name != nullptr && !runtime::copy_guest_wstring(name, 4096U, name_copy)) {
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
    std::u16string name_copy;
    if (name != nullptr && !runtime::copy_guest_wstring(name, 4096U, name_copy)) {
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
    if (buffer == nullptr || buffer_max <= 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::vector<std::uint16_t> wide(static_cast<std::size_t>(buffer_max), 0);
    const int length = tl_LoadStringW(instance, id, wide.data(), buffer_max);
    if (length <= 0) {
        const char zero = '\0';
        if (runtime::write_guest_memory(buffer, &zero, sizeof(zero)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            set_last_error(abi::kErrorInvalidParameter);
        }
        return 0;
    }
    const std::string utf8 = util::wide_to_utf8(wide.data(), static_cast<std::size_t>(length));
    const std::size_t copied = std::min(utf8.size(), static_cast<std::size_t>(buffer_max - 1));
    std::vector<char> output(copied + 1U, '\0');
    std::copy_n(utf8.data(), copied, output.data());
    if (runtime::write_guest_memory(buffer, output.data(), output.size()).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(copied);
}

TL_MSABI int tl_LoadStringW(void* instance, const std::uint32_t id, std::uint16_t* buffer, const int buffer_max) noexcept {
    if (buffer == nullptr || buffer_max <= 0) {
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
        const std::uint16_t zero = 0;
        if (runtime::write_guest_memory(buffer, &zero, sizeof(zero)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        set_last_error(abi::kErrorResourceNameNotFound);
        return 0;
    }

    const std::size_t unit_count = byte_size / sizeof(std::uint16_t);
    std::size_t offset = 0;
    const std::size_t index = id % 16U;
    for (std::size_t current = 0; current <= index; ++current) {
        if (offset >= unit_count) {
            const std::uint16_t zero = 0;
            if (runtime::write_guest_memory(buffer, &zero, sizeof(zero)).status !=
                runtime::GuestMemoryAccessStatus::Success) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
            set_last_error(abi::kErrorResourceDataNotFound);
            return 0;
        }
        const std::size_t length = data[offset++];
        if (length > unit_count - offset) {
            const std::uint16_t zero = 0;
            if (runtime::write_guest_memory(buffer, &zero, sizeof(zero)).status !=
                runtime::GuestMemoryAccessStatus::Success) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
            set_last_error(abi::kErrorResourceDataNotFound);
            return 0;
        }
        if (current == index) {
            const std::size_t copied = std::min(length, static_cast<std::size_t>(buffer_max - 1));
            std::vector<std::uint16_t> output(copied + 1U, 0);
            std::copy_n(data + offset, copied, output.data());
            if (runtime::write_guest_memory(buffer, output.data(), output.size() * sizeof(output[0])).status !=
                runtime::GuestMemoryAccessStatus::Success) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
            set_last_error(abi::kErrorSuccess);
            return static_cast<int>(copied);
        }
        offset += length;
    }
    const std::uint16_t zero = 0;
    if (runtime::write_guest_memory(buffer, &zero, sizeof(zero)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorResourceNameNotFound);
    return 0;
}

TL_MSABI int tl_PtInRect(const void* const rect, const std::int32_t x, const std::int32_t y) noexcept {
    abi::GuestRect r{};
    if (rect == nullptr || !read_guest_value(rect, r)) {
        return 0;
    }
    return (x >= r.left && x < r.right && y >= r.top && y < r.bottom) ? 1 : 0;
}

TL_MSABI int tl_CopyRect(void* const dest_rect, const void* const src_rect) noexcept {
    abi::GuestRect source{};
    if (dest_rect == nullptr || src_rect == nullptr || !read_guest_value(src_rect, source) ||
        !write_guest_value(dest_rect, source)) {
        return 0;
    }
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
    std::u16string copy;
    if (!runtime::copy_guest_wstring(str, 65535U, copy)) return str;
    for (char16_t& value : copy) {
        if (value >= u'a' && value <= u'z') {
            value = static_cast<char16_t>(value - u'a' + u'A');
        }
    }
    copy.push_back(0);
    static_cast<void>(runtime::write_guest_memory(str, copy.data(), copy.size() * sizeof(copy[0])));
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
    std::u16string copy;
    if (!runtime::copy_guest_wstring(str, 65535U, copy)) return str;
    for (char16_t& value : copy) {
        if (value >= u'A' && value <= u'Z') {
            value = static_cast<char16_t>(value - u'A' + u'a');
        }
    }
    copy.push_back(0);
    static_cast<void>(runtime::write_guest_memory(str, copy.data(), copy.size() * sizeof(copy[0])));
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
    std::string start_copy;
    if (!runtime::copy_guest_cstring(start, 65535U, start_copy)) {
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
    char previous = 0;
    if (runtime::read_guest_memory(current - 1, &previous, sizeof(previous)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
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
    if (text == nullptr || rect == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string text_copy;
    if (count < 0) {
        if (!runtime::copy_guest_cstring(text, 32768U, text_copy)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    } else {
        text_copy.resize(static_cast<std::size_t>(count));
        if (count > 0 && runtime::read_guest_memory(text, text_copy.data(), text_copy.size()).status !=
                              runtime::GuestMemoryAccessStatus::Success) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    const std::size_t len = text_copy.size();
    abi::GuestRect r{};
    if (!read_guest_value(rect, r)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    constexpr int kLineHeight = 16;
    constexpr int kCharWidth = 8;
    if ((format & kDtCalcRect) != 0) {
        r.right = r.left + static_cast<std::int32_t>(len * kCharWidth);
        r.bottom = r.top + kLineHeight;
        if (!write_guest_value(rect, r)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        return kLineHeight;
    }
    if (dc != nullptr) {
        (void)tl_TextOut(dc, r.left, r.top, text_copy.data(), static_cast<int>(len));
    }
    return kLineHeight;
}

TL_MSABI int tl_DrawTextW(const void* const dc, const std::uint16_t* const text, const int count,
                          void* const rect, const std::uint32_t format) noexcept {
    if (text == nullptr || rect == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::u16string text_copy;
    if (count < 0) {
        if (!runtime::copy_guest_wstring(text, 32768U, text_copy)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    } else {
        text_copy.resize(static_cast<std::size_t>(count));
        if (count > 0 && runtime::read_guest_memory(text, text_copy.data(), text_copy.size() * sizeof(text_copy[0])).status !=
                              runtime::GuestMemoryAccessStatus::Success) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    const std::size_t len = text_copy.size();
    abi::GuestRect r{};
    if (!read_guest_value(rect, r)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    constexpr int kLineHeight = 16;
    constexpr int kCharWidth = 8;
    if ((format & kDtCalcRect) != 0) {
        r.right = r.left + static_cast<std::int32_t>(len * kCharWidth);
        r.bottom = r.top + kLineHeight;
        if (!write_guest_value(rect, r)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        return kLineHeight;
    }
    if (dc != nullptr) {
        const std::string utf8 = util::wide_to_utf8(
            reinterpret_cast<const std::uint16_t*>(text_copy.data()), len);
        (void)tl_TextOut(dc, r.left, r.top, utf8.c_str(), static_cast<int>(utf8.size()));
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
    if (point != nullptr) {
        const std::array<std::byte, 8> zeroes{};
        static_cast<void>(runtime::write_guest_memory(point, zeroes.data(), zeroes.size()));
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
    if (min_pos != nullptr) {
        static_cast<void>(write_guest_value(min_pos, 0));
    }
    if (max_pos != nullptr) {
        static_cast<void>(write_guest_value(max_pos, 100));
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
    if (icon_info != nullptr) {
        const std::array<std::byte, 32> zeroes{};
        static_cast<void>(runtime::write_guest_memory(icon_info, zeroes.data(), zeroes.size()));
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetIconInfoExW(void* const icon, void* const icon_info_ex) noexcept {
    (void)icon;
    if (icon_info_ex != nullptr) {
        const std::array<std::byte, 40> zeroes{};
        static_cast<void>(runtime::write_guest_memory(icon_info_ex, zeroes.data(), zeroes.size()));
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
    if (rect != nullptr) {
        const std::array<std::int32_t, 4> update_rect{0, 0, 1024, 768};
        static_cast<void>(runtime::write_guest_memory(rect, update_rect.data(), sizeof(update_rect)));
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
    if (length_needed != nullptr) {
        static_cast<void>(write_guest_value(length_needed, static_cast<std::uint32_t>(sizeof(std::uint32_t))));
    }
    if (info != nullptr && length >= sizeof(std::uint32_t)) {
        static_cast<void>(write_guest_value(static_cast<std::uint32_t*>(info), 1U)); // WSF_VISIBLE
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_EnumDisplayDevicesA(const char* const device, const std::uint32_t dev_num,
                                    void* const display_device, const std::uint32_t flags) noexcept {
    (void)device;
    (void)flags;
    if (dev_num > 0 || display_device == nullptr) {
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
    } display{};
    std::uint32_t cb = 0;
    if (!read_guest_value(display_device, cb)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::memset(&display, 0, sizeof(display));
    display.cb = cb;
    std::strncpy(display.DeviceName, "\\\\.\\DISPLAY1", sizeof(display.DeviceName) - 1);
    std::strncpy(display.DeviceString, "Generic PnP Monitor", sizeof(display.DeviceString) - 1);
    display.StateFlags = 1 | 4; // DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE
    const std::size_t output_size = std::min<std::size_t>(cb, sizeof(display));
    if (output_size == 0 || runtime::write_guest_memory(display_device, &display, output_size).status !=
                                runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
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
    abi::GuestRect value{};
    if (rect != nullptr && read_guest_value(rect, value)) {
        value.left += dx;
        value.top += dy;
        value.right += dx;
        value.bottom += dy;
        if (!write_guest_value(rect, value)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
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
    if (lpmi != nullptr) {
        const std::array<std::int32_t, 10> monitor_info{
            40, 0, 0, 1920, 1080, 0, 0, 1920, 1080, 1};
        static_cast<void>(runtime::write_guest_memory(lpmi, monitor_info.data(), sizeof(monitor_info)));
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
    abi::GuestRect value{};
    if (lprc == nullptr || !read_guest_value(lprc, value)) return 0;
    value.left -= dx;
    value.top -= dy;
    value.right += dx;
    value.bottom += dy;
    if (!write_guest_value(lprc, value)) return 0;
    return 1;
}

TL_MSABI int tl_IntersectRect(void* const lprcDst, const void* const lprcSrc1, const void* const lprcSrc2) noexcept {
    abi::GuestRect source1{};
    abi::GuestRect source2{};
    if (lprcDst == nullptr || lprcSrc1 == nullptr || lprcSrc2 == nullptr ||
        !read_guest_value(lprcSrc1, source1) || !read_guest_value(lprcSrc2, source2)) return 0;
    abi::GuestRect destination{
        std::max(source1.left, source2.left), std::max(source1.top, source2.top),
        std::min(source1.right, source2.right), std::min(source1.bottom, source2.bottom)};
    if (destination.left >= destination.right || destination.top >= destination.bottom) {
        destination = {};
        if (!write_guest_value(lprcDst, destination)) return 0;
        return 0;
    }
    if (!write_guest_value(lprcDst, destination)) return 0;
    return 1;
}

TL_MSABI int tl_SetRectEmpty(void* const lprc) noexcept {
    const abi::GuestRect empty{};
    return lprc != nullptr && write_guest_value(lprc, empty) ? 1 : 0;
}

TL_MSABI int tl_GetComboBoxInfo(void* const hwndCombo, void* const pcbi) noexcept {
    (void)hwndCombo;
    if (pcbi != nullptr) {
        std::array<std::byte, 64> combo_info{};
        const std::uint32_t size = 64;
        std::memcpy(combo_info.data(), &size, sizeof(size));
        static_cast<void>(runtime::write_guest_memory(pcbi, combo_info.data(), combo_info.size()));
    }
    return 1;
}

TL_MSABI int tl_wsprintfW(wchar_t* const lpOut, const wchar_t* const lpFmt, ...) noexcept {
    if (lpOut == nullptr || lpFmt == nullptr) return 0;
    std::u16string format_copy;
    const auto* const guest_format = reinterpret_cast<const std::uint16_t*>(lpFmt);
    if (!runtime::copy_guest_wstring(guest_format, 4096U, format_copy)) {
        return 0;
    }
    format_copy.push_back(0);
    auto* const guest_output = reinterpret_cast<std::uint16_t*>(lpOut);
    if (runtime::write_guest_memory(guest_output, format_copy.data(),
                                    format_copy.size() * sizeof(format_copy[0])).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        return 0;
    }
    return static_cast<int>(format_copy.size() - 1U);
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
    if (dev_mode != nullptr) {
        std::array<std::byte, 124> output{};
        const std::uint32_t size = 124;
        std::memcpy(output.data(), &size, sizeof(size));
        // dmPelsWidth/Height at offset 104/108
        const std::uint32_t width = 1920;
        const std::uint32_t height = 1080;
        const std::uint32_t bits = 32;
        std::memcpy(output.data() + 104, &width, sizeof(width));
        std::memcpy(output.data() + 108, &height, sizeof(height));
        std::memcpy(output.data() + 112, &bits, sizeof(bits));
        if (runtime::write_guest_memory(dev_mode, output.data(), output.size()).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_IsRectEmpty(const void* const rect) noexcept {
    abi::GuestRect value{};
    if (rect == nullptr || !read_guest_value(rect, value)) {
        return 1;
    }
    return (value.right <= value.left || value.bottom <= value.top) ? 1 : 0;
}

TL_MSABI int tl_SubtractRect(void* const dest, const void* const src1, const void* const src2) noexcept {
    abi::GuestRect source1{};
    abi::GuestRect source2{};
    if (dest == nullptr || src1 == nullptr || src2 == nullptr ||
        !read_guest_value(src1, source1) || !read_guest_value(src2, source2)) {
        return 0;
    }
    // Simplificado: dest = src1 - intersecção
    if (!write_guest_value(dest, source1)) return 0;
    // Se há intersecção, retorna 1
    const int left = std::max(source1.left, source2.left);
    const int top = std::max(source1.top, source2.top);
    const int right = std::min(source1.right, source2.right);
    const int bottom = std::min(source1.bottom, source2.bottom);
    return (left < right && top < bottom) ? 1 : 0;
}

}  // extern "C"
}  // namespace tradutorlinux
