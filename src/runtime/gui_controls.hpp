#pragma once

#include "tradutorlinux/gui/x11.hpp"
#include "tradutorlinux/runtime/winapi.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

namespace tradutorlinux::runtime_gui {

enum class ControlKind { None, Edit, Button, ComboBox, Static, ListView };

struct ListViewRow {
    std::vector<std::string> columns;
    std::intptr_t param{};
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
    std::deque<abi::GuestMsg> queued_messages;
    char last_key{'\0'};
    bool left_button_down{false};
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
    bool pressed{false};
    std::vector<std::string> combo_items;
    int combo_selection{-1};
    std::vector<ListViewRow> list_rows;
    int list_selection{-1};
    void* user_data{nullptr};
};

[[nodiscard]] bool is_builtin_control(const char* name) noexcept;
[[nodiscard]] ControlKind control_kind_for(const char* name) noexcept;

void queue_window_message(WindowSlot& slot, std::uint32_t message, abi::Wparam wparam,
                          abi::Lparam lparam) noexcept;
void queue_command(WindowSlot& control, std::uint32_t notification) noexcept;
void queue_list_notification(WindowSlot& list, std::int32_t code, int item) noexcept;

void render_controls(WindowSlot& parent, std::span<WindowSlot> windows) noexcept;
void handle_control_key(WindowSlot& parent, std::span<WindowSlot> windows,
                        WindowSlot*& focused_control, const gui::WindowEvent& event) noexcept;
void handle_control_mouse(WindowSlot& parent, std::span<WindowSlot> windows,
                          WindowSlot*& focused_control, const gui::WindowEvent& event) noexcept;
void set_focus_control(WindowSlot* control, WindowSlot*& focused_control) noexcept;
void copy_control_text(const WindowSlot& control, char* output, int capacity) noexcept;

}  // namespace tradutorlinux::runtime_gui
