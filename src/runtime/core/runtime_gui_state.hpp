#pragma once

#include "runtime_state_common.hpp"

#include "tradutorlinux/gui/platform.hpp"
#include "../gui_controls.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tradutorlinux {

using runtime_gui::ControlKind;
using runtime_gui::GuestTimer;
using runtime_gui::ListViewRow;
using runtime_gui::TreeItem;
using runtime_gui::ToolbarButton;
using runtime_gui::WindowSlot;

struct ClassSlot {
    bool used{false};
    std::string name;
    std::uintptr_t wndproc{0};
    std::uint16_t atom{0};
    std::uintptr_t menu_name_raw{0};
    std::u16string menu_name_text;
};

constexpr std::size_t kMaxGuestClasses = 64;
constexpr std::size_t kMaxGuestWindows = 512;

extern std::array<ClassSlot, kMaxGuestClasses> g_classes;
extern std::array<WindowSlot, kMaxGuestWindows> g_windows;
extern WindowSlot* g_focused_control;
extern WindowSlot* g_active_dialog;
extern std::mutex g_modal_mutex;
extern bool g_modal_done;
extern std::intptr_t g_modal_result;
extern WindowSlot* g_modal_parent;
extern bool g_modal_parent_was_enabled;
extern bool g_quit_requested;
extern std::uint32_t g_quit_code;

struct CrossThreadWindowMessage {
    std::uintptr_t window{};
    std::uint32_t message{};
    std::uintptr_t wparam{};
    std::intptr_t lparam{};
};

bool register_window_handle(const void* handle) noexcept;
void unregister_window_handle(const void* handle) noexcept;
[[nodiscard]] bool is_registered_window_handle(const void* handle) noexcept;
[[nodiscard]] bool post_cross_thread_window_message(const void* window,
                                                    std::uint32_t message,
                                                    std::uintptr_t wparam,
                                                    std::intptr_t lparam) noexcept;
[[nodiscard]] bool peek_cross_thread_window_message(const void* window_filter,
                                                    CrossThreadWindowMessage& message) noexcept;
[[nodiscard]] bool take_cross_thread_window_message(const void* window_filter,
                                                    CrossThreadWindowMessage& message) noexcept;
void clear_cross_thread_window_messages() noexcept;

struct MenuSlot;

struct MenuItem {
    std::uint32_t type{0};
    std::uint32_t state{0};
    std::uint32_t command_id{0};
    std::uint16_t flags{0};
    std::string text;
    MenuSlot* submenu{nullptr};
};

struct MenuSlot {
    bool used{false};
    std::vector<gui::PopupMenuItem> items;
    std::vector<MenuItem> logical_items;
};

extern std::array<MenuSlot, 256> g_menus;

[[nodiscard]] inline const MenuSlot* find_menu_slot(const void* const menu) noexcept {
    if (menu == nullptr) {
        return nullptr;
    }
    const auto it = std::find_if(g_menus.begin(), g_menus.end(),
                                 [menu](const MenuSlot& entry) {
                                     return entry.used && &entry == menu;
                                 });
    return it == g_menus.end() ? nullptr : &*it;
}

ClassSlot* find_class_slot(const char* name) noexcept;
WindowSlot* find_window_slot(const void* handle) noexcept;
WindowSlot* create_logical_control(WindowSlot& parent, std::string_view class_name,
                                   std::string_view title, std::uint32_t style,
                                   std::uintptr_t control_id, int x, int y, int width,
                                   int height) noexcept;

struct WindowDrawingTarget {
    gui::NativeWindow native{nullptr};
    int offset_x{0};
    int offset_y{0};
};

[[nodiscard]] WindowDrawingTarget window_drawing_target(const void* handle) noexcept;
int stock_object_index(const void* token) noexcept;

}  // namespace tradutorlinux
