#include "gui_controls.hpp"

#include "tradutorlinux/runtime/guest_context.hpp"
#include "tradutorlinux/util/basics.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <string_view>

namespace tradutorlinux::runtime_gui {
namespace {

[[nodiscard]] bool is_seven_zip_file_manager(const WindowSlot& parent) noexcept {
    return util::ascii_iequals(parent.class_name, "7-Zip::FM");
}

struct SevenZipDirectoryEntry {
    std::string name;
    std::string size;
    bool directory{false};
};

[[nodiscard]] std::filesystem::path seven_zip_host_directory() noexcept {
    const auto& module_path = runtime::guest_context().module_file_name;
    if (module_path.empty()) {
        return {};
    }
    const std::filesystem::path executable{module_path};
    if (executable.empty() || !executable.is_absolute() || executable.filename().empty()) {
        return {};
    }
    return executable.parent_path();
}

[[nodiscard]] std::string seven_zip_display_name(const std::string_view name) {
    // A coluna de nome tem cerca de 200 px no shell 800x600. O limite mantém
    // o tamanho visível separado mesmo quando o diretório contém installers.
    constexpr std::size_t kMaxNameBytes = 24;
    if (name.size() <= kMaxNameBytes) {
        return std::string{name};
    }
    return std::string{name.substr(0, kMaxNameBytes - 3)} + "...";
}

void draw_seven_zip_toolbar_button(const gui::NativeWindow native, const char* const icon,
                                   const char* const label, const int x, const int y,
                                   const int width) noexcept {
    constexpr std::uint32_t kButton = 0xF8FAFCU;
    constexpr std::uint32_t kBorder = 0xB7C2CCU;
    constexpr std::uint32_t kIcon = 0x245B8FU;
    constexpr std::uint32_t kText = 0x263442U;
    gui::platform::fill_rectangle_color(native, x, y, width, 36, kButton);
    gui::platform::draw_rectangle_color(native, x, y, width, 36, kBorder);
    gui::platform::draw_text_color(native, icon, x + 8, y + 23, kIcon, true);
    gui::platform::draw_text_color(native, label, x + 27, y + 23, kText);
}

void render_seven_zip_file_manager(WindowSlot& parent) noexcept {
    constexpr std::uint32_t kWindow = 0xF5F7F9U;
    constexpr std::uint32_t kMenu = 0xEEF1F4U;
    constexpr std::uint32_t kToolbar = 0xE2E7ECU;
    constexpr std::uint32_t kSurface = 0xFFFFFFU;
    constexpr std::uint32_t kBorder = 0xB4C0CBU;
    constexpr std::uint32_t kHeader = 0xD9E2EAU;
    constexpr std::uint32_t kText = 0x263442U;
    constexpr std::uint32_t kMuted = 0x657586U;
    constexpr std::uint32_t kBlue = 0x245B8FU;
    constexpr std::uint32_t kSelection = 0xD4E5F7U;
    constexpr std::uint32_t kStatus = 0xE9EEF2U;

    const int width = std::max(parent.width, 1);
    const int height = std::max(parent.height, 1);
    const int status_y = std::max(height - 24, 0);
    const int body_y = 108;

    gui::platform::fill_rectangle_color(parent.native, 0, 0, width, height, kWindow);

    // File Manager's menu bar.
    gui::platform::fill_rectangle_color(parent.native, 0, 0, width, 28, kMenu);
    gui::platform::fill_rectangle_color(parent.native, 0, 27, width, 1, kBorder);
    constexpr std::array<const char*, 5> kMenus{"File", "Edit", "View", "Tools", "Help"};
    constexpr std::array<int, 5> kMenu_x{14, 63, 111, 165, 220};
    for (std::size_t index = 0; index < kMenus.size(); ++index) {
        gui::platform::draw_text_color(parent.native, kMenus[index], kMenu_x[index], 19, kText);
    }

    // The classic 7-Zip toolbar, kept as a logical visual surface until its
    // command notifications are connected to the guest window procedure.
    gui::platform::fill_rectangle_color(parent.native, 0, 28, width, 44, kToolbar);
    gui::platform::fill_rectangle_color(parent.native, 0, 71, width, 1, kBorder);
    constexpr std::array<const char*, 7> kToolbar_icons{"+", "->", "T", "C", "M", "X", "i"};
    constexpr std::array<const char*, 7> kToolbar_labels{
        "Add", "Extract", "Test", "Copy", "Move", "Delete", "Info"};
    constexpr std::array<int, 7> kToolbar_widths{66, 78, 66, 68, 68, 76, 62};
    int toolbar_x = 8;
    for (std::size_t index = 0; index < kToolbar_labels.size(); ++index) {
        draw_seven_zip_toolbar_button(parent.native, kToolbar_icons[index], kToolbar_labels[index],
                                      toolbar_x, 32, kToolbar_widths[index]);
        toolbar_x += kToolbar_widths[index] + 4;
    }

    // Location bar.
    gui::platform::draw_text_color(parent.native, "Address", 10, 91, kMuted);
    const int address_x = 67;
    const int address_width = std::max(width - address_x - 10, 20);
    gui::platform::fill_rectangle_color(parent.native, address_x, 78, address_width, 24,
                                        kSurface);
    gui::platform::draw_rectangle_color(parent.native, address_x, 78, address_width, 24, kBorder);
    gui::platform::draw_text_color(parent.native, "Z:\\", address_x + 9, 95, kText);

    if (status_y > body_y) {
        // Navigation tree on the left and file list on the right.
        const int body_height = status_y - body_y;
        const int navigation_width = width >= 480 ? std::clamp(width / 4, 170, 220)
                                                  : std::max(width / 3, 1);
        const int list_x = navigation_width + 8;
        const int list_width = std::max(width - list_x - 8, 1);
        gui::platform::fill_rectangle_color(parent.native, 8, body_y, navigation_width,
                                            body_height, 0xF0F3F6U);
        gui::platform::draw_rectangle_color(parent.native, 8, body_y, navigation_width,
                                            body_height, kBorder);
        gui::platform::draw_text_color(parent.native, "Navigation", 20, body_y + 19, kText, true);
        constexpr std::array<const char*, 5> kNavigation_items{
            "Computer", "Local Disk (Z:)", "Home", "Desktop", "Documents"};
        for (std::size_t index = 0; index < kNavigation_items.size(); ++index) {
            const int row_y = body_y + 48 + static_cast<int>(index) * 24;
            if (index == 1) {
                gui::platform::fill_rectangle_color(parent.native, 9, row_y - 17,
                                                    navigation_width - 2, 22, kSelection);
            }
            gui::platform::draw_text_color(parent.native, index == 1 ? "[+]" : "[ ]", 20, row_y,
                                           kBlue, true);
            gui::platform::draw_text_color(parent.native, kNavigation_items[index], 50, row_y, kText);
        }

        gui::platform::fill_rectangle_color(parent.native, list_x, body_y, list_width,
                                            body_height, kSurface);
        gui::platform::draw_rectangle_color(parent.native, list_x, body_y, list_width,
                                            body_height, kBorder);
        gui::platform::fill_rectangle_color(parent.native, list_x + 1, body_y + 1,
                                            std::max(list_width - 2, 1), 25, kHeader);

        constexpr std::array<int, 6> kColumns{8, 250, 360, 475, 600, 700};
        constexpr std::array<const char*, 6> kHeadings{
            "", "Name", "Size", "Packed Size", "Modified", "Attributes"};
        for (std::size_t index = 0; index < kHeadings.size(); ++index) {
            if (kHeadings[index][0] != '\0') {
                gui::platform::draw_text_color(parent.native, kHeadings[index],
                                               list_x + kColumns[index], body_y + 18, kText, true);
            }
        }

        const std::vector<ListViewRow> rows =
            collect_seven_zip_directory_rows(seven_zip_host_directory());
        if (rows.empty()) {
            gui::platform::draw_text_color(parent.native, "(diretorio indisponivel)", list_x + 65,
                                           body_y + 48, kMuted);
        }
        for (std::size_t index = 0; index < rows.size(); ++index) {
            const int row_y = body_y + 48 + static_cast<int>(index) * 22;
            if (row_y + 8 >= status_y) {
                break;
            }
            if (index == 0) {
                gui::platform::fill_rectangle_color(parent.native, list_x + 1, row_y - 17,
                                                    std::max(list_width - 2, 1), 21, kSelection);
            }
            const bool directory = rows[index].columns.size() > 1 &&
                                   rows[index].columns[1] == "<DIR>";
            gui::platform::draw_text_color(parent.native, directory ? "[DIR]" : "[FILE]",
                                           list_x + 12, row_y, kBlue, true);
            if (!rows[index].columns.empty()) {
                const std::string name = seven_zip_display_name(rows[index].columns[0]);
                gui::platform::draw_text_color(parent.native, name.c_str(), list_x + 65, row_y,
                                               kText);
            }
            if (rows[index].columns.size() > 1) {
                gui::platform::draw_text_color(parent.native, rows[index].columns[1].c_str(),
                                               list_x + 260, row_y, kMuted);
            }
        }
    }

    // Keep the limitation visible without replacing the application surface
    // with the old unsupported-control diagnostic.
    gui::platform::fill_rectangle_color(parent.native, 0, status_y, width,
                                        std::max(height - status_y, 1), kStatus);
    gui::platform::fill_rectangle_color(parent.native, 0, status_y, width, 1, kBorder);
    gui::platform::draw_text_color(parent.native, "Visualizacao experimental", 10,
                                   std::min(status_y + 17, height - 4), kMuted);
    gui::platform::draw_text_color(parent.native, "Z:\\", std::max(width - 42, 10),
                                   std::min(status_y + 17, height - 4), kMuted);
    gui::platform::flush_window(parent.native);
}

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

std::vector<ListViewRow> collect_seven_zip_directory_rows(
    const std::filesystem::path& directory, const std::size_t max_rows) noexcept {
    std::vector<ListViewRow> rows;
    if (directory.empty() || max_rows == 0) {
        return rows;
    }

    try {
        const std::size_t row_limit = std::min(max_rows, kSevenZipDirectoryRowLimit);
        std::vector<SevenZipDirectoryEntry> entries;
        entries.reserve(row_limit - 1);
        std::error_code iterator_error;
        std::filesystem::directory_iterator iterator(
            directory, std::filesystem::directory_options::skip_permission_denied,
            iterator_error);
        if (iterator_error) {
            return rows;
        }

        for (const auto end = std::filesystem::directory_iterator{};
             iterator != end && entries.size() + 1 < row_limit; iterator.increment(iterator_error)) {
            if (iterator_error) {
                break;
            }
            const std::filesystem::directory_entry& entry = *iterator;
            std::error_code status_error;
            const std::filesystem::file_status status = entry.symlink_status(status_error);
            if (status_error) {
                continue;
            }
            const bool is_directory = std::filesystem::is_directory(status);
            std::string size = is_directory ? "<DIR>" : "";
            if (!is_directory && std::filesystem::is_regular_file(status)) {
                std::error_code size_error;
                const std::uintmax_t bytes = entry.file_size(size_error);
                if (!size_error) {
                    size = std::to_string(bytes);
                }
            }
            entries.push_back({entry.path().filename().string(), std::move(size), is_directory});
        }

        std::sort(entries.begin(), entries.end(), [](const SevenZipDirectoryEntry& left,
                                                     const SevenZipDirectoryEntry& right) {
            if (left.directory != right.directory) {
                return left.directory > right.directory;
            }
            return left.name < right.name;
        });

        rows.reserve(entries.size() + 1);
        rows.push_back({{"..", "<DIR>"}, 0});
        for (SevenZipDirectoryEntry& entry : entries) {
            rows.push_back({{std::move(entry.name), std::move(entry.size)}, 0});
        }
    } catch (...) {
        rows.clear();
    }
    return rows;
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

    if (is_seven_zip_file_manager(parent)) {
        render_seven_zip_file_manager(parent);
        return;
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
