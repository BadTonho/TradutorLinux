#pragma once

#include "tradutorlinux/gui/x11.hpp"

namespace tradutorlinux::gui::wayland {

[[nodiscard]] NativeWindow create_window(const char* caption, int width, int height) noexcept;
void destroy_window(NativeWindow window) noexcept;
bool map_window(NativeWindow window) noexcept;
void unmap_window(NativeWindow window) noexcept;
void flush_window(NativeWindow window) noexcept;
void draw_text(NativeWindow window, const char* text, int x, int y) noexcept;
void draw_text_len(NativeWindow window, const char* text, int length, int x, int y) noexcept;
void draw_text_color(NativeWindow window, const char* text, int x, int y, std::uint32_t rgb,
                     bool bold = false) noexcept;
void draw_text_len_color(NativeWindow window, const char* text, int length, int x, int y,
                         std::uint32_t rgb, bool bold = false) noexcept;
void draw_rectangle(NativeWindow window, int x, int y, int width, int height) noexcept;
void draw_rectangle_color(NativeWindow window, int x, int y, int width, int height,
                          std::uint32_t rgb) noexcept;
void fill_rectangle(NativeWindow window, int x, int y, int width, int height,
                    int brush_index) noexcept;
void fill_rectangle_color(NativeWindow window, int x, int y, int width, int height,
                          std::uint32_t rgb) noexcept;
[[nodiscard]] WindowEvent next_window_event(NativeWindow window) noexcept;
[[nodiscard]] std::uint32_t message_box(const char* text, const char* caption) noexcept;
[[nodiscard]] std::uint32_t track_popup_menu(const std::vector<PopupMenuItem>& items, int x,
                                             int y) noexcept;
[[nodiscard]] bool available() noexcept;

}  // namespace tradutorlinux::gui::wayland
