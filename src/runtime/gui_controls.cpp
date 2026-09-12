#include "gui_controls.hpp"
#include "gui_extension.hpp"

#include "core/runtime_state_common.hpp"
#include "core/runtime_gui_state.hpp"
#include "tradutorlinux/runtime/guest_context.hpp"
#include "tradutorlinux/util/basics.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

namespace tradutorlinux::runtime_gui {
namespace {

constexpr std::uint32_t kWsTabStop = 0x00010000U;
constexpr std::uint32_t kBsDefPushButton = 0x00000001U;

[[nodiscard]] WindowSlot* default_button(const WindowSlot& parent,
                                          const std::span<WindowSlot> windows) noexcept {
    WindowSlot* first = nullptr;
    WindowSlot* tab_stop = nullptr;
    for (WindowSlot& child : windows) {
        if (!child.used || !child.is_control || child.parent != &parent ||
            child.control_kind != ControlKind::Button || !child.visible || !child.enabled) {
            continue;
        }
        if (first == nullptr) {
            first = &child;
        }
        if ((child.style & 0x0FU) == kBsDefPushButton) {
            return &child;
        }
        if (tab_stop == nullptr && (child.style & kWsTabStop) != 0U) {
            tab_stop = &child;
        }
    }
    return tab_stop != nullptr ? tab_stop : first;
}
[[nodiscard]] bool control_accepts_focus(const WindowSlot& child,
                                         const WindowSlot& parent) noexcept {
    if (!child.used || !child.is_control || child.parent != &parent || !child.visible ||
        !child.enabled) {
        return false;
    }
    if ((child.style & kWsTabStop) != 0U) {
        return true;
    }
    return child.control_kind == ControlKind::Edit ||
           child.control_kind == ControlKind::Button ||
           child.control_kind == ControlKind::ComboBox ||
           child.control_kind == ControlKind::ListView;
}

[[nodiscard]] WindowSlot* next_control_tab_item(const WindowSlot& parent,
                                                const std::span<WindowSlot> windows,
                                                WindowSlot* const current) noexcept {
    WindowSlot* first = nullptr;
    bool after_current = current == nullptr;
    for (WindowSlot& child : windows) {
        if (!control_accepts_focus(child, parent)) {
            continue;
        }
        if (first == nullptr) {
            first = &child;
        }
        if (after_current) {
            return &child;
        }
        if (&child == current) {
            after_current = true;
        }
    }
    return first;
}


}  // namespace

WindowSlot* find_control_at(WindowSlot& parent, const std::span<WindowSlot> windows,
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
    if (util::ascii_iequals(name, "ToolbarWindow32")) {
        return ControlKind::Toolbar;
    }
    if (util::ascii_iequals(name, "msctls_statusbar32")) {
        return ControlKind::StatusBar;
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

void queue_command_id(WindowSlot& control, const std::uint32_t notification,
                      const std::uintptr_t command_id) noexcept {
    if (control.parent == nullptr) {
        return;
    }
    const abi::Wparam value = (static_cast<abi::Wparam>(notification) << 16U) |
                              (command_id & 0xFFFFU);
    queue_window_message(*control.parent, abi::kWmCommand, value,
                         reinterpret_cast<abi::Lparam>(&control));
}

void queue_command(WindowSlot& control, const std::uint32_t notification) noexcept {
    queue_command_id(control, notification, control.control_id);
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
    if (!parent.used || parent.is_control || parent.native == nullptr || !parent.mapped) {
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

    if (const GuiExtension* const extension = active_gui_extension(parent);
        extension != nullptr) {
        GuiExtensionRuntime* const state = active_gui_extension_runtime();
        if (state != nullptr && extension->render(*state, parent, windows)) {
            return;
        }
    }

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
        } else if (control.control_kind == ControlKind::Toolbar) {
            const int width = std::max(control.width, 16);
            const int height = std::max(control.height, 24);
            gui::platform::fill_rectangle_color(parent.native, x, y, width, height, kPanel);
            gui::platform::draw_rectangle_color(parent.native, x, y, width, height, kBorder);
            if (!control.toolbar_buttons.empty()) {
                const int fitting_width = std::max(
                    (width - 8) / static_cast<int>(control.toolbar_buttons.size()), 1);
                const int button_width = std::min(
                    fitting_width, control.toolbar_button_width > 0 ? control.toolbar_button_width
                                                                     : fitting_width);
                int button_x = x + 4;
                for (std::size_t index = 0; index < control.toolbar_buttons.size(); ++index) {
                    const int remaining = x + width - button_x - 4;
                    const int current_width = std::min(button_width, std::max(remaining, 1));
                    gui::platform::fill_rectangle_color(parent.native, button_x, y + 3,
                                                        current_width, height - 6, kSurface);
                    gui::platform::draw_rectangle_color(parent.native, button_x, y + 3,
                                                        current_width, height - 6, kBorder);
                    const std::string label =
                        "#" + std::to_string(control.toolbar_buttons[index].command_id);
                    gui::platform::draw_text_color(parent.native, label.c_str(), button_x + 8,
                                                   y + std::min(height - 7, 21), kText, true);
                    button_x += current_width + 3;
                    if (button_x >= x + width - 3) {
                        break;
                    }
                }
            }
        } else if (control.control_kind == ControlKind::StatusBar) {
            const int width = std::max(control.width, 16);
            const int height = std::max(control.height, 20);
            gui::platform::fill_rectangle_color(parent.native, x, y, width, height, kPanel);
            gui::platform::fill_rectangle_color(parent.native, x, y, width, 1, kBorder);
            gui::platform::draw_text_color(parent.native,
                                           control.text.empty() ? "Ready" : control.text.c_str(),
                                           x + 8, y + std::min(height - 4, 16), kMuted);
        } else if (control.control_kind == ControlKind::Generic && control.width > 8 &&
                   control.height > 8) {
            // Um controle registrado pelo convidado pode ter um WNDPROC válido
            // sem que o subconjunto atual possua o backend de pintura dele.
            // Mostre a limitação na própria área do controle, em vez de
            // produzir uma janela aparentemente congelada e completamente
            // branca.
            const int width = std::max(control.width, 16);
            const int height = std::max(control.height, 16);
            gui::platform::fill_rectangle_color(parent.native, x, y, width, height, kSurface);
            gui::platform::draw_rectangle_color(parent.native, x, y, width, height, kBorder);
            gui::platform::fill_rectangle_color(parent.native, x + 1, y + 1, width - 2,
                                                std::min(height - 2, 32), kHeader);
            const std::string title = "Controle Win32 sem renderer: " + control.class_name;
            gui::platform::draw_text_color(parent.native, title.c_str(), x + 10, y + 22,
                                           kText, true);
            if (height > 48) {
                gui::platform::draw_text_color(
                    parent.native,
                    "A classe foi registrada e recebeu WM_PAINT; o backend visual ainda nao",
                    x + 10, y + 52, kMuted, false);
                gui::platform::draw_text_color(
                    parent.native, "implementa este controle ou seu modelo de desenho.", x + 10,
                    y + 72, kMuted, false);
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
    if (const GuiExtension* const extension = active_gui_extension(parent);
        extension != nullptr) {
        GuiExtensionRuntime* const state = active_gui_extension_runtime();
        if (state != nullptr &&
            extension->handle_key(*state, parent, windows, focused_control, event)) {
            return;
        }
    }
    if (!parent.is_dialog && event.type == gui::WindowEventType::KeyDown &&
        event.keysym == 0xFF09UL) {
        WindowSlot* const next = next_control_tab_item(parent, windows, focused_control);
        if (next != nullptr) {
            set_focus_control(next, focused_control);
        }
        return;
    }
    if (!parent.is_dialog && event.type == gui::WindowEventType::KeyDown &&
        event.keysym == 0xFF0DUL) {
        WindowSlot* target = focused_control;
        if (target == nullptr || target->parent != &parent || !target->used ||
            !target->is_control || !target->visible || !target->enabled ||
            target->control_kind != ControlKind::Button) {
            target = default_button(parent, windows);
        }
        if (target != nullptr) {
            queue_command(*target, abi::kBnClicked);
            return;
        }
    }
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
    if (const GuiExtension* const extension = active_gui_extension(parent);
        extension != nullptr) {
        GuiExtensionRuntime* const state = active_gui_extension_runtime();
        if (state != nullptr &&
            extension->handle_mouse(*state, parent, windows, focused_control, event)) {
            return;
        }
    }

    WindowSlot* const hit_control = find_control_at(parent, windows, event.x, event.y);
    WindowSlot* control = hit_control;
    if (event.type == gui::WindowEventType::Press) {
        if (control == nullptr) {
            return;
        }
        set_focus_control(control->control_kind == ControlKind::Edit ? control : nullptr,
                          focused_control);
        control->pressed = true;
        if (control->control_kind == ControlKind::Toolbar &&
            !control->toolbar_buttons.empty()) {
            const int available_width = std::max(control->width, 16);
            const int fitting_width = std::max(
                (available_width - 8) / static_cast<int>(control->toolbar_buttons.size()), 1);
            const int button_width = std::min(
                fitting_width, control->toolbar_button_width > 0 ? control->toolbar_button_width
                                                                   : fitting_width);
            control->pressed_toolbar_index = (event.x - control->x - 4) / button_width;
        }
        if (control->control_kind == ControlKind::ListView) {
            const int row = (event.y - control->y - 24) / 20;
            if (row >= 0 && static_cast<std::size_t>(row) < control->list_rows.size()) {
                control->list_selection = row;
                queue_list_notification(*control, abi::kLvnItemChanged, row);
            }
        }
        render_controls(parent, windows);
    } else if (event.type == gui::WindowEventType::Release) {
        if (control == nullptr || !control->pressed) {
            for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
                if (it->used && it->is_control && it->parent == &parent && it->pressed) {
                    control = &*it;
                    break;
                }
            }
        }
        if (control == nullptr || !control->pressed) {
            return;
        }
        const bool released_over_pressed_control = hit_control == control;
        control->pressed = false;
        if (control->control_kind == ControlKind::Button) {
            if (released_over_pressed_control) {
                queue_command(*control, abi::kBnClicked);
            }
        } else if (control->control_kind == ControlKind::ComboBox && !control->combo_items.empty()) {
            if (released_over_pressed_control) {
                control->combo_selection = (control->combo_selection + 1) %
                                           static_cast<int>(control->combo_items.size());
            }
        } else if (control->control_kind == ControlKind::Toolbar &&
                   !control->toolbar_buttons.empty()) {
            const int available_width = std::max(control->width, 16);
            const int fitting_width = std::max(
                (available_width - 8) / static_cast<int>(control->toolbar_buttons.size()), 1);
            const int button_width = std::min(
                fitting_width, control->toolbar_button_width > 0 ? control->toolbar_button_width
                                                                  : fitting_width);
            const int index = (event.x - control->x - 4) / button_width;
            if (released_over_pressed_control && index == control->pressed_toolbar_index &&
                index >= 0 && static_cast<std::size_t>(index) < control->toolbar_buttons.size()) {
                queue_command_id(*control, 0,
                                 static_cast<std::uintptr_t>(
                                     control->toolbar_buttons[static_cast<std::size_t>(index)]
                                         .command_id));
            }
            control->hovered_toolbar_index = index;
            control->pressed_toolbar_index = -1;
        }
        render_controls(parent, windows);
    }
}

}
