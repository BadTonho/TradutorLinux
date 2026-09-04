#include "gui_controls.hpp"

#include "runtime_context.hpp"
#include "tradutorlinux/runtime/guest_context.hpp"
#include "tradutorlinux/util/basics.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
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

struct SevenZipToolbarVisual {
    std::int32_t command_id;
    const char* icon;
    const char* label;
    int width;
};

struct SevenZipListGeometry {
    int body_y{108};
    int status_y{0};
    int navigation_width{0};
    int list_x{0};
    int list_width{0};
};

constexpr std::array<SevenZipToolbarVisual, 7> kSevenZipToolbarVisuals{{
    {1070, "+", "Add", 66},
    {1071, "->", "Extract", 78},
    {1072, "T", "Test", 66},
    {546, "C", "Copy", 68},
    {547, "M", "Move", 68},
    {548, "X", "Delete", 76},
    {551, "i", "Info", 62},
}};

[[nodiscard]] const SevenZipToolbarVisual* seven_zip_toolbar_visual(
    const std::int32_t command_id) noexcept {
    const auto found = std::find_if(
        kSevenZipToolbarVisuals.begin(), kSevenZipToolbarVisuals.end(),
        [command_id](const SevenZipToolbarVisual& visual) {
            return visual.command_id == command_id;
        });
    return found == kSevenZipToolbarVisuals.end() ? nullptr : &*found;
}

[[nodiscard]] int seven_zip_toolbar_button_width(const std::int32_t command_id) noexcept {
    const SevenZipToolbarVisual* const visual = seven_zip_toolbar_visual(command_id);
    return visual == nullptr ? 68 : visual->width;
}

[[nodiscard]] SevenZipListGeometry seven_zip_list_geometry(const WindowSlot& parent) noexcept {
    SevenZipListGeometry geometry{};
    const int width = std::max(parent.width, 1);
    const int height = std::max(parent.height, 1);
    geometry.status_y = std::max(height - 24, 0);
    geometry.navigation_width = width >= 480 ? std::clamp(width / 4, 170, 220)
                                             : std::max(width / 3, 1);
    geometry.list_x = geometry.navigation_width + 8;
    geometry.list_width = std::max(width - geometry.list_x - 8, 1);
    return geometry;
}

[[nodiscard]] std::filesystem::path seven_zip_current_directory(
    const WindowSlot& parent) noexcept {
    return parent.visual_directory.empty() ? seven_zip_host_directory() : parent.visual_directory;
}

[[nodiscard]] std::filesystem::path seven_zip_root_directory(
    const WindowSlot& parent) noexcept {
    return parent.visual_root_directory.empty() ? seven_zip_host_directory()
                                                  : parent.visual_root_directory;
}

[[nodiscard]] std::filesystem::path seven_zip_home_directory() noexcept {
    const char* const home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        return {};
    }
    return std::filesystem::path{home};
}

[[nodiscard]] std::filesystem::path seven_zip_navigation_directory(
    const WindowSlot& parent, const int row) noexcept {
    const std::filesystem::path root = seven_zip_root_directory(parent);
    if (row == 0 || row == 1) {
        return root;
    }
    const std::filesystem::path home = seven_zip_home_directory();
    if (home.empty()) {
        return {};
    }
    if (row == 2) {
        return home;
    }
    if (row == 3) {
        const std::filesystem::path desktop = home / "Desktop";
        std::error_code error;
        if (std::filesystem::is_directory(desktop, error) && !error) {
            return desktop;
        }
        return home / "Área de trabalho";
    }
    if (row == 4) {
        const std::filesystem::path documents = home / "Documents";
        std::error_code error;
        if (std::filesystem::is_directory(documents, error) && !error) {
            return documents;
        }
        return home / "Documentos";
    }
    return {};
}

[[nodiscard]] std::string seven_zip_visual_address(const WindowSlot& parent) noexcept {
    try {
        const std::filesystem::path base = seven_zip_root_directory(parent);
        const std::filesystem::path current = seven_zip_current_directory(parent);
        if (base.empty() || current.empty()) {
            return "Z:\\";
        }
        const std::filesystem::path relative = current.lexically_relative(base);
        const std::string relative_text = relative.generic_string();
        if (relative_text.empty() || relative_text == ".") {
            return "Z:\\";
        }
        std::string result = "Z:\\";
        for (const char character : relative_text) {
            result.push_back(character == '/' ? '\\' : character);
        }
        return result;
    } catch (...) {
        return "Z:\\";
    }
}

[[nodiscard]] bool seven_zip_address_hit(const WindowSlot& parent, const int x,
                                          const int y) noexcept {
    const int width = std::max(parent.width, 1);
    const int address_x = 67;
    const int address_width = std::max(width - address_x - 10, 20);
    return x >= address_x && x < address_x + address_width && y >= 78 && y < 102;
}

[[nodiscard]] std::filesystem::path seven_zip_path_from_address(
    const WindowSlot& parent, const std::string_view address) noexcept {
    try {
        if (address.size() < 2U || (address[0] != 'Z' && address[0] != 'z') ||
            address[1] != ':') {
            return {};
        }
        const std::filesystem::path root = seven_zip_root_directory(parent);
        if (root.empty()) {
            return {};
        }
        std::string relative_text{address.substr(2)};
        while (!relative_text.empty() &&
               (relative_text.front() == '\\' || relative_text.front() == '/')) {
            relative_text.erase(relative_text.begin());
        }
        for (char& character : relative_text) {
            if (character == '\\') {
                character = '/';
            }
        }
        const std::filesystem::path candidate =
            (relative_text.empty() ? root : root / relative_text).lexically_normal();
        const std::string relative = candidate.lexically_relative(root).generic_string();
        if (relative == ".." || relative.starts_with("../")) {
            return {};
        }
        return candidate;
    } catch (...) {
        return {};
    }
}

void clear_seven_zip_address_edit(WindowSlot& parent) noexcept {
    parent.address_editing = false;
    parent.address_error = false;
    parent.address_text.clear();
}

[[nodiscard]] std::string menu_display_text(const std::string_view text) {
    std::string result;
    result.reserve(text.size());
    bool accelerator = false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char character = text[index];
        if (character == '\t') {
            break;
        }
        if (character == '&' && !accelerator) {
            if (index + 1U < text.size() && text[index + 1U] == '&') {
                result.push_back('&');
                ++index;
            }
            accelerator = true;
            continue;
        }
        accelerator = false;
        result.push_back(character);
    }
    return result;
}

constexpr int kSevenZipMenuBarHeight = 28;
constexpr int kSevenZipMenuRowHeight = 24;
constexpr std::uint32_t kSevenZipMenuSeparator = 0x00000800U;
constexpr std::uint32_t kSevenZipMenuDisabled = 0x00000003U;

[[nodiscard]] int seven_zip_menu_label_width(const MenuItem& item) noexcept {
    const std::string label = menu_display_text(item.text);
    return std::max(46, static_cast<int>(label.size()) * 8 + 20);
}

struct SevenZipPopupGeometry {
    const MenuSlot* menu{nullptr};
    int x{0};
    int y{0};
    int width{0};
    int height{0};
};

[[nodiscard]] int seven_zip_top_menu_at(const WindowSlot& parent, const MenuSlot& menu,
                                        const int x, const int y) noexcept {
    if (y < 0 || y >= kSevenZipMenuBarHeight) {
        return -1;
    }
    int menu_x = 14;
    for (std::size_t index = 0; index < menu.logical_items.size(); ++index) {
        const MenuItem& item = menu.logical_items[index];
        if (menu_display_text(item.text).empty()) {
            continue;
        }
        const int width = seven_zip_menu_label_width(item);
        if (x >= menu_x && x < menu_x + width) {
            return static_cast<int>(index);
        }
        menu_x += width;
        if (menu_x >= std::max(parent.width, 1) - 20) {
            break;
        }
    }
    return -1;
}

[[nodiscard]] SevenZipPopupGeometry seven_zip_popup_geometry(const WindowSlot& parent) noexcept {
    SevenZipPopupGeometry geometry{};
    const MenuSlot* const menu = find_menu_slot(parent.menu_handle);
    if (menu == nullptr || parent.open_menu_index < 0 ||
        static_cast<std::size_t>(parent.open_menu_index) >= menu->logical_items.size()) {
        return geometry;
    }
    const MenuItem& top_item =
        menu->logical_items[static_cast<std::size_t>(parent.open_menu_index)];
    if (top_item.submenu == nullptr || top_item.submenu->logical_items.empty()) {
        return geometry;
    }

    int popup_x = 14;
    for (int index = 0; index < parent.open_menu_index; ++index) {
        const MenuItem& item = menu->logical_items[static_cast<std::size_t>(index)];
        if (!menu_display_text(item.text).empty()) {
            popup_x += seven_zip_menu_label_width(item);
        }
    }
    int popup_width = 170;
    for (const MenuItem& item : top_item.submenu->logical_items) {
        const std::string label = menu_display_text(item.text);
        const int arrow_width = item.submenu != nullptr ? 20 : 0;
        popup_width = std::max(popup_width, static_cast<int>(label.size()) * 8 + 34 + arrow_width);
    }
    const int available_width = std::max(parent.width, 1);
    if (popup_x + popup_width > available_width) {
        popup_x = std::max(0, available_width - popup_width);
    }
    geometry.menu = top_item.submenu;
    geometry.x = popup_x;
    geometry.y = kSevenZipMenuBarHeight;
    geometry.width = popup_width;
    geometry.height = static_cast<int>(geometry.menu->logical_items.size()) * kSevenZipMenuRowHeight;
    return geometry;
}

[[nodiscard]] int seven_zip_popup_item_at(const SevenZipPopupGeometry& geometry, const int x,
                                          const int y) noexcept {
    if (geometry.menu == nullptr || x < geometry.x || x >= geometry.x + geometry.width ||
        y < geometry.y || y >= geometry.y + geometry.height) {
        return -1;
    }
    const int index = (y - geometry.y) / kSevenZipMenuRowHeight;
    return index >= 0 && static_cast<std::size_t>(index) < geometry.menu->logical_items.size()
               ? index
               : -1;
}

[[nodiscard]] int seven_zip_list_row_at(const WindowSlot& parent, const int x,
                                        const int y) noexcept {
    const SevenZipListGeometry geometry = seven_zip_list_geometry(parent);
    if (geometry.status_y <= geometry.body_y || x < geometry.list_x ||
        x >= geometry.list_x + geometry.list_width) {
        return -1;
    }
    constexpr int kRowTop = 31;
    constexpr int kRowStep = 22;
    constexpr int kRowHeight = 21;
    if (y < geometry.body_y + kRowTop || y >= geometry.status_y) {
        return -1;
    }
    const int relative_y = y - geometry.body_y - kRowTop;
    if (relative_y % kRowStep >= kRowHeight) {
        return -1;
    }
    const int index = relative_y / kRowStep;
    const std::vector<ListViewRow> rows =
        collect_seven_zip_directory_rows(seven_zip_current_directory(parent));
    return index >= 0 && static_cast<std::size_t>(index) < rows.size() ? index : -1;
}

[[nodiscard]] int seven_zip_navigation_row_at(const WindowSlot& parent, const int x,
                                              const int y) noexcept {
    const SevenZipListGeometry geometry = seven_zip_list_geometry(parent);
    if (geometry.status_y <= geometry.body_y || x < 8 || x >= 8 + geometry.navigation_width) {
        return -1;
    }
    constexpr int kRowTop = 31;
    constexpr int kRowStep = 24;
    constexpr int kRowHeight = 22;
    if (y < geometry.body_y + kRowTop || y >= geometry.status_y) {
        return -1;
    }
    const int relative_y = y - geometry.body_y - kRowTop;
    if (relative_y % kRowStep >= kRowHeight) {
        return -1;
    }
    const int index = relative_y / kRowStep;
    return index >= 0 && index < 5 ? index : -1;
}

[[nodiscard]] bool open_seven_zip_directory_row(WindowSlot& parent, const int row) noexcept {
    try {
        const std::filesystem::path current_directory = seven_zip_current_directory(parent);
        const std::vector<ListViewRow> rows = collect_seven_zip_directory_rows(current_directory);
        if (row < 0) {
            return false;
        }
        const std::size_t selected = static_cast<std::size_t>(row);
        if (selected >= rows.size() || rows[selected].columns.size() < 2U ||
            rows[selected].columns[1] != "<DIR>" || rows[selected].columns[0].empty()) {
            return false;
        }
        const std::filesystem::path next_directory =
            rows[selected].columns[0] == ".." ? current_directory.parent_path()
                                               : current_directory / rows[selected].columns[0];
        std::error_code error;
        if (!std::filesystem::is_directory(next_directory, error) || error) {
            return false;
        }
        if (parent.visual_root_directory.empty()) {
            parent.visual_root_directory = current_directory;
        }
        parent.visual_directory = next_directory;
        parent.list_selection = 0;
        parent.last_list_press_row = -1;
        parent.last_list_press_time = {};
        clear_seven_zip_address_edit(parent);
        return true;
    } catch (...) {
        // A falha de conversão do caminho não pode derrubar o convidado.
        return false;
    }
}

void close_seven_zip_menu(WindowSlot& parent) noexcept {
    parent.open_menu_index = -1;
    parent.hovered_menu_item = -1;
    parent.pressed_menu_item = -1;
}

[[nodiscard]] bool seven_zip_menu_item_selectable(const MenuItem& item) noexcept {
    return (item.type & kSevenZipMenuSeparator) == 0U &&
           (item.state & kSevenZipMenuDisabled) == 0U && item.submenu == nullptr &&
           item.command_id != 0U;
}

void activate_seven_zip_menu_item(WindowSlot& parent, const SevenZipPopupGeometry& geometry,
                                  const int index) noexcept {
    if (geometry.menu == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= geometry.menu->logical_items.size()) {
        return;
    }
    const MenuItem& item = geometry.menu->logical_items[static_cast<std::size_t>(index)];
    if (seven_zip_menu_item_selectable(item)) {
        queue_window_message(parent, abi::kWmCommand,
                             static_cast<abi::Wparam>(item.command_id), 0);
    }
}

[[nodiscard]] int next_seven_zip_menu_item(const SevenZipPopupGeometry& geometry,
                                           const int current, const int direction) noexcept {
    if (geometry.menu == nullptr || geometry.menu->logical_items.empty() || direction == 0) {
        return -1;
    }
    const int count = static_cast<int>(geometry.menu->logical_items.size());
    int index = current < 0 ? (direction > 0 ? -1 : 0) : current;
    for (int step = 0; step < count; ++step) {
        index = (index + direction + count) % count;
        const MenuItem& item = geometry.menu->logical_items[static_cast<std::size_t>(index)];
        if ((item.type & kSevenZipMenuSeparator) == 0U &&
            (item.state & kSevenZipMenuDisabled) == 0U) {
            return index;
        }
    }
    return -1;
}

[[nodiscard]] bool handle_seven_zip_menu_mouse(WindowSlot& parent,
                                                const std::span<WindowSlot> windows,
                                                const gui::WindowEvent& event) noexcept {
    if (!is_seven_zip_file_manager(parent)) {
        return false;
    }
    const MenuSlot* const menu = find_menu_slot(parent.menu_handle);
    if (menu == nullptr || menu->logical_items.empty()) {
        close_seven_zip_menu(parent);
        return false;
    }

    if (event.type == gui::WindowEventType::Press &&
        event.y >= 0 && event.y < kSevenZipMenuBarHeight) {
        const int index = seven_zip_top_menu_at(parent, *menu, event.x, event.y);
        if (index >= 0) {
            parent.open_menu_index = index;
            parent.hovered_menu_item = -1;
            parent.pressed_menu_item = -1;
            set_focus_control(nullptr, g_focused_control);
            render_controls(parent, windows);
            return true;
        }
    }

    if (parent.open_menu_index < 0) {
        return false;
    }

    if ((event.type == gui::WindowEventType::Press ||
         event.type == gui::WindowEventType::Release ||
         event.type == gui::WindowEventType::MouseMove) &&
        event.y >= 0 && event.y < kSevenZipMenuBarHeight) {
        const int index = seven_zip_top_menu_at(parent, *menu, event.x, event.y);
        if (index >= 0) {
            if (event.type != gui::WindowEventType::Release || index != parent.open_menu_index) {
                parent.open_menu_index = index;
                parent.hovered_menu_item = -1;
                parent.pressed_menu_item = -1;
                render_controls(parent, windows);
            }
            return true;
        }
    }

    const SevenZipPopupGeometry geometry = seven_zip_popup_geometry(parent);
    const int item_index = seven_zip_popup_item_at(geometry, event.x, event.y);
    if (event.type == gui::WindowEventType::Press) {
        if (item_index >= 0) {
            parent.hovered_menu_item = item_index;
            parent.pressed_menu_item = item_index;
            render_controls(parent, windows);
            return true;
        }
        close_seven_zip_menu(parent);
        render_controls(parent, windows);
        return true;
    }
    if (event.type == gui::WindowEventType::MouseMove) {
        const int hovered = item_index;
        if (hovered != parent.hovered_menu_item) {
            parent.hovered_menu_item = hovered;
            render_controls(parent, windows);
        }
        return true;
    }
    if (event.type == gui::WindowEventType::Release) {
        if (item_index >= 0 && item_index == parent.pressed_menu_item &&
            geometry.menu != nullptr) {
            activate_seven_zip_menu_item(parent, geometry, item_index);
        }
        close_seven_zip_menu(parent);
        render_controls(parent, windows);
        return true;
    }
    return false;
}

[[nodiscard]] bool handle_seven_zip_file_list_mouse(WindowSlot& parent,
                                                    const std::span<WindowSlot> windows,
                                                    const gui::WindowEvent& event) noexcept {
    if (!is_seven_zip_file_manager(parent) ||
        (event.type != gui::WindowEventType::Press &&
         event.type != gui::WindowEventType::Release)) {
        return false;
    }
    const int row = seven_zip_list_row_at(parent, event.x, event.y);
    if (row < 0) {
        return false;
    }
    if (event.type == gui::WindowEventType::Press) {
        const auto now = std::chrono::steady_clock::now();
        constexpr auto kDoubleClickWindow = std::chrono::milliseconds{500};
        const bool double_click = parent.last_list_press_row == row &&
                                  parent.last_list_press_time !=
                                      std::chrono::steady_clock::time_point{} &&
                                  now - parent.last_list_press_time <= kDoubleClickWindow;
        parent.list_selection = row;
        parent.last_list_press_row = row;
        parent.last_list_press_time = now;
        if (double_click && open_seven_zip_directory_row(parent, row)) {
            // A abertura redefine a seleção para a primeira entrada da nova pasta.
        }
        render_controls(parent, windows);
    }
    return true;
}

[[nodiscard]] bool handle_seven_zip_navigation_mouse(
    WindowSlot& parent, const std::span<WindowSlot> windows,
    const gui::WindowEvent& event) noexcept {
    if (!is_seven_zip_file_manager(parent) ||
        (event.type != gui::WindowEventType::Press &&
         event.type != gui::WindowEventType::Release)) {
        return false;
    }
    const int row = seven_zip_navigation_row_at(parent, event.x, event.y);
    if (row < 0) {
        return false;
    }
    if (event.type == gui::WindowEventType::Press) {
        try {
            if (parent.visual_root_directory.empty()) {
                parent.visual_root_directory = seven_zip_current_directory(parent);
            }
            const std::filesystem::path target = seven_zip_navigation_directory(parent, row);
            std::error_code error;
            if (!target.empty() && std::filesystem::is_directory(target, error) && !error) {
                parent.visual_directory = target;
                parent.navigation_selection = row;
                parent.list_selection = 0;
                parent.last_list_press_row = -1;
                parent.last_list_press_time = {};
                clear_seven_zip_address_edit(parent);
                render_controls(parent, windows);
            }
        } catch (...) {
            // A falha de leitura do diretório não pode derrubar o convidado.
        }
    }
    return true;
}

[[nodiscard]] bool handle_seven_zip_address_mouse(
    WindowSlot& parent, const std::span<WindowSlot> windows,
    WindowSlot*& focused_control, const gui::WindowEvent& event) noexcept {
    if (!is_seven_zip_file_manager(parent) ||
        (event.type != gui::WindowEventType::Press &&
         event.type != gui::WindowEventType::Release) ||
        !seven_zip_address_hit(parent, event.x, event.y)) {
        return false;
    }
    if (event.type == gui::WindowEventType::Press) {
        set_focus_control(nullptr, focused_control);
        parent.address_editing = true;
        parent.address_error = false;
        parent.address_text = seven_zip_visual_address(parent);
        render_controls(parent, windows);
    }
    return true;
}

void draw_seven_zip_toolbar_button(const gui::NativeWindow native, const char* const icon,
                                   const char* const label, const int x, const int y,
                                   const int width, const bool pressed) noexcept {
    constexpr std::uint32_t kButton = 0xF8FAFCU;
    constexpr std::uint32_t kPressed = 0xD4E5F7U;
    constexpr std::uint32_t kBorder = 0xB7C2CCU;
    constexpr std::uint32_t kIcon = 0x245B8FU;
    constexpr std::uint32_t kText = 0x263442U;
    gui::platform::fill_rectangle_color(native, x, y, width, 36, pressed ? kPressed : kButton);
    gui::platform::draw_rectangle_color(native, x, y, width, 36, kBorder);
    gui::platform::draw_text_color(native, icon, x + 8, y + 23, kIcon, true);
    gui::platform::draw_text_color(native, label, x + 27, y + 23, kText);
}

[[nodiscard]] WindowSlot* seven_zip_toolbar_control(WindowSlot& parent,
                                                     const std::span<WindowSlot> windows) noexcept {
    for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
        if (it->used && it->is_control && it->parent == &parent && it->visible &&
            it->control_kind == ControlKind::Toolbar) {
            return &*it;
        }
    }
    return nullptr;
}

[[nodiscard]] int seven_zip_toolbar_button_at(const WindowSlot& toolbar, const int x,
                                              const int y) noexcept {
    constexpr int kToolbarY = 32;
    constexpr int kToolbarHeight = 36;
    if (y < kToolbarY || y >= kToolbarY + kToolbarHeight || x < 8) {
        return -1;
    }
    int button_x = 8;
    for (std::size_t index = 0; index < toolbar.toolbar_buttons.size(); ++index) {
        const int width = seven_zip_toolbar_button_width(
            toolbar.toolbar_buttons[index].command_id);
        if (x >= button_x && x < button_x + width) {
            return static_cast<int>(index);
        }
        button_x += width + 4;
    }
    return -1;
}

void render_seven_zip_file_manager(WindowSlot& parent,
                                   const std::span<WindowSlot> windows) noexcept {
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
    const SevenZipListGeometry list_geometry = seven_zip_list_geometry(parent);
    const int status_y = list_geometry.status_y;
    const int body_y = list_geometry.body_y;

    gui::platform::fill_rectangle_color(parent.native, 0, 0, width, height, kWindow);

    // File Manager's menu bar.
    gui::platform::fill_rectangle_color(parent.native, 0, 0, width, 28, kMenu);
    gui::platform::fill_rectangle_color(parent.native, 0, 27, width, 1, kBorder);
    const MenuSlot* const menu = find_menu_slot(parent.menu_handle);
    int menu_x = 14;
    if (menu != nullptr && !menu->logical_items.empty()) {
        for (const MenuItem& item : menu->logical_items) {
            const std::string label = menu_display_text(item.text);
            if (label.empty()) {
                continue;
            }
            gui::platform::draw_text_color(parent.native, label.c_str(), menu_x, 19, kText);
            menu_x += std::max(46, static_cast<int>(label.size()) * 8 + 20);
            if (menu_x >= width - 20) {
                break;
            }
        }
    } else {
        constexpr std::array<const char*, 5> kMenus{"File", "Edit", "View", "Tools", "Help"};
        constexpr std::array<int, 5> kMenu_x{14, 63, 111, 165, 220};
        for (std::size_t index = 0; index < kMenus.size(); ++index) {
            gui::platform::draw_text_color(parent.native, kMenus[index], kMenu_x[index], 19, kText);
        }
    }

    // The visual shell uses the command order supplied by the guest toolbar.
    // Labels/icons remain a small app-specific presentation map until the
    // resource-backed bitmap contract is supported.
    gui::platform::fill_rectangle_color(parent.native, 0, 28, width, 44, kToolbar);
    gui::platform::fill_rectangle_color(parent.native, 0, 71, width, 1, kBorder);
    WindowSlot* const logical_toolbar = seven_zip_toolbar_control(parent, windows);
    int toolbar_x = 8;
    if (logical_toolbar != nullptr && !logical_toolbar->toolbar_buttons.empty()) {
        for (std::size_t index = 0; index < logical_toolbar->toolbar_buttons.size(); ++index) {
            const std::int32_t command_id = logical_toolbar->toolbar_buttons[index].command_id;
            const SevenZipToolbarVisual* const visual = seven_zip_toolbar_visual(command_id);
            const int button_width = seven_zip_toolbar_button_width(command_id);
            const std::string fallback_label = "#" + std::to_string(command_id);
            draw_seven_zip_toolbar_button(
                parent.native, visual == nullptr ? "?" : visual->icon,
                visual == nullptr ? fallback_label.c_str() : visual->label, toolbar_x, 32,
                button_width,
                logical_toolbar->pressed_toolbar_index == static_cast<int>(index));
            toolbar_x += button_width + 4;
        }
    } else {
        for (const SevenZipToolbarVisual& visual : kSevenZipToolbarVisuals) {
            draw_seven_zip_toolbar_button(parent.native, visual.icon, visual.label, toolbar_x, 32,
                                          visual.width, false);
            toolbar_x += visual.width + 4;
        }
    }

    // Location bar.
    gui::platform::draw_text_color(parent.native, "Address", 10, 91, kMuted);
    const int address_x = 67;
    const int address_width = std::max(width - address_x - 10, 20);
    gui::platform::fill_rectangle_color(parent.native, address_x, 78, address_width, 24,
                                        kSurface);
    const std::string visual_address = seven_zip_visual_address(parent);
    const std::string& address_display = parent.address_editing ? parent.address_text
                                                                 : visual_address;
    const std::uint32_t address_border = parent.address_editing ? kBlue : kBorder;
    gui::platform::draw_rectangle_color(parent.native, address_x, 78, address_width, 24,
                                        address_border);
    gui::platform::draw_text_color(parent.native, address_display.c_str(), address_x + 9, 95,
                                   parent.address_error ? 0xB42318U : kText);
    if (parent.address_editing) {
        const int cursor_x = std::min(
            address_x + 9 + static_cast<int>(address_display.size()) * 8,
            address_x + address_width - 4);
        gui::platform::fill_rectangle_color(parent.native, cursor_x, 82, 1, 16, kBlue);
    }

    if (status_y > body_y) {
        // Navigation tree on the left and file list on the right.
        const int body_height = status_y - body_y;
        const int navigation_width = list_geometry.navigation_width;
        const int list_x = list_geometry.list_x;
        const int list_width = list_geometry.list_width;
        gui::platform::fill_rectangle_color(parent.native, 8, body_y, navigation_width,
                                            body_height, 0xF0F3F6U);
        gui::platform::draw_rectangle_color(parent.native, 8, body_y, navigation_width,
                                            body_height, kBorder);
        gui::platform::draw_text_color(parent.native, "Navigation", 20, body_y + 19, kText, true);
        constexpr std::array<const char*, 5> kNavigation_items{
            "Computer", "Local Disk (Z:)", "Home", "Desktop", "Documents"};
        for (std::size_t index = 0; index < kNavigation_items.size(); ++index) {
            const int row_y = body_y + 48 + static_cast<int>(index) * 24;
            if (static_cast<int>(index) == parent.navigation_selection) {
                gui::platform::fill_rectangle_color(parent.native, 9, row_y - 17,
                                                    navigation_width - 2, 22, kSelection);
            }
            gui::platform::draw_text_color(parent.native,
                                           static_cast<int>(index) == parent.navigation_selection
                                               ? "[+]"
                                               : "[ ]",
                                           20, row_y,
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

        const std::filesystem::path current_directory = seven_zip_current_directory(parent);
        const std::vector<ListViewRow> rows = collect_seven_zip_directory_rows(current_directory);
        if (rows.empty()) {
            gui::platform::draw_text_color(parent.native, "(diretorio indisponivel)", list_x + 65,
                                           body_y + 48, kMuted);
        }
        for (std::size_t index = 0; index < rows.size(); ++index) {
            const int row_y = body_y + 48 + static_cast<int>(index) * 22;
            if (row_y + 8 >= status_y) {
                break;
            }
            const int selected_row = parent.list_selection >= 0 ? parent.list_selection : 0;
            if (static_cast<int>(index) == selected_row) {
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
    const char* const status_text = parent.address_error ? "Pasta nao encontrada"
                                                          : "Visualizacao experimental";
    gui::platform::draw_text_color(parent.native, status_text, 10,
                                   std::min(status_y + 17, height - 4), kMuted);
    const int status_address_x = std::max(
        width - static_cast<int>(visual_address.size()) * 8 - 12, 10);
    gui::platform::draw_text_color(parent.native, visual_address.c_str(), status_address_x,
                                   std::min(status_y + 17, height - 4), kMuted);

    // O submenu fica na mesma superfície lógica da janela principal. Isso
    // preserva a hierarquia carregada do recurso Win32 e permite que o
    // próximo clique seja traduzido para WM_COMMAND pelo pump normal.
    const SevenZipPopupGeometry popup = seven_zip_popup_geometry(parent);
    if (popup.menu != nullptr) {
        constexpr std::uint32_t kPopup = 0xFFFFFFU;
        constexpr std::uint32_t kPopupBorder = 0x8998A8U;
        constexpr std::uint32_t kPopupSelection = 0xD4E5F7U;
        gui::platform::fill_rectangle_color(parent.native, popup.x, popup.y, popup.width,
                                            popup.height, kPopup);
        gui::platform::draw_rectangle_color(parent.native, popup.x, popup.y, popup.width,
                                             popup.height, kPopupBorder);
        for (std::size_t index = 0; index < popup.menu->logical_items.size(); ++index) {
            const MenuItem& item = popup.menu->logical_items[index];
            const int row_y = popup.y + static_cast<int>(index) * kSevenZipMenuRowHeight;
            if (row_y >= height || row_y + kSevenZipMenuRowHeight <= 0) {
                continue;
            }
            const bool separator = (item.type & kSevenZipMenuSeparator) != 0U;
            const bool disabled = (item.state & kSevenZipMenuDisabled) != 0U;
            if (static_cast<int>(index) == parent.hovered_menu_item && !disabled && !separator) {
                gui::platform::fill_rectangle_color(parent.native, popup.x + 1, row_y + 1,
                                                    std::max(popup.width - 2, 1),
                                                    kSevenZipMenuRowHeight - 2, kPopupSelection);
            }
            if (separator) {
                gui::platform::draw_rectangle_color(parent.native, popup.x + 9,
                                                     row_y + kSevenZipMenuRowHeight / 2,
                                                     std::max(popup.width - 18, 1), 1,
                                                     kPopupBorder);
                continue;
            }
            const std::string label = menu_display_text(item.text);
            const std::uint32_t text_color = disabled ? kMuted : kText;
            gui::platform::draw_text_color(parent.native, label.c_str(), popup.x + 12,
                                           row_y + 17, text_color, false);
            if (item.submenu != nullptr) {
                gui::platform::draw_text_color(parent.native, ">", popup.x + popup.width - 17,
                                               row_y + 17, text_color, true);
            }
        }
    }
    gui::platform::flush_window(parent.native);
}

}  // namespace

WindowSlot* find_control_at(WindowSlot& parent, const std::span<WindowSlot> windows,
                            const int x, const int y) noexcept {
    if (is_seven_zip_file_manager(parent)) {
        WindowSlot* const toolbar = seven_zip_toolbar_control(parent, windows);
        if (toolbar != nullptr && seven_zip_toolbar_button_at(*toolbar, x, y) >= 0) {
            return toolbar;
        }
    }
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

    if (is_seven_zip_file_manager(parent)) {
        render_seven_zip_file_manager(parent, windows);
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
    if (is_seven_zip_file_manager(parent) && parent.open_menu_index >= 0 &&
        event.type == gui::WindowEventType::KeyDown) {
        if (event.keysym == 0xFF1BUL) {  // XK_Escape
            close_seven_zip_menu(parent);
            render_controls(parent, windows);
            return;
        }
        const SevenZipPopupGeometry geometry = seven_zip_popup_geometry(parent);
        if (event.keysym == 0xFF54UL || event.keysym == 0xFF52UL) {  // XK_Down/XK_Up
            const int direction = event.keysym == 0xFF54UL ? 1 : -1;
            const int next = next_seven_zip_menu_item(geometry, parent.hovered_menu_item, direction);
            if (next >= 0) {
                parent.hovered_menu_item = next;
                parent.pressed_menu_item = -1;
                render_controls(parent, windows);
            }
            return;
        }
        if (event.keysym == 0xFF0DUL) {  // XK_Return
            activate_seven_zip_menu_item(parent, geometry, parent.hovered_menu_item);
            close_seven_zip_menu(parent);
            render_controls(parent, windows);
            return;
        }
        return;
    }
    if (is_seven_zip_file_manager(parent) && parent.open_menu_index < 0 &&
        parent.address_editing && event.type == gui::WindowEventType::KeyDown) {
        if (event.keysym == 0xFF1BUL) {  // XK_Escape
            clear_seven_zip_address_edit(parent);
            render_controls(parent, windows);
            return;
        }
        if (event.keysym == 0xFF0DUL) {  // XK_Return
            const std::filesystem::path target =
                seven_zip_path_from_address(parent, parent.address_text);
            std::error_code error;
            if (!target.empty() && std::filesystem::is_directory(target, error) && !error) {
                if (parent.visual_root_directory.empty()) {
                    parent.visual_root_directory = seven_zip_root_directory(parent);
                }
                parent.visual_directory = target;
                parent.list_selection = 0;
                parent.last_list_press_row = -1;
                parent.last_list_press_time = {};
                clear_seven_zip_address_edit(parent);
            } else {
                parent.address_error = true;
            }
            render_controls(parent, windows);
            return;
        }
        if (event.keysym == 0xFF08UL || event.keysym == 0xFFFFUL) {
            if (!parent.address_text.empty()) {
                parent.address_text.pop_back();
            }
            parent.address_error = false;
            render_controls(parent, windows);
            return;
        }
        if (event.character >= 0x20 && event.character != 0x7F) {
            parent.address_text.push_back(event.character);
            parent.address_error = false;
            render_controls(parent, windows);
        }
        return;
    }
    if (is_seven_zip_file_manager(parent) && parent.open_menu_index < 0 &&
        event.type == gui::WindowEventType::KeyDown && event.keysym == 0xFF0DUL &&
        parent.list_selection >= 0) {  // XK_Return: abre uma pasta selecionada
        if (open_seven_zip_directory_row(parent, parent.list_selection)) {
            render_controls(parent, windows);
        }
        return;
    }
    if (is_seven_zip_file_manager(parent) && parent.open_menu_index < 0 &&
        event.type == gui::WindowEventType::KeyDown &&
        (event.keysym == 0xFF52UL || event.keysym == 0xFF54UL)) {  // XK_Up/XK_Down
        try {
            const std::vector<ListViewRow> rows =
                collect_seven_zip_directory_rows(seven_zip_current_directory(parent));
            if (!rows.empty()) {
                const int direction = event.keysym == 0xFF54UL ? 1 : -1;
                const int last = static_cast<int>(rows.size()) - 1;
                if (parent.list_selection < 0) {
                    parent.list_selection = direction > 0 ? 0 : last;
                } else {
                    parent.list_selection =
                        std::clamp(parent.list_selection + direction, 0, last);
                }
                render_controls(parent, windows);
            }
        } catch (...) {
            // A falha de leitura do diretório não pode derrubar o convidado.
        }
        return;
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
    if (is_seven_zip_file_manager(parent) && event.type == gui::WindowEventType::Press &&
        parent.address_editing && !seven_zip_address_hit(parent, event.x, event.y)) {
        clear_seven_zip_address_edit(parent);
        render_controls(parent, windows);
    }
    if (handle_seven_zip_menu_mouse(parent, windows, event)) {
        return;
    }
    if (handle_seven_zip_file_list_mouse(parent, windows, event)) {
        return;
    }
    if (handle_seven_zip_navigation_mouse(parent, windows, event)) {
        return;
    }
    if (handle_seven_zip_address_mouse(parent, windows, focused_control, event)) {
        return;
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
        if (control->control_kind == ControlKind::Toolbar && control->parent != nullptr &&
            is_seven_zip_file_manager(*control->parent)) {
            control->pressed_toolbar_index = seven_zip_toolbar_button_at(*control, event.x, event.y);
        } else if (control->control_kind == ControlKind::Toolbar &&
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
            int index = -1;
            if (control->parent != nullptr && is_seven_zip_file_manager(*control->parent)) {
                index = seven_zip_toolbar_button_at(*control, event.x, event.y);
            } else {
                const int available_width = std::max(control->width, 16);
                const int fitting_width = std::max(
                    (available_width - 8) / static_cast<int>(control->toolbar_buttons.size()), 1);
                const int button_width = std::min(
                    fitting_width, control->toolbar_button_width > 0 ? control->toolbar_button_width
                                                                      : fitting_width);
                index = (event.x - control->x - 4) / button_width;
            }
            if (released_over_pressed_control && index == control->pressed_toolbar_index &&
                index >= 0 && static_cast<std::size_t>(index) < control->toolbar_buttons.size()) {
                queue_command_id(*control, 0,
                                 static_cast<std::uintptr_t>(
                                     control->toolbar_buttons[static_cast<std::size_t>(index)]
                                         .command_id));
            }
            control->pressed_toolbar_index = -1;
        }
        render_controls(parent, windows);
    }
}

}  // namespace tradutorlinux::runtime_gui
