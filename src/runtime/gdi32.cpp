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
        gui::platform::draw_text_len(slot->native, text, length, x, y);
        gui::platform::flush_window(slot->native);
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
        gui::platform::fill_rectangle(slot->native, rc->left, rc->top, width, height, brush_index);
        gui::platform::flush_window(slot->native);
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

TL_MSABI void* tl_CreateBitmap(const int width, const int height, const std::uint32_t planes,
                               const std::uint32_t bit_count, const void* bits) noexcept {
    (void)width;
    (void)height;
    (void)planes;
    (void)bit_count;
    (void)bits;
    static char g_bitmap_token = 0;
    set_last_error(abi::kErrorSuccess);
    return &g_bitmap_token;
}

TL_MSABI int tl_StretchBlt(void* dest_dc, const int x_dest, const int y_dest, const int w_dest, const int h_dest,
                           const void* src_dc, const int x_src, const int y_src, const int w_src, const int h_src,
                           const std::uint32_t rop) noexcept {
    (void)dest_dc;
    (void)x_dest;
    (void)y_dest;
    (void)w_dest;
    (void)h_dest;
    (void)src_dc;
    (void)x_src;
    (void)y_src;
    (void)w_src;
    (void)h_src;
    (void)rop;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

struct GuestBitmap {
    std::int32_t bmType{0};
    std::int32_t bmWidth{100};
    std::int32_t bmHeight{100};
    std::int32_t bmWidthBytes{400};
    std::uint16_t bmPlanes{1};
    std::uint16_t bmBitsPixel{32};
    void* bmBits{nullptr};
};

TL_MSABI int tl_GetObjectW(const void* hgdiobj, const int buffer_size, void* object_buffer) noexcept {
    if (hgdiobj == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (object_buffer == nullptr) {
        return static_cast<int>(sizeof(GuestBitmap));
    }
    if (buffer_size <= 0) {
        return 0;
    }
    if (buffer_size >= static_cast<int>(sizeof(GuestBitmap))) {
        GuestBitmap bmp{};
        bmp.bmType = 0;
        bmp.bmWidth = 100;
        bmp.bmHeight = 100;
        bmp.bmWidthBytes = 400;
        bmp.bmPlanes = 1;
        bmp.bmBitsPixel = 32;
        bmp.bmBits = nullptr;
        std::memcpy(object_buffer, &bmp, sizeof(GuestBitmap));
        set_last_error(abi::kErrorSuccess);
        return static_cast<int>(sizeof(GuestBitmap));
    }
    std::memset(object_buffer, 0, static_cast<std::size_t>(buffer_size));
    set_last_error(abi::kErrorSuccess);
    return buffer_size;
}

TL_MSABI void* tl_CreateDIBSection(const void* dc, const void* pbmi, const std::uint32_t usage,
                                   void** ppv_bits, void* section, const std::uint32_t offset) noexcept {
    (void)dc;
    (void)pbmi;
    (void)usage;
    (void)section;
    (void)offset;
    static char g_dib_buffer[4096]{};
    if (ppv_bits != nullptr) {
        *ppv_bits = g_dib_buffer;
    }
    static char g_dib_token = 0;
    set_last_error(abi::kErrorSuccess);
    return &g_dib_token;
}

TL_MSABI int tl_GetTextExtentPoint32W(void* const hdc, const std::uint16_t* const string,
                                      const int length, void* const size) noexcept {
    (void)hdc;
    (void)string;
    struct GuestSize {
        std::int32_t cx;
        std::int32_t cy;
    };
    if (size != nullptr) {
        GuestSize s{};
        s.cx = length > 0 ? length * 8 : 16;
        s.cy = 16;
        std::memcpy(size, &s, sizeof(s));
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_StartDocW(void* const hdc, const void* const doc_info) noexcept {
    (void)hdc;
    (void)doc_info;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_EndDoc(void* const hdc) noexcept {
    (void)hdc;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_StartPage(void* const hdc) noexcept {
    (void)hdc;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_EndPage(void* const hdc) noexcept {
    (void)hdc;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_AbortDoc(void* const hdc) noexcept {
    (void)hdc;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetTextMetricsW(void* const hdc, void* const tm) noexcept {
    (void)hdc;
    if (tm != nullptr && mapped_guest_range(tm, 56, true)) {
        std::memset(tm, 0, 56);
        // Default reasonable character metrics: height 16, width 8
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(tm) + 0) = 16; // tmHeight
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(tm) + 20) = 8; // tmAveCharWidth
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetTextMetricsA(void* const hdc, void* const tm) noexcept {
    return tl_GetTextMetricsW(hdc, tm);
}

TL_MSABI void* tl_CreatePen(const int style, const int width, const std::uint32_t color) noexcept {
    (void)style;
    (void)width;
    (void)color;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x50454E53ULL); // 'PENS'
}

TL_MSABI int tl_ExtTextOutW(void* const hdc, const int x, const int y, const std::uint32_t options,
                            const void* const rect, const std::uint16_t* const string,
                            const std::uint32_t count, const int* const dx) noexcept {
    (void)hdc;
    (void)x;
    (void)y;
    (void)options;
    (void)rect;
    (void)string;
    (void)count;
    (void)dx;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_ExtTextOutA(void* const hdc, const int x, const int y, const std::uint32_t options,
                            const void* const rect, const char* const string,
                            const std::uint32_t count, const int* const dx) noexcept {
    (void)hdc;
    (void)x;
    (void)y;
    (void)options;
    (void)rect;
    (void)string;
    (void)count;
    (void)dx;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_MoveToEx(void* const hdc, const int x, const int y, void* const point) noexcept {
    (void)hdc;
    (void)x;
    (void)y;
    (void)point;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_LineTo(void* const hdc, const int x, const int y) noexcept {
    (void)hdc;
    (void)x;
    (void)y;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_Polyline(void* const hdc, const void* const points, const int count) noexcept {
    (void)hdc;
    (void)points;
    (void)count;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_Polygon(void* const hdc, const void* const points, const int count) noexcept {
    (void)hdc;
    (void)points;
    (void)count;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreateRectRgn(const int left, const int top, const int right, const int bottom) noexcept {
    (void)left;
    (void)top;
    (void)right;
    (void)bottom;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x52454354ULL); // 'RECT'
}

TL_MSABI int tl_CombineRgn(void* const dst, void* const src1, void* const src2, const int mode) noexcept {
    (void)dst;
    (void)src1;
    (void)src2;
    (void)mode;
    set_last_error(abi::kErrorSuccess);
    return 2; // SIMPLEREGION
}

TL_MSABI int tl_SelectClipRgn(void* const hdc, void* const rgn) noexcept {
    (void)hdc;
    (void)rgn;
    set_last_error(abi::kErrorSuccess);
    return 2; // SIMPLEREGION
}

TL_MSABI int tl_GetClipBox(void* const hdc, void* const rect) noexcept {
    (void)hdc;
    if (rect != nullptr && mapped_guest_range(rect, 16, true)) {
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(rect) + 0) = 0;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(rect) + 4) = 0;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(rect) + 8) = 1024;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(rect) + 12) = 768;
    }
    set_last_error(abi::kErrorSuccess);
    return 2; // SIMPLEREGION
}

TL_MSABI int tl_GetCharWidthW(void* const hdc, const std::uint32_t first, const std::uint32_t last,
                              int* const buffer) noexcept {
    (void)hdc;
    if (buffer != nullptr && last >= first) {
        const std::size_t count = static_cast<std::size_t>(last - first + 1);
        if (mapped_guest_range(buffer, count * sizeof(int), true)) {
            for (std::size_t i = 0; i < count; ++i) {
                buffer[i] = 8; // standard 8px mono width
            }
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetCharWidth32W(void* const hdc, const std::uint32_t first, const std::uint32_t last,
                                int* const buffer) noexcept {
    return tl_GetCharWidthW(hdc, first, last, buffer);
}

TL_MSABI int tl_GetTextExtentPoint32A(void* const hdc, const char* const string, const int length,
                                      void* const size) noexcept {
    (void)hdc;
    if (size != nullptr && mapped_guest_range(size, 8, true)) {
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(size) + 0) = (length > 0 ? length : (string ? static_cast<int>(std::strlen(string)) : 0)) * 8;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(size) + 4) = 16;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_SetTextAlign(void* const hdc, const std::uint32_t align) noexcept {
    (void)hdc;
    set_last_error(abi::kErrorSuccess);
    return align;
}

TL_MSABI std::uint32_t tl_GetTextAlign(void* const hdc) noexcept {
    (void)hdc;
    return 0; // TA_LEFT | TA_TOP | TA_NOUPDATECP
}

TL_MSABI int tl_SetROP2(void* const hdc, const int rop2) noexcept {
    (void)hdc;
    return rop2;
}

TL_MSABI std::uint32_t tl_GetSystemPaletteEntries(void* const hdc, const std::uint32_t start,
                                                  const std::uint32_t count, void* const entries) noexcept {
    (void)hdc;
    (void)start;
    (void)entries;
    return count;
}

TL_MSABI void* tl_CreatePatternBrush(void* const hbmp) noexcept {
    (void)hbmp;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x50415442ULL); // 'PATB'
}

TL_MSABI void* tl_CreateHatchBrush(const int style, const std::uint32_t color) noexcept {
    (void)style;
    (void)color;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x48415443ULL); // 'HATC'
}

TL_MSABI int tl_PatBlt(void* const hdc, const int x, const int y, const int w, const int h, const std::uint32_t rop) noexcept {
    (void)hdc;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)rop;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_MaskBlt(void* const hdc_dest, const int x_dest, const int y_dest, const int width, const int height,
                        void* const hdc_src, const int x_src, const int y_src, void* const mask_bmp,
                        const int x_mask, const int y_mask, const std::uint32_t rop) noexcept {
    (void)hdc_dest;
    (void)x_dest;
    (void)y_dest;
    (void)width;
    (void)height;
    (void)hdc_src;
    (void)x_src;
    (void)y_src;
    (void)mask_bmp;
    (void)x_mask;
    (void)y_mask;
    (void)rop;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_PlgBlt(void* const hdc_dest, const void* const point, void* const hdc_src,
                       const int x_src, const int y_src, const int width, const int height,
                       void* const mask_bmp, const int x_mask, const int y_mask) noexcept {
    (void)hdc_dest;
    (void)point;
    (void)hdc_src;
    (void)x_src;
    (void)y_src;
    (void)width;
    (void)height;
    (void)mask_bmp;
    (void)x_mask;
    (void)y_mask;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_AlphaBlend(void* const hdc_dest, const int x_dest, const int y_dest, const int w_dest, const int h_dest,
                           void* const hdc_src, const int x_src, const int y_src, const int w_src, const int h_src,
                           const std::uint32_t blend_function) noexcept {
    (void)hdc_dest;
    (void)x_dest;
    (void)y_dest;
    (void)w_dest;
    (void)h_dest;
    (void)hdc_src;
    (void)x_src;
    (void)y_src;
    (void)w_src;
    (void)h_src;
    (void)blend_function;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_TransparentBlt(void* const hdc_dest, const int x_dest, const int y_dest, const int w_dest, const int h_dest,
                               void* const hdc_src, const int x_src, const int y_src, const int w_src, const int h_src,
                               const std::uint32_t cr_transparent) noexcept {
    (void)hdc_dest;
    (void)x_dest;
    (void)y_dest;
    (void)w_dest;
    (void)h_dest;
    (void)hdc_src;
    (void)x_src;
    (void)y_src;
    (void)w_src;
    (void)h_src;
    (void)cr_transparent;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_EnumFontFamiliesExW(void* const hdc, const void* const logfont, void* const callback,
                                    const std::intptr_t lparam, const std::uint32_t flags) noexcept {
    (void)hdc;
    (void)logfont;
    (void)callback;
    (void)lparam;
    (void)flags;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_EnumFontFamiliesExA(void* const hdc, const void* const logfont, void* const callback,
                                    const std::intptr_t lparam, const std::uint32_t flags) noexcept {
    (void)hdc;
    (void)logfont;
    (void)callback;
    (void)lparam;
    (void)flags;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreatePolygonRgn(const void* const points, const int count, const int mode) noexcept {
    (void)points;
    (void)count;
    (void)mode;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x504F4C59ULL); // 'POLY'
}

TL_MSABI int tl_FrameRgn(void* const hdc, void* const rgn, void* const brush, const int w, const int h) noexcept {
    (void)hdc;
    (void)rgn;
    (void)brush;
    (void)w;
    (void)h;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_FillRgn(void* const hdc, void* const rgn, void* const brush) noexcept {
    (void)hdc;
    (void)rgn;
    (void)brush;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_PaintRgn(void* const hdc, void* const rgn) noexcept {
    (void)hdc;
    (void)rgn;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_InvertRgn(void* const hdc, void* const rgn) noexcept {
    (void)hdc;
    (void)rgn;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreatePalette(const void* const logpalette) noexcept {
    (void)logpalette;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x50414C45ULL); // 'PALE'
}

TL_MSABI int tl_ExcludeClipRect(void* const hdc, const int left, const int top, const int right, const int bottom) noexcept {
    (void)hdc;
    (void)left;
    (void)top;
    (void)right;
    (void)bottom;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetBkMode(void* const hdc) noexcept {
    (void)hdc;
    set_last_error(abi::kErrorSuccess);
    return 1; // TRANSPARENT = 1, OPAQUE = 2
}

TL_MSABI int tl_GetCharABCWidthsFloatA(void* const hdc, const std::uint32_t first, const std::uint32_t last, void* const abc) noexcept {
    (void)hdc;
    if (abc != nullptr && last >= first) {
        const std::size_t count = static_cast<std::size_t>(last - first + 1);
        struct ABCFloat { float a; float b; float c; };
        if (mapped_guest_range(abc, count * sizeof(ABCFloat), true)) {
            auto* const out = static_cast<ABCFloat*>(abc);
            for (std::size_t i = 0; i < count; ++i) {
                out[i] = {0.0f, 8.0f, 0.0f};
            }
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetCharWidth32A(void* const hdc, const std::uint32_t first, const std::uint32_t last, int* const buffer) noexcept {
    (void)hdc;
    if (buffer != nullptr && last >= first) {
        const std::size_t count = static_cast<std::size_t>(last - first + 1);
        if (mapped_guest_range(buffer, count * sizeof(int), true)) {
            for (std::size_t i = 0; i < count; ++i) {
                buffer[i] = 8;
            }
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetCharWidthA(void* const hdc, const std::uint32_t first, const std::uint32_t last, int* const buffer) noexcept {
    return tl_GetCharWidth32A(hdc, first, last, buffer);
}

TL_MSABI std::uint32_t tl_GetCharacterPlacementW(void* const hdc, const wchar_t* const str, const int count, const int max, void* const results, const std::uint32_t flags) noexcept {
    (void)hdc;
    (void)str;
    (void)max;
    (void)results;
    (void)flags;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(count > 0 ? (count * 8) : 0);
}

TL_MSABI void* tl_GetCurrentObject(void* const hdc, const std::uint32_t type) noexcept {
    (void)hdc;
    (void)type;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x4744494FUL); // 'GDIO'
}

TL_MSABI int tl_GetDIBits(void* const hdc, void* const hbm, const std::uint32_t start, const std::uint32_t lines, void* const bits, void* const bi, const std::uint32_t usage) noexcept {
    (void)hdc;
    (void)hbm;
    (void)start;
    (void)bits;
    (void)bi;
    (void)usage;
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(lines);
}

TL_MSABI int tl_GetObjectA(void* const hgdiobj, const int cb_buffer, void* const lpv_object) noexcept {
    return tl_GetObjectW(hgdiobj, cb_buffer, lpv_object);
}

TL_MSABI std::uint32_t tl_GetOutlineTextMetricsA(void* const hdc, const std::uint32_t cb_data, void* const otm) noexcept {
    (void)hdc;
    (void)cb_data;
    (void)otm;
    set_last_error(abi::kErrorSuccess);
    return 0; // Not a TrueType font
}

TL_MSABI std::uint32_t tl_GetPixel(void* const hdc, const int x, const int y) noexcept {
    (void)hdc;
    (void)x;
    (void)y;
    set_last_error(abi::kErrorSuccess);
    return 0x00000000U; // Black
}

TL_MSABI int tl_GetTextExtentExPointA(void* const hdc, const char* const str, const int count, const int max_extent, int* const fit, int* const dx, void* const size) noexcept {
    (void)hdc;
    (void)str;
    (void)max_extent;
    if (fit != nullptr && mapped_guest_range(fit, sizeof(int), true)) {
        *fit = count;
    }
    if (dx != nullptr && count > 0 && mapped_guest_range(dx, static_cast<std::size_t>(count) * sizeof(int), true)) {
        for (int i = 0; i < count; ++i) {
            dx[i] = (i + 1) * 8;
        }
    }
    if (size != nullptr && mapped_guest_range(size, 8, true)) {
        *reinterpret_cast<std::int32_t*>(size) = count * 8;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(size) + 4) = 16;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetTextExtentPointA(void* const hdc, const char* const str, const int count, void* const size) noexcept {
    return tl_GetTextExtentExPointA(hdc, str, count, 0, nullptr, nullptr, size);
}

TL_MSABI int tl_IntersectClipRect(void* const hdc, const int left, const int top, const int right, const int bottom) noexcept {
    (void)hdc;
    (void)left;
    (void)top;
    (void)right;
    (void)bottom;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_RealizePalette(void* const hdc) noexcept {
    (void)hdc;
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI void* tl_SelectPalette(void* const hdc, void* const hpal, const int b_force_background) noexcept {
    (void)hdc;
    (void)b_force_background;
    set_last_error(abi::kErrorSuccess);
    return hpal;
}

TL_MSABI int tl_SetMapMode(void* const hdc, const int mode) noexcept {
    (void)hdc;
    (void)mode;
    set_last_error(abi::kErrorSuccess);
    return 1; // MM_TEXT
}

TL_MSABI std::uint32_t tl_SetPaletteEntries(void* const hpal, const std::uint32_t start, const std::uint32_t count, const void* const entries) noexcept {
    (void)hpal;
    (void)start;
    (void)entries;
    set_last_error(abi::kErrorSuccess);
    return count;
}

TL_MSABI std::uint32_t tl_SetPixel(void* const hdc, const int x, const int y, const std::uint32_t color) noexcept {
    (void)hdc;
    (void)x;
    (void)y;
    set_last_error(abi::kErrorSuccess);
    return color;
}

TL_MSABI int tl_TranslateCharsetInfo(std::uint32_t* const src, void* const cs, const std::uint32_t flags) noexcept {
    (void)src;
    (void)flags;
    if (cs != nullptr && mapped_guest_range(cs, 32, true)) {
        std::memset(cs, 0, 32);
        *reinterpret_cast<std::uint32_t*>(static_cast<char*>(cs) + 4) = 1252; // cp
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_UnrealizeObject(void* const hgdiobj) noexcept {
    (void)hgdiobj;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_UpdateColors(void* const hdc) noexcept {
    (void)hdc;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetWindowOrgEx(void* const hdc, const int x, const int y, void* const lppt) noexcept {
    (void)hdc;
    (void)x;
    (void)y;
    if (lppt != nullptr && mapped_guest_range(lppt, 8, true)) {
        *reinterpret_cast<std::int32_t*>(lppt) = 0;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(lppt) + 4) = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SaveDC(void* const hdc) noexcept {
    (void)hdc;
    return 1;
}

TL_MSABI int tl_RestoreDC(void* const hdc, const int nSavedDC) noexcept {
    (void)hdc;
    (void)nSavedDC;
    return 1;
}

TL_MSABI int tl_OffsetWindowOrgEx(void* const hdc, const int x, const int y, void* const lppt) noexcept {
    (void)hdc;
    (void)x;
    (void)y;
    if (lppt != nullptr && mapped_guest_range(lppt, 8, true)) {
        *reinterpret_cast<std::int32_t*>(lppt) = 0;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(lppt) + 4) = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetBrushOrgEx(void* const hdc, const int x, const int y, void* const lppt) noexcept {
    (void)hdc;
    (void)x;
    (void)y;
    if (lppt != nullptr && mapped_guest_range(lppt, 8, true)) {
        *reinterpret_cast<std::int32_t*>(lppt) = 0;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(lppt) + 4) = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetDIBits(void* const hdc, void* const hbm, const std::uint32_t start, const std::uint32_t lines, const void* const lpBits, const void* const lpbmi, const std::uint32_t fuColorUse) noexcept {
    (void)hdc;
    (void)hbm;
    (void)start;
    (void)lpBits;
    (void)lpbmi;
    (void)fuColorUse;
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(lines);
}

TL_MSABI int tl_DPtoLP(void* const hdc, void* const lpPoints, const int nCount) noexcept {
    (void)hdc;
    (void)lpPoints;
    (void)nCount;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetTextExtentPointW(void* const hdc, const wchar_t* const lpString, const int c, void* const lpSize) noexcept {
    (void)hdc;
    (void)lpString;
    if (lpSize != nullptr && mapped_guest_range(lpSize, 8, true)) {
        const int len = c >= 0 ? c : 0;
        *reinterpret_cast<std::int32_t*>(lpSize) = len * 8;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(lpSize) + 4) = 16;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_Ellipse(void* const hdc, const int left, const int top, const int right, const int bottom) noexcept {
    (void)hdc;
    (void)left;
    (void)top;
    (void)right;
    (void)bottom;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_ExtCreatePen(const std::uint32_t iPenStyle, const std::uint32_t cWidth, const void* const plbrush, const std::uint32_t cStyle, const std::uint32_t* const pstyle) noexcept {
    (void)iPenStyle;
    (void)cWidth;
    (void)plbrush;
    (void)cStyle;
    (void)pstyle;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x50454E32ULL); // 'PEN2'
}

TL_MSABI int tl_GdiAlphaBlend(void* const hdcDest, const int xoriginDest, const int yoriginDest, const int wDest, const int hDest, void* const hdcSrc, const int xoriginSrc, const int yoriginSrc, const int wSrc, const int hSrc, const std::uint32_t ftn) noexcept {
    (void)hdcDest;
    (void)xoriginDest;
    (void)yoriginDest;
    (void)wDest;
    (void)hDest;
    (void)hdcSrc;
    (void)xoriginSrc;
    (void)yoriginSrc;
    (void)wSrc;
    (void)hSrc;
    (void)ftn;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetTextExtentExPointW(void* const hdc, const wchar_t* const lpszStr, const int cchString, const int nMaxExtent, int* const lpnFit, int* const alpDx, void* const lpSize) noexcept {
    (void)hdc;
    (void)lpszStr;
    (void)nMaxExtent;
    if (lpnFit != nullptr && mapped_guest_range(lpnFit, sizeof(int), true)) {
        *lpnFit = cchString;
    }
    if (alpDx != nullptr && cchString > 0 && mapped_guest_range(alpDx, static_cast<std::size_t>(cchString) * sizeof(int), true)) {
        for (int i = 0; i < cchString; ++i) {
            alpDx[i] = (i + 1) * 8;
        }
    }
    if (lpSize != nullptr && mapped_guest_range(lpSize, 8, true)) {
        *reinterpret_cast<std::int32_t*>(lpSize) = cchString * 8;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(lpSize) + 4) = 16;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetROP2(void* const hdc) noexcept {
    (void)hdc;
    return 13; // R2_COPYPEN
}

TL_MSABI int tl_GetClipRgn(void* const hdc, void* const hrgn) noexcept {
    (void)hdc;
    (void)hrgn;
    return 0; // No initial clip region
}

TL_MSABI void* tl_CreateRectRgnIndirect(const void* const lprect) noexcept {
    (void)lprect;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x52474E49ULL); // 'RGNI'
}

TL_MSABI int tl_RoundRect(void* const hdc, const int left, const int top, const int right, const int bottom, const int width, const int height) noexcept {
    (void)hdc;
    (void)left;
    (void)top;
    (void)right;
    (void)bottom;
    (void)width;
    (void)height;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_Arc(void* const hdc, const int left, const int top, const int right, const int bottom, const int x_start, const int y_start, const int x_end, const int y_end) noexcept {
    (void)hdc;
    (void)left;
    (void)top;
    (void)right;
    (void)bottom;
    (void)x_start;
    (void)y_start;
    (void)x_end;
    (void)y_end;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_Pie(void* const hdc, const int left, const int top, const int right, const int bottom, const int x1, const int y1, const int x2, const int y2) noexcept {
    (void)hdc;
    (void)left;
    (void)top;
    (void)right;
    (void)bottom;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetTextCharacterExtra(void* const hdc) noexcept {
    (void)hdc;
    return 0;
}

TL_MSABI int tl_GetCharABCWidthsA(void* const hdc, const std::uint32_t first, const std::uint32_t last, void* const abc) noexcept {
    (void)hdc;
    if (abc != nullptr && last >= first) {
        const std::size_t count = static_cast<std::size_t>(last - first + 1);
        if (mapped_guest_range(abc, count * 12, true)) {
            std::memset(abc, 0, count * 12);
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetDeviceGammaRamp(void* const hdc, void* const ramp) noexcept {
    (void)hdc;
    if (ramp != nullptr && mapped_guest_range(ramp, 512, true)) {
        std::memset(ramp, 0, 512);
    }
    return 1;
}

TL_MSABI void* tl_CreateDCA(const char* const driver, const char* const device, const char* const port, const void* const dev_mode) noexcept {
    (void)driver;
    (void)device;
    (void)port;
    (void)dev_mode;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x44434100ULL); // 'DCA\0'
}

}  // extern "C"

}  // namespace tradutorlinux
