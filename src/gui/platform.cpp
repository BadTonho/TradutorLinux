#include "tradutorlinux/gui/platform.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/gui/wayland.hpp"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <array>
#include <memory>
#include <new>
#include <iostream>

namespace tradutorlinux::gui::platform {
namespace {

enum class Backend { X11, Wayland };
struct WindowHandle {
    Backend backend;
    NativeWindow native;
};

[[nodiscard]] bool wants_wayland() noexcept {
    const char* value = std::getenv("TL_GUI_BACKEND");
    return value != nullptr && std::strcmp(value, "wayland") == 0;
}

[[nodiscard]] bool wants_x11() noexcept {
    const char* value = std::getenv("TL_GUI_BACKEND");
    return value != nullptr && std::strcmp(value, "x11") == 0;
}

[[nodiscard]] WindowHandle* handle(NativeWindow window) noexcept {
    return static_cast<WindowHandle*>(window);
}

}  // namespace

NativeWindow create_window(const char* caption, const int width, const int height) noexcept {
    if (!wants_x11() && (wants_wayland() || std::getenv("WAYLAND_DISPLAY") != nullptr) &&
        wayland::available()) {
        NativeWindow native = wayland::create_window(caption, width, height);
        if (native != nullptr) {
            auto* result = new (std::nothrow) WindowHandle{Backend::Wayland, native};
            if (result != nullptr) return result;
            wayland::destroy_window(native);
        }
        if (wants_wayland()) {
            std::fprintf(stderr, "[tl][gui][error] backend=wayland status=create-window-failed\n");
            return nullptr;
        }
    }
    if (wants_wayland()) {
        std::fprintf(stderr, "[tl][gui][error] backend=wayland status=unavailable\n");
        return nullptr;
    }
    NativeWindow native = ::tradutorlinux::gui::create_window(caption, width, height);
    if (native == nullptr) return nullptr;
    try {
        const std::array fields{diagnostics::TraceField{"backend", "x11"},
                                diagnostics::TraceField{"status", "selected"}};
        diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Gui,
                                 diagnostics::TraceLevel::Info, "backend", fields);
    } catch (...) {
    }
    auto* result = new (std::nothrow) WindowHandle{Backend::X11, native};
    if (result == nullptr) {
        ::tradutorlinux::gui::destroy_window(native);
        return nullptr;
    }
    return result;
}

void destroy_window(const NativeWindow window) noexcept {
    WindowHandle* value = handle(window);
    if (value == nullptr) return;
    if (value->backend == Backend::Wayland) wayland::destroy_window(value->native);
    else ::tradutorlinux::gui::destroy_window(value->native);
    delete value;
}

#define TL_PLATFORM_FORWARD_BOOL(name) \
    bool name(const NativeWindow window) noexcept { \
        WindowHandle* value = handle(window); if (value == nullptr) return false; \
        return value->backend == Backend::Wayland ? wayland::name(value->native) : ::tradutorlinux::gui::name(value->native); \
    }
#define TL_PLATFORM_FORWARD_VOID(name) \
    void name(const NativeWindow window) noexcept { \
        WindowHandle* value = handle(window); if (value == nullptr) return; \
        if (value->backend == Backend::Wayland) wayland::name(value->native); else ::tradutorlinux::gui::name(value->native); \
    }

TL_PLATFORM_FORWARD_BOOL(map_window)
TL_PLATFORM_FORWARD_VOID(unmap_window)
TL_PLATFORM_FORWARD_VOID(flush_window)

void draw_text(const NativeWindow window, const char* text, const int x, const int y) noexcept {
    draw_text_len(window, text, text == nullptr ? 0 : static_cast<int>(std::strlen(text)), x, y);
}
void draw_text_len(const NativeWindow window, const char* text, const int length, const int x, const int y) noexcept {
    draw_text_len_color(window, text, length, x, y, 0x1F2937U, false);
}
void draw_text_color(const NativeWindow window, const char* text, const int x, const int y,
                     const std::uint32_t rgb, const bool bold) noexcept {
    draw_text_len_color(window, text, text == nullptr ? 0 : static_cast<int>(std::strlen(text)), x, y, rgb, bold);
}
void draw_text_len_color(const NativeWindow window, const char* text, const int length, const int x,
                         const int y, const std::uint32_t rgb, const bool bold) noexcept {
    WindowHandle* value = handle(window); if (value == nullptr) return;
    if (value->backend == Backend::Wayland) wayland::draw_text_len_color(value->native, text, length, x, y, rgb, bold);
    else ::tradutorlinux::gui::draw_text_len_color(value->native, text, length, x, y, rgb, bold);
}
void draw_rectangle(const NativeWindow window, const int x, const int y, const int width, const int height) noexcept {
    draw_rectangle_color(window, x, y, width, height, 0x1F2937U);
}
void draw_rectangle_color(const NativeWindow window, const int x, const int y, const int width, const int height,
                          const std::uint32_t rgb) noexcept {
    WindowHandle* value = handle(window); if (value == nullptr) return;
    if (value->backend == Backend::Wayland) wayland::draw_rectangle_color(value->native, x, y, width, height, rgb);
    else ::tradutorlinux::gui::draw_rectangle_color(value->native, x, y, width, height, rgb);
}
void fill_rectangle(const NativeWindow window, const int x, const int y, const int width, const int height,
                    const int brush_index) noexcept {
    WindowHandle* value = handle(window); if (value == nullptr) return;
    if (value->backend == Backend::Wayland) wayland::fill_rectangle(value->native, x, y, width, height, brush_index);
    else ::tradutorlinux::gui::fill_rectangle(value->native, x, y, width, height, brush_index);
}
void fill_rectangle_color(const NativeWindow window, const int x, const int y, const int width, const int height,
                          const std::uint32_t rgb) noexcept {
    WindowHandle* value = handle(window); if (value == nullptr) return;
    if (value->backend == Backend::Wayland) wayland::fill_rectangle_color(value->native, x, y, width, height, rgb);
    else ::tradutorlinux::gui::fill_rectangle_color(value->native, x, y, width, height, rgb);
}
WindowEvent next_window_event(const NativeWindow window) noexcept {
    WindowHandle* value = handle(window); if (value == nullptr) return {};
    return value->backend == Backend::Wayland ? wayland::next_window_event(value->native) : ::tradutorlinux::gui::next_window_event(value->native);
}
std::uint32_t message_box(const char* text, const char* caption) noexcept {
    return ::tradutorlinux::gui::message_box(text, caption);
}
std::uint32_t track_popup_menu(const std::vector<PopupMenuItem>& items, const int x, const int y) noexcept {
    return ::tradutorlinux::gui::track_popup_menu(items, x, y);
}

}  // namespace tradutorlinux::gui::platform
