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

}  // extern "C"

}  // namespace tradutorlinux
