#pragma once

#include "tradutorlinux/gui/platform.hpp"
#include "tradutorlinux/runtime/winapi.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

namespace tradutorlinux::runtime_gui {

enum class ControlKind { None, Edit, Button, ComboBox, Static, ListView, Toolbar, StatusBar, Generic };

struct ToolbarButton {
    std::int32_t command_id{0};
};

struct ListViewRow {
    std::vector<std::string> columns;
    std::intptr_t param{};
};

struct TreeItem {
    std::uintptr_t handle{0};
    std::uintptr_t parent{0};
    std::uintptr_t item_data{0};
    std::string text;
    bool expanded{false};
};

struct GuestTimer {
    std::uintptr_t id{0};
    std::chrono::steady_clock::time_point deadline{};
    std::chrono::milliseconds interval{};
};

struct WindowSlot {
    bool used{false};
    std::uintptr_t wndproc{0};
    std::string class_name;
    std::string window_title;
    gui::NativeWindow native{nullptr};
    bool mapped{false};
    abi::GuestMsg pending{};
    bool has_pending{false};
    bool render_pending{false};
    std::deque<abi::GuestMsg> queued_messages;
    char last_key{'\0'};
    bool left_button_down{false};
    bool tray_registered{false};
    std::uint32_t tray_icon_id{0};
    std::uint32_t tray_callback_message{0};
    std::vector<GuestTimer> timers;
    int width{0};
    int height{0};
    bool painting{false};
    bool is_control{false};
    ControlKind control_kind{ControlKind::None};
    WindowSlot* parent{nullptr};
    std::uintptr_t control_id{0};
    int x{0};
    int y{0};
    std::string text;
    bool visible{true};
    bool enabled{true};
    bool focused{false};
    bool destroying{false};
    bool pressed{false};
    int hovered_toolbar_index{-1};
    int pressed_toolbar_index{-1};
    const void* menu_handle{nullptr};
    int open_menu_index{-1};
    std::vector<std::string> combo_items;
    int combo_selection{-1};
    std::vector<ToolbarButton> toolbar_buttons;
    int toolbar_button_width{0};
    std::uint32_t toolbar_button_struct_size{0};
    std::vector<ListViewRow> list_rows;
    int list_selection{-1};
    std::vector<TreeItem> tree_items;
    std::uintptr_t tree_next_handle{1};
    std::uintptr_t tree_selected{0};
    void* user_data{nullptr};
    std::uint32_t style{0};
    std::uint32_t extended_style{0};
    bool is_dialog{false};
    std::vector<WindowSlot*> dialog_children;
    std::array<std::uint8_t, 256> extra_bytes{};
};

[[nodiscard]] bool is_builtin_control(const char* name) noexcept;
[[nodiscard]] ControlKind control_kind_for(const char* name) noexcept;

void queue_window_message(WindowSlot& slot, std::uint32_t message, abi::Wparam wparam,
                          abi::Lparam lparam) noexcept;
void queue_command(WindowSlot& control, std::uint32_t notification) noexcept;
void queue_command_id(WindowSlot& control, std::uint32_t notification,
                      std::uintptr_t command_id) noexcept;
void queue_list_notification(WindowSlot& list, std::int32_t code, int item) noexcept;

[[nodiscard]] WindowSlot* find_control_at(WindowSlot& parent, std::span<WindowSlot> windows,
                                          int x, int y) noexcept;
void render_controls(WindowSlot& parent, std::span<WindowSlot> windows) noexcept;
void handle_control_key(WindowSlot& parent, std::span<WindowSlot> windows,
                        WindowSlot*& focused_control, const gui::WindowEvent& event) noexcept;
void handle_control_mouse(WindowSlot& parent, std::span<WindowSlot> windows,
                          WindowSlot*& focused_control, const gui::WindowEvent& event) noexcept;
void set_focus_control(WindowSlot* control, WindowSlot*& focused_control) noexcept;
void copy_control_text(const WindowSlot& control, char* output, int capacity) noexcept;

}  // namespace tradutorlinux::runtime_gui
