#include "tradutorlinux/runtime/winapi.hpp"
#include "runtime_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace tradutorlinux {

extern "C" {

// Tokens opacos para stock objects do GDI: o próprio endereço serve de handle
// e o deslocamento identifica o objeto. Stock objects não são liberados.
TL_MSABI void* tl_GetStockObject(const int object) noexcept {
    if (object < 0 || object >= 24) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("GetStockObject", "object", "stock object fora da faixa suportada");
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "GetStockObject"},
        diagnostics::TraceField{"object", std::to_string(object)},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"mechanism", "token"},
    };
    runtime_trace("GetStockObject", fields, 4);
    return kStockObjectTokens + object;
}

TL_MSABI int tl_TextOut(const void* const dc, const int x, const int y,
                        const char* const text, const int length) noexcept {
    if (text == nullptr || length < 0 ||
        (length > 0 &&
         !mapped_guest_range(text, static_cast<std::size_t>(length), false))) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("TextOut", "text", "ponteiro ou comprimento inválido");
        return 0;
    }
    WindowSlot* const slot = find_window_slot(dc);
    if (slot == nullptr || slot->native == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("TextOut", "dc", "HDC inválido");
        return 0;
    }
    if (length > 0) {
        gui::draw_text_len(slot->native, text, length, x, y);
        gui::flush_window(slot->native);
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "TextOut"},
        diagnostics::TraceField{"x", std::to_string(x)},
        diagnostics::TraceField{"y", std::to_string(y)},
        diagnostics::TraceField{"length", std::to_string(length)},
    };
    runtime_trace("TextOut", fields, 4);
    return 1;
}

TL_MSABI int tl_FillRect(const void* const dc,
                         const void* const rect, const void* const brush) noexcept {
    if (rect == nullptr || !mapped_guest_range(rect, sizeof(abi::GuestRect), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("FillRect", "rect", "ponteiro RECT inválido");
        return 0;
    }
    WindowSlot* const slot = find_window_slot(dc);
    if (slot == nullptr || slot->native == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("FillRect", "dc", "HDC inválido");
        return 0;
    }
    const int brush_index = stock_object_index(brush);
    if (brush_index < 0 || brush_index >= 6) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("FillRect", "brush", "brush deve ser um stock object WHITE..NULL");
        return 0;
    }
    const auto* const rc = static_cast<const abi::GuestRect*>(rect);
    const int width = rc->right - rc->left;
    const int height = rc->bottom - rc->top;
    if (width > 0 && height > 0 && brush_index != 5) {
        gui::fill_rectangle(slot->native, rc->left, rc->top, width, height, brush_index);
        gui::flush_window(slot->native);
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "FillRect"},
        diagnostics::TraceField{"brush", std::to_string(brush_index)},
        diagnostics::TraceField{"rect", std::to_string(rc->left) + "," + std::to_string(rc->top) +
                                     "-" + std::to_string(rc->right) + "," +
                                     std::to_string(rc->bottom)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("FillRect", fields, 4);
    return 1;
}

TL_MSABI void* tl_CreateFontA(int height, int width, int escapement, int orientation, int weight,
                              std::uint32_t italic, std::uint32_t underline,
                              std::uint32_t strikeout, std::uint32_t charset,
                              std::uint32_t output_precision, std::uint32_t clip_precision,
                              std::uint32_t quality, std::uint32_t pitch_and_family,
                              const char* face_name) noexcept {
    (void)height;
    (void)width;
    (void)escapement;
    (void)orientation;
    (void)weight;
    (void)italic;
    (void)underline;
    (void)strikeout;
    (void)charset;
    (void)output_precision;
    (void)clip_precision;
    (void)quality;
    (void)pitch_and_family;
    (void)face_name;
    static std::array<char, 16> tokens{};
    for (char& token : tokens) {
        if (token == 0) {
            token = 1;
            set_last_error(abi::kErrorSuccess);
            return &token;
        }
    }
    set_last_error(abi::kErrorNotEnoughMemory);
    return nullptr;
}

TL_MSABI void* tl_CreateSolidBrush(const std::uint32_t color) noexcept {
    (void)color;
    static std::array<char, 16> tokens{};
    for (char& token : tokens) {
        if (token == 0) {
            token = 1;
            set_last_error(abi::kErrorSuccess);
            return &token;
        }
    }
    set_last_error(abi::kErrorNotEnoughMemory);
    return nullptr;
}

TL_MSABI int tl_DeleteObject(const void* object) noexcept {
    (void)object;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_SetBkColor(const void* dc, const std::uint32_t color) noexcept {
    (void)dc;
    set_last_error(abi::kErrorSuccess);
    return color;
}

TL_MSABI std::uint32_t tl_SetTextColor(const void* dc, const std::uint32_t color) noexcept {
    (void)dc;
    set_last_error(abi::kErrorSuccess);
    return color;
}

TL_MSABI int tl_GetDeviceCaps(const void* dc, const int index) noexcept {
    (void)dc;
    switch (index) {
        case 8: return 1920; // HORZRES
        case 10: return 1080; // VERTRES
        case 12: return 32;   // BITSPIXEL
        case 14: return 1;    // PLANES
        case 88: return 96;   // LOGPIXELSX
        case 90: return 96;   // LOGPIXELSY
        case 115: return 24;  // SIZEPALETTE
        case 116: return 20;  // NUMRESERVED
        case 117: return 16777216; // COLORRES
        default: return 0;
    }
}

TL_MSABI void* tl_CreateCompatibleDC(const void* dc) noexcept {
    (void)dc;
    static char g_compat_dc_token = 0;
    set_last_error(abi::kErrorSuccess);
    return &g_compat_dc_token;
}

TL_MSABI int tl_DeleteDC(const void* dc) noexcept {
    (void)dc;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreateCompatibleBitmap(const void* dc, const int width, const int height) noexcept {
    (void)dc;
    (void)width;
    (void)height;
    static char g_compat_bitmap_token = 0;
    set_last_error(abi::kErrorSuccess);
    return &g_compat_bitmap_token;
}

TL_MSABI int tl_BitBlt(const void* dest_dc, const int x, const int y,
                       const int width, const int height,
                       const void* src_dc, const int src_x, const int src_y,
                       const std::uint32_t rop) noexcept {
    (void)dest_dc;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)src_dc;
    (void)src_x;
    (void)src_y;
    (void)rop;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_SelectObject(const void* dc, const void* object) noexcept {
    (void)dc;
    set_last_error(abi::kErrorSuccess);
    return const_cast<void*>(object);
}

TL_MSABI int tl_SetBkMode(const void* dc, const int mode) noexcept {
    (void)dc;
    set_last_error(abi::kErrorSuccess);
    return mode;
}

TL_MSABI void* tl_CreateFontIndirectA(const void* log_font) noexcept {
    (void)log_font;
    static char g_font_token = 0;
    set_last_error(abi::kErrorSuccess);
    return &g_font_token;
}

TL_MSABI void* tl_CreateFontIndirectW(const void* log_font) noexcept {
    (void)log_font;
    static char g_font_w_token = 0;
    set_last_error(abi::kErrorSuccess);
    return &g_font_w_token;
}

TL_MSABI void* tl_CreateFontW(int height, int width, int escapement, int orientation, int weight,
                              std::uint32_t italic, std::uint32_t underline,
                              std::uint32_t strikeout, std::uint32_t charset,
                              std::uint32_t output_precision, std::uint32_t clip_precision,
                              std::uint32_t quality, std::uint32_t pitch_and_family,
                              const std::uint16_t* face_name) noexcept {
    (void)height;
    (void)width;
    (void)escapement;
    (void)orientation;
    (void)weight;
    (void)italic;
    (void)underline;
    (void)strikeout;
    (void)charset;
    (void)output_precision;
    (void)clip_precision;
    (void)quality;
    (void)pitch_and_family;
    (void)face_name;
    static char g_font_w2_token = 0;
    if (face_name != nullptr && !mapped_guest_wstring(face_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return &g_font_w2_token;
}

TL_MSABI std::uint32_t tl_SetDCBrushColor(const void* dc, std::uint32_t color) noexcept {
    (void)dc;
    (void)color;
    set_last_error(abi::kErrorSuccess);
    return 0x00000000; // previous black
}

TL_MSABI std::uint32_t tl_SetDCPenColor(const void* dc, std::uint32_t color) noexcept {
    (void)dc;
    (void)color;
    set_last_error(abi::kErrorSuccess);
    return 0x00000000;
}

}  // extern "C"

}  // namespace tradutorlinux
