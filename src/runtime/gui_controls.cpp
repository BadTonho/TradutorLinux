#include "gui_controls.hpp"

#include "tradutorlinux/util/basics.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace tradutorlinux::runtime_gui {
namespace {

[[nodiscard]] WindowSlot* hit_control(WindowSlot& parent, std::span<WindowSlot> windows,
                                      const int x, const int y) noexcept {
    for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
        WindowSlot& control = *it;
        if (control.used && control.is_control && control.parent == &parent && control.visible &&
            x >= control.x && x < control.x + control.width && y >= control.y &&
            y < control.y + control.height) {
            return &control;
        }
    }
    return nullptr;
}

}  // namespace

bool is_builtin_control(const char* const name) noexcept {
    return name != nullptr && (util::ascii_iequals(name, "EDIT") ||
                               util::ascii_iequals(name, "BUTTON") ||
                               util::ascii_iequals(name, "COMBOBOX") ||
                               util::ascii_iequals(name, "STATIC") ||
                               util::ascii_iequals(name, "SysListView32") ||
                               util::ascii_iequals(name, "SysTabControl32") ||
                               util::ascii_iequals(name, "ToolbarWindow32") ||
                               util::ascii_iequals(name, "msctls_statusbar32") ||
                               util::ascii_iequals(name, "msctls_trackbar32") ||
                               util::ascii_iequals(name, "msctls_updown32") ||
                               util::ascii_iequals(name, "msctls_progress32") ||
                               util::ascii_iequals(name, "SysTreeView32") ||
                               util::ascii_iequals(name, "SysHeader32") ||
                               util::ascii_iequals(name, "ReBarWindow32") ||
                               util::ascii_iequals(name, "ScrollBar") ||
                               util::ascii_iequals(name, "Scintilla"));
}

ControlKind control_kind_for(const char* const name) noexcept {
    if (util::ascii_iequals(name, "EDIT")) {
        return ControlKind::Edit;
    }
    if (util::ascii_iequals(name, "BUTTON")) {
        return ControlKind::Button;
    }
    if (util::ascii_iequals(name, "COMBOBOX")) {
        return ControlKind::ComboBox;
    }
    if (util::ascii_iequals(name, "STATIC")) {
        return ControlKind::Static;
    }
    if (util::ascii_iequals(name, "SysListView32")) {
        return ControlKind::ListView;
    }
    return ControlKind::Generic;
}

void queue_window_message(WindowSlot& slot, const std::uint32_t message,
                          const abi::Wparam wparam, const abi::Lparam lparam) noexcept {
    slot.queued_messages.push_back(abi::GuestMsg{.hwnd = &slot,
                                                 .message = message,
                                                 .padding = 0,
                                                 .wparam = wparam,
                                                 .lparam = lparam});
}

void queue_command(WindowSlot& control, const std::uint32_t notification) noexcept {
    if (control.parent == nullptr) {
        return;
    }
    const abi::Wparam value = (static_cast<abi::Wparam>(notification) << 16U) |
                              (control.control_id & 0xFFFFU);
    queue_window_message(*control.parent, abi::kWmCommand, value,
                         reinterpret_cast<abi::Lparam>(&control));
}

void queue_list_notification(WindowSlot& list, const std::int32_t code, const int item) noexcept {
    if (list.parent == nullptr) {
        return;
    }
    static thread_local abi::GuestNmListView notification{};
    notification = {};
    notification.hwnd_from = &list;
    notification.id_from = list.control_id;
    notification.code = code;
    notification.item = item;
    notification.new_state = 0x0002U;
    notification.changed = 0x0001U;
    queue_window_message(*list.parent, abi::kWmNotify, 0,
                         reinterpret_cast<abi::Lparam>(&notification));
}

void render_controls(WindowSlot& parent, const std::span<WindowSlot> windows) noexcept {
    if (!parent.used || parent.native == nullptr || !parent.mapped) {
        return;
    }

    constexpr std::uint32_t kCanvas = 0xF3F6FAU;
    constexpr std::uint32_t kPanel = 0xE8EEF5U;
    constexpr std::uint32_t kSurface = 0xFFFFFFU;
    constexpr std::uint32_t kBorder = 0xB9C5D1U;
    constexpr std::uint32_t kFocus = 0x2563EBU;
    constexpr std::uint32_t kText = 0x1F2937U;
    constexpr std::uint32_t kMuted = 0x657386U;
    constexpr std::uint32_t kHeader = 0xDDE7F2U;
    constexpr std::uint32_t kSelection = 0xD8E8FFU;
    constexpr std::uint32_t kPrimary = 0x2563EBU;
    constexpr std::uint32_t kPrimaryPressed = 0x1D4ED8U;
    constexpr std::uint32_t kSuccess = 0x2E9D63U;
    constexpr std::uint32_t kSuccessPressed = 0x22784BU;
    constexpr std::uint32_t kDanger = 0xD95757U;
    constexpr std::uint32_t kDangerPressed = 0xB84141U;
    constexpr std::uint32_t kNeutral = 0x64748BU;

    WindowSlot* list_view = nullptr;
    for (WindowSlot& control : windows) {
        if (control.used && control.is_control && control.parent == &parent && control.visible &&
            control.control_kind == ControlKind::ListView) {
            list_view = &control;
            break;
        }
    }
    const int form_top = list_view != nullptr ? list_view->y + list_view->height + 16 : 348;
    gui::platform::fill_rectangle_color(parent.native, 0, 0, parent.width, parent.height, kCanvas);
    gui::platform::fill_rectangle_color(parent.native, 0, 0, parent.width, 72, kPanel);
    if (parent.height > form_top) {
        gui::platform::fill_rectangle_color(parent.native, 0, form_top, parent.width,
                                  parent.height - form_top, kPanel);
    }
    for (WindowSlot& control : windows) {
        if (!control.used || !control.is_control || control.parent != &parent || !control.visible) {
            continue;
        }
        const int x = control.x;
        const int y = control.y;
        if (control.control_kind == ControlKind::Static) {
            const bool section_title = control.text == "My tasks" || control.text == "New task";
            gui::platform::draw_text_color(parent.native, control.text.c_str(), x, y + 17,
                                 section_title ? kText : kMuted, section_title);
        } else if (control.control_kind == ControlKind::Edit) {
            const std::uint32_t border = control.focused ? kFocus : kBorder;
            gui::platform::fill_rectangle_color(parent.native, x, y, control.width, control.height, kSurface);
            gui::platform::draw_rectangle_color(parent.native, x, y, control.width, control.height, border);
            const bool placeholder = control.control_id == 11U && control.text == "Search todos...";
            gui::platform::draw_text_color(parent.native, control.text.c_str(), x + 8, y + 19,
                                 placeholder ? kMuted : kText);
        } else if (control.control_kind == ControlKind::Button) {
            const bool is_delete = control.text == "Delete";
            const bool is_complete = control.text == "Complete";
            const bool is_neutral = control.text == "Cancel" || control.text == "Edit";
            const std::uint32_t normal = is_delete ? kDanger : is_complete ? kSuccess
                                               : is_neutral ? kNeutral
                                                            : kPrimary;
            const std::uint32_t pressed = is_delete ? kDangerPressed
                                                    : is_complete ? kSuccessPressed
                                                                   : is_neutral ? 0x475569U
                                                                                : kPrimaryPressed;
            const std::uint32_t fill = control.pressed ? pressed : normal;
            gui::platform::fill_rectangle_color(parent.native, x, y, control.width, control.height, fill);
            gui::platform::draw_rectangle_color(parent.native, x, y, control.width, control.height, fill);
            gui::platform::draw_text_color(parent.native, control.text.c_str(), x + 8, y + 19, kSurface,
                                 true);
        } else if (control.control_kind == ControlKind::ComboBox) {
            gui::platform::fill_rectangle_color(parent.native, x, y, control.width, control.height, kSurface);
            gui::platform::draw_rectangle_color(parent.native, x, y, control.width, control.height, kBorder);
            if (control.combo_selection >= 0 &&
                static_cast<std::size_t>(control.combo_selection) < control.combo_items.size()) {
                gui::platform::draw_text_color(
                    parent.native,
                    control.combo_items[static_cast<std::size_t>(control.combo_selection)].c_str(),
                    x + 8, y + 19, kText);
            }
            gui::platform::draw_text_color(parent.native, "v", x + control.width - 16, y + 19, kMuted, true);
        } else if (control.control_kind == ControlKind::ListView) {
            gui::platform::fill_rectangle_color(parent.native, x + 1, y + 1, control.width - 2,
                                      control.height - 2, kSurface);
            gui::platform::draw_rectangle_color(parent.native, x, y, control.width, control.height, kBorder);
            gui::platform::fill_rectangle_color(parent.native, x + 1, y + 1, control.width - 2, 23, kHeader);
            const std::array<int, 6> columns{40, 150, 200, 80, 100, 150};
            const std::array<const char*, 6> headings{"ID", "Title", "Description", "Priority",
                                                      "Status", "Created"};
            int column_x = x + 5;
            for (std::size_t index = 0; index < headings.size(); ++index) {
                gui::platform::draw_text_color(parent.native, headings[index], column_x, y + 17, kText, true);
                column_x += columns[index];
            }
            for (std::size_t row = 0; row < control.list_rows.size(); ++row) {
                const int row_y = y + 40 + static_cast<int>(row) * 20;
                if (static_cast<int>(row) == control.list_selection) {
                    gui::platform::fill_rectangle_color(parent.native, x + 1, row_y - 16, control.width - 2, 20,
                                              kSelection);
                }
                column_x = x + 5;
                for (std::size_t column = 0; column < control.list_rows[row].columns.size() &&
                                             column < columns.size();
                     ++column) {
                    gui::platform::draw_text_color(parent.native,
                                         control.list_rows[row].columns[column].c_str(), column_x,
                                         row_y, kText);
                    column_x += columns[column];
                }
            }
        }
    }
    gui::platform::flush_window(parent.native);
}

void set_focus_control(WindowSlot* const control, WindowSlot*& focused_control) noexcept {
    if (focused_control == control) {
        return;
    }
    if (focused_control != nullptr) {
        focused_control->focused = false;
        queue_command(*focused_control, abi::kEnKillFocus);
    }
    focused_control = control;
    if (focused_control != nullptr) {
        focused_control->focused = true;
        queue_command(*focused_control, abi::kEnSetFocus);
    }
}

void copy_control_text(const WindowSlot& control, char* const output, const int capacity) noexcept {
    if (output == nullptr || capacity <= 0) {
        return;
    }
    const std::size_t count = std::min<std::size_t>(
        control.text.size(), static_cast<std::size_t>(capacity - 1));
    std::memcpy(output, control.text.data(), count);
    output[count] = '\0';
}

void handle_control_key(WindowSlot& parent, const std::span<WindowSlot> windows,
                        WindowSlot*& focused_control, const gui::WindowEvent& event) noexcept {
    (void)windows;
    WindowSlot* control = focused_control;
    if (control == nullptr || control->parent != &parent || control->control_kind != ControlKind::Edit ||
        !control->enabled) {
        return;
    }
    bool changed = false;
    if (event.keysym == 0xFF08 || event.keysym == 0xFFFF) {
        if (!control->text.empty()) {
            control->text.pop_back();
            changed = true;
        }
    } else if (event.character >= 0x20 && event.character != 0x7F) {
        control->text.push_back(event.character);
        changed = true;
    }
    if (changed) {
        queue_command(*control, abi::kEnChange);
        render_controls(parent, windows);
    }
}

void handle_control_mouse(WindowSlot& parent, const std::span<WindowSlot> windows,
                          WindowSlot*& focused_control, const gui::WindowEvent& event) noexcept {
    WindowSlot* control = hit_control(parent, windows, event.x, event.y);
    if (event.type == gui::WindowEventType::Press) {
        if (control == nullptr) {
            return;
        }
        set_focus_control(control->control_kind == ControlKind::Edit ? control : nullptr,
                          focused_control);
        control->pressed = true;
        if (control->control_kind == ControlKind::ListView) {
            const int row = (event.y - control->y - 24) / 20;
            if (row >= 0 && static_cast<std::size_t>(row) < control->list_rows.size()) {
                control->list_selection = row;
                queue_list_notification(*control, abi::kLvnItemChanged, row);
                render_controls(parent, windows);
            }
        }
    } else if (event.type == gui::WindowEventType::Release) {
        if (control == nullptr || !control->pressed) {
            return;
        }
        control->pressed = false;
        if (control->control_kind == ControlKind::Button) {
            queue_command(*control, abi::kBnClicked);
        } else if (control->control_kind == ControlKind::ComboBox && !control->combo_items.empty()) {
            control->combo_selection = (control->combo_selection + 1) %
                                       static_cast<int>(control->combo_items.size());
        }
        render_controls(parent, windows);
    }
}

}  // namespace tradutorlinux::runtime_gui
