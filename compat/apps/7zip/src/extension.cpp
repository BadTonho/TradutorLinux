#include "runtime/gui_controls.hpp"
#include "runtime/gui_extension.hpp"

#include "runtime_gui_state.hpp"
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

struct SevenZipWindowState {
    std::vector<int> open_menu_path;
    int hovered_menu_item{-1};
    int pressed_menu_item{-1};
    int hovered_list_row{-1};
    int last_list_press_row{-1};
    std::chrono::steady_clock::time_point last_list_press_time{};
    int navigation_selection{1};
    int hovered_navigation_row{-1};
    std::filesystem::path visual_root_directory;
    std::filesystem::path visual_directory;
    bool address_editing{false};
    bool address_error{false};
    std::string address_text;
    std::string last_operation_status;
};

class SevenZipRuntime final : public GuiExtensionRuntime {
public:
    struct Slot {
        WindowSlot* window{nullptr};
        std::array<char, 64> class_name{};
        SevenZipWindowState state{};
    };

    SevenZipWindowState& state_for(WindowSlot& window) noexcept {
        Slot* available = nullptr;
        for (Slot& slot : slots_) {
            if (slot.window == &window) {
                if (!same_class(slot, window.class_name)) {
                    slot.state = {};
                    bind_class(slot, window.class_name);
                }
                return slot.state;
            }
            if (available == nullptr && slot.window == nullptr) {
                available = &slot;
            }
        }
        if (available == nullptr) {
            return slots_[0].state;
        }
        available->window = &window;
        bind_class(*available, window.class_name);
        return available->state;
    }

private:
    static bool same_class(const Slot& slot, const std::string& class_name) noexcept {
        const std::size_t length = std::min(class_name.size(), slot.class_name.size() - 1U);
        return std::char_traits<char>::length(slot.class_name.data()) == length &&
               std::equal(slot.class_name.begin(), slot.class_name.begin() +
                                                    static_cast<std::ptrdiff_t>(length),
                          class_name.begin());
    }

    static void bind_class(Slot& slot, const std::string& class_name) noexcept {
        slot.class_name.fill('\0');
        const std::size_t length = std::min(class_name.size(), slot.class_name.size() - 1U);
        std::copy_n(class_name.data(), length, slot.class_name.data());
    }

    std::array<Slot, 32> slots_{};
};

[[nodiscard]] SevenZipRuntime& seven_zip_runtime() noexcept {
    return *static_cast<SevenZipRuntime*>(active_gui_extension_runtime());
}

[[nodiscard]] SevenZipWindowState& seven_zip_state(WindowSlot& window) noexcept {
    return seven_zip_runtime().state_for(window);
}

[[nodiscard]] const SevenZipWindowState& seven_zip_state(const WindowSlot& window) noexcept {
    return seven_zip_runtime().state_for(const_cast<WindowSlot&>(window));
}

[[nodiscard]] bool is_seven_zip_file_manager(const WindowSlot& parent) noexcept {
    return util::ascii_iequals(parent.class_name, "7-Zip::FM");
}

constexpr std::size_t kSevenZipDirectoryRowLimit = 128;

struct SevenZipDirectoryEntry {
    std::string name;
    std::string size;
    bool directory{false};
};

[[nodiscard]] std::vector<ListViewRow> collect_seven_zip_directory_rows(
    const std::filesystem::path& directory,
    std::size_t max_rows = kSevenZipDirectoryRowLimit) noexcept;

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
    return seven_zip_state(parent).visual_directory.empty() ? seven_zip_host_directory() : seven_zip_state(parent).visual_directory;
}

[[nodiscard]] std::filesystem::path seven_zip_root_directory(
    const WindowSlot& parent) noexcept {
    return seven_zip_state(parent).visual_root_directory.empty() ? seven_zip_host_directory()
                                                  : seven_zip_state(parent).visual_root_directory;
}

[[nodiscard]] std::filesystem::path seven_zip_home_directory() noexcept {
    const char* const home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        return {};
    }
    return std::filesystem::path{home};
}

[[nodiscard]] bool seven_zip_path_within(const std::filesystem::path& root,
                                          const std::filesystem::path& candidate) noexcept {
    if (root.empty() || candidate.empty()) {
        return false;
    }
    try {
        const std::string relative =
            candidate.lexically_normal().lexically_relative(root.lexically_normal()).generic_string();
        return relative.empty() || relative == "." ||
               (!relative.starts_with("../") && relative != "..");
    } catch (...) {
        return false;
    }
}

[[nodiscard]] std::filesystem::path seven_zip_copy_destination() noexcept {
    const char* const configured = std::getenv("TL_7ZFM_COPY_DESTINATION");
    if (configured == nullptr || configured[0] == '\0') {
        return {};
    }
    try {
        const std::filesystem::path destination{configured};
        return destination.is_absolute() ? destination : std::filesystem::path{};
    } catch (...) {
        return {};
    }
}

[[nodiscard]] std::filesystem::path seven_zip_selected_file(
    const WindowSlot& parent) noexcept {
    if (parent.list_selection < 0) {
        return {};
    }
    try {
        const std::filesystem::path current = seven_zip_current_directory(parent);
        const std::vector<ListViewRow> rows = collect_seven_zip_directory_rows(current);
        const std::size_t index = static_cast<std::size_t>(parent.list_selection);
        if (index >= rows.size() || rows[index].columns.empty() || rows[index].columns[0] == "..") {
            return {};
        }
        const std::filesystem::path candidate = current / rows[index].columns[0];
        const std::filesystem::file_status status = std::filesystem::symlink_status(candidate);
        return std::filesystem::is_regular_file(status) ? candidate : std::filesystem::path{};
    } catch (...) {
        return {};
    }
}

void trace_seven_zip_operation(const std::string_view operation, const std::string_view status,
                               const std::filesystem::path& source,
                               const std::filesystem::path& destination) noexcept {
    const std::string source_name = source.empty() ? std::string{} : source.filename().string();
    const std::string destination_name =
        destination.empty() ? std::string{} : destination.filename().string();
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", std::string{operation}},
        diagnostics::TraceField{"status", std::string{status}},
        diagnostics::TraceField{"source-name", source_name},
        diagnostics::TraceField{"destination-name", destination_name},
    };
    runtime_trace("SevenZipOperation", fields, 4);
}

void perform_seven_zip_copy(WindowSlot& parent) noexcept {
    const std::filesystem::path source = seven_zip_selected_file(parent);
    const std::filesystem::path destination_directory = seven_zip_copy_destination();
    const std::filesystem::path root = seven_zip_root_directory(parent);
    if (source.empty()) {
        seven_zip_state(parent).last_operation_status = "Copiar: selecione um arquivo";
        trace_seven_zip_operation("copy", "no-selection", source, destination_directory);
        return;
    }
    if (destination_directory.empty() ||
        !seven_zip_path_within(root, destination_directory)) {
        seven_zip_state(parent).last_operation_status = "Copiar: destino nao configurado";
        trace_seven_zip_operation("copy", "destination-outside-prefix", source,
                                  destination_directory);
        return;
    }
    std::error_code error;
    if (!std::filesystem::is_directory(destination_directory, error) || error) {
        seven_zip_state(parent).last_operation_status = "Copiar: destino indisponivel";
        trace_seven_zip_operation("copy", "destination-not-directory", source,
                                  destination_directory);
        return;
    }
    const std::filesystem::path destination = destination_directory / source.filename();
    if (!seven_zip_path_within(root, source) || !seven_zip_path_within(root, destination)) {
        seven_zip_state(parent).last_operation_status = "Copiar: caminho fora da raiz";
        trace_seven_zip_operation("copy", "path-outside-root", source, destination);
        return;
    }
    error.clear();
    const bool copied = std::filesystem::copy_file(source, destination, error);
    if (!copied || error) {
        seven_zip_state(parent).last_operation_status = "Copiar: falha (arquivo ja existe ou inacessivel)";
        trace_seven_zip_operation("copy", "failed", source, destination);
        return;
    }
    seven_zip_state(parent).last_operation_status = "Copiado: " + source.filename().string();
    trace_seven_zip_operation("copy", "success", source, destination);
}

void perform_seven_zip_command(WindowSlot& parent, const std::uintptr_t command_id) noexcept {
    switch (command_id) {
        case 546U:  // Copy, conforme o idCommand da toolbar real do 7-Zip.
            perform_seven_zip_copy(parent);
            return;
        default:
            trace_seven_zip_operation("command", "not-supported", {}, {});
            return;
    }
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
    seven_zip_state(parent).address_editing = false;
    seven_zip_state(parent).address_error = false;
    seven_zip_state(parent).address_text.clear();
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

[[nodiscard]] int seven_zip_open_menu_path_index(const WindowSlot& parent,
                                                 const std::size_t level) noexcept {
    if (!seven_zip_state(parent).open_menu_path.empty()) {
        return level < seven_zip_state(parent).open_menu_path.size() ? seven_zip_state(parent).open_menu_path[level] : -1;
    }
    return level == 0 ? parent.open_menu_index : -1;
}

[[nodiscard]] std::size_t seven_zip_open_menu_path_size(const WindowSlot& parent) noexcept {
    return seven_zip_state(parent).open_menu_path.empty() ? (parent.open_menu_index >= 0 ? 1U : 0U)
                                         : seven_zip_state(parent).open_menu_path.size();
}

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

[[nodiscard]] int seven_zip_popup_width(const MenuSlot& menu) noexcept {
    int width = 170;
    for (const MenuItem& item : menu.logical_items) {
        const std::string label = menu_display_text(item.text);
        const int arrow_width = item.submenu != nullptr ? 20 : 0;
        width = std::max(width, static_cast<int>(label.size()) * 8 + 34 + arrow_width);
    }
    return width;
}

[[nodiscard]] SevenZipPopupGeometry seven_zip_popup_geometry(const WindowSlot& parent) noexcept {
    SevenZipPopupGeometry geometry{};
    const MenuSlot* menu = find_menu_slot(parent.menu_handle);
    const std::size_t path_size = seven_zip_open_menu_path_size(parent);
    if (menu == nullptr || path_size == 0U) {
        return geometry;
    }

    int parent_popup_x = 0;
    int parent_popup_y = 0;
    int parent_popup_width = 0;
    int parent_item_index = 0;
    int popup_x = 14;
    int popup_y = kSevenZipMenuBarHeight;
    int popup_width = 0;
    for (std::size_t level = 0; level < path_size; ++level) {
        const int item_index = seven_zip_open_menu_path_index(parent, level);
        if (item_index < 0 || static_cast<std::size_t>(item_index) >= menu->logical_items.size()) {
            return {};
        }
        const MenuItem& item = menu->logical_items[static_cast<std::size_t>(item_index)];
        if (item.submenu == nullptr || item.submenu->logical_items.empty()) {
            return {};
        }

        if (level == 0U) {
            popup_x = 14;
            for (int index = 0; index < item_index; ++index) {
                const MenuItem& previous = menu->logical_items[static_cast<std::size_t>(index)];
                if (!menu_display_text(previous.text).empty()) {
                    popup_x += seven_zip_menu_label_width(previous);
                }
            }
            popup_y = kSevenZipMenuBarHeight;
        } else {
            popup_x = parent_popup_x + parent_popup_width - 2;
            popup_y = parent_popup_y + parent_item_index * kSevenZipMenuRowHeight;
        }

        popup_width = seven_zip_popup_width(*item.submenu);
        const int available_width = std::max(parent.width, 1);
        if (popup_x + popup_width > available_width) {
            popup_x = level == 0U ? std::max(0, available_width - popup_width)
                                  : std::max(0, parent_popup_x - popup_width + 2);
        }
        parent_popup_x = popup_x;
        parent_popup_y = popup_y;
        parent_popup_width = popup_width;
        parent_item_index = item_index;
        menu = item.submenu;
    }

    geometry.menu = menu;
    geometry.x = popup_x;
    geometry.y = popup_y;
    geometry.width = popup_width;
    geometry.height = static_cast<int>(menu->logical_items.size()) * kSevenZipMenuRowHeight;
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
        if (seven_zip_state(parent).visual_root_directory.empty()) {
            seven_zip_state(parent).visual_root_directory = current_directory;
        }

        seven_zip_state(parent).visual_directory = next_directory;
        parent.list_selection = 0;
        seven_zip_state(parent).last_list_press_row = -1;
        seven_zip_state(parent).last_list_press_time = {};
        clear_seven_zip_address_edit(parent);
        return true;
    } catch (...) {
        // A falha de conversão do caminho não pode derrubar o convidado.
        return false;
    }
}

void close_seven_zip_menu(WindowSlot& parent) noexcept {
    parent.open_menu_index = -1;
    seven_zip_state(parent).open_menu_path.clear();
    seven_zip_state(parent).hovered_menu_item = -1;
    seven_zip_state(parent).pressed_menu_item = -1;
}

[[nodiscard]] bool open_seven_zip_submenu(WindowSlot& parent, const int index) noexcept {
    const SevenZipPopupGeometry geometry = seven_zip_popup_geometry(parent);
    if (geometry.menu == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= geometry.menu->logical_items.size() ||
        geometry.menu->logical_items[static_cast<std::size_t>(index)].submenu == nullptr) {
        return false;
    }
    try {
        seven_zip_state(parent).open_menu_path.push_back(index);
        seven_zip_state(parent).hovered_menu_item = -1;
        seven_zip_state(parent).pressed_menu_item = -1;
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool open_seven_zip_root_menu(WindowSlot& parent, const int index) noexcept {
    try {
        parent.open_menu_index = index;
        seven_zip_state(parent).open_menu_path.clear();
        seven_zip_state(parent).open_menu_path.push_back(index);
        seven_zip_state(parent).hovered_menu_item = -1;
        seven_zip_state(parent).pressed_menu_item = -1;
        return true;
    } catch (...) {
        close_seven_zip_menu(parent);
        return false;
    }
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
        perform_seven_zip_command(parent, item.command_id);
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
            if (!open_seven_zip_root_menu(parent, index)) {
                return true;
            }
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
                if (!open_seven_zip_root_menu(parent, index)) {
                    return true;
                }
                render_controls(parent, windows);
            }
            return true;
        }
    }

    const SevenZipPopupGeometry geometry = seven_zip_popup_geometry(parent);
    const int item_index = seven_zip_popup_item_at(geometry, event.x, event.y);
    if (event.type == gui::WindowEventType::Press) {
        if (item_index >= 0) {
            seven_zip_state(parent).hovered_menu_item = item_index;
            seven_zip_state(parent).pressed_menu_item = item_index;
            render_controls(parent, windows);
            return true;
        }
        close_seven_zip_menu(parent);
        render_controls(parent, windows);
        return true;
    }
    if (event.type == gui::WindowEventType::MouseMove) {
        const int hovered = item_index;
        if (hovered != seven_zip_state(parent).hovered_menu_item) {
            seven_zip_state(parent).hovered_menu_item = hovered;
            render_controls(parent, windows);
        }
        return true;
    }
    if (event.type == gui::WindowEventType::Release) {
        if (item_index >= 0 && item_index == seven_zip_state(parent).pressed_menu_item &&
            geometry.menu != nullptr) {
            const MenuItem& item = geometry.menu->logical_items[static_cast<std::size_t>(item_index)];
            if (item.submenu != nullptr) {
                (void)open_seven_zip_submenu(parent, item_index);
                render_controls(parent, windows);
                return true;
            }
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
        const bool double_click = seven_zip_state(parent).last_list_press_row == row &&
                                  seven_zip_state(parent).last_list_press_time !=
                                      std::chrono::steady_clock::time_point{} &&
                                  now - seven_zip_state(parent).last_list_press_time <= kDoubleClickWindow;
        parent.list_selection = row;
        seven_zip_state(parent).last_list_press_row = row;
        seven_zip_state(parent).last_list_press_time = now;
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
            if (seven_zip_state(parent).visual_root_directory.empty()) {
                seven_zip_state(parent).visual_root_directory = seven_zip_current_directory(parent);
            }
            const std::filesystem::path target = seven_zip_navigation_directory(parent, row);
            std::error_code error;
            if (!target.empty() && std::filesystem::is_directory(target, error) && !error) {
                seven_zip_state(parent).visual_directory = target;
                seven_zip_state(parent).navigation_selection = row;
                parent.list_selection = 0;
                seven_zip_state(parent).last_list_press_row = -1;
                seven_zip_state(parent).last_list_press_time = {};
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
        seven_zip_state(parent).address_editing = true;
        seven_zip_state(parent).address_error = false;
        seven_zip_state(parent).address_text = seven_zip_visual_address(parent);
        render_controls(parent, windows);
    }
    return true;
}

void draw_seven_zip_toolbar_button(const gui::NativeWindow native, const char* const icon,
                                   const char* const label, const int x, const int y,
                                   const int width, const bool pressed, const bool hovered) noexcept {
    constexpr std::uint32_t kButton = 0xF8FAFCU;
    constexpr std::uint32_t kPressed = 0xD4E5F7U;
    constexpr std::uint32_t kHover = 0xEAF2FAU;
    constexpr std::uint32_t kBorder = 0xB7C2CCU;
    constexpr std::uint32_t kIcon = 0x245B8FU;
    constexpr std::uint32_t kText = 0x263442U;
    const std::uint32_t fill = pressed ? kPressed : hovered ? kHover : kButton;
    gui::platform::fill_rectangle_color(native, x, y, width, 36, fill);
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

[[nodiscard]] bool handle_seven_zip_hover_mouse(
    WindowSlot& parent, const std::span<WindowSlot> windows,
    const gui::WindowEvent& event) noexcept {
    if (!is_seven_zip_file_manager(parent) || event.type != gui::WindowEventType::MouseMove) {
        return false;
    }

    bool changed = false;
    WindowSlot* const toolbar = seven_zip_toolbar_control(parent, windows);
    const int hovered_toolbar = toolbar == nullptr
                                    ? -1
                                    : seven_zip_toolbar_button_at(*toolbar, event.x, event.y);
    if (toolbar != nullptr && toolbar->hovered_toolbar_index != hovered_toolbar) {
        toolbar->hovered_toolbar_index = hovered_toolbar;
        changed = true;
    }

    const int hovered_row = seven_zip_list_row_at(parent, event.x, event.y);
    if (seven_zip_state(parent).hovered_list_row != hovered_row) {
        seven_zip_state(parent).hovered_list_row = hovered_row;
        changed = true;
    }
    const int hovered_navigation = seven_zip_navigation_row_at(parent, event.x, event.y);
    if (seven_zip_state(parent).hovered_navigation_row != hovered_navigation) {
        seven_zip_state(parent).hovered_navigation_row = hovered_navigation;
        changed = true;
    }
    if (changed) {
        render_controls(parent, windows);
    }
    return changed || hovered_toolbar >= 0 || hovered_row >= 0;
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
    constexpr std::uint32_t kHover = 0xEAF2FAU;
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
                logical_toolbar->pressed_toolbar_index == static_cast<int>(index),
                logical_toolbar->hovered_toolbar_index == static_cast<int>(index));
            toolbar_x += button_width + 4;
        }
    } else {
        for (const SevenZipToolbarVisual& visual : kSevenZipToolbarVisuals) {
            draw_seven_zip_toolbar_button(parent.native, visual.icon, visual.label, toolbar_x, 32,
                                          visual.width, false, false);
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
    const std::string& address_display = seven_zip_state(parent).address_editing ? seven_zip_state(parent).address_text
                                                                 : visual_address;
    const std::uint32_t address_border = seven_zip_state(parent).address_editing ? kBlue : kBorder;
    gui::platform::draw_rectangle_color(parent.native, address_x, 78, address_width, 24,
                                        address_border);
    gui::platform::draw_text_color(parent.native, address_display.c_str(), address_x + 9, 95,
                                   seven_zip_state(parent).address_error ? 0xB42318U : kText);
    if (seven_zip_state(parent).address_editing) {
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
            if (static_cast<int>(index) == seven_zip_state(parent).navigation_selection) {
                gui::platform::fill_rectangle_color(parent.native, 9, row_y - 17,
                                                    navigation_width - 2, 22, kSelection);
            } else if (static_cast<int>(index) == seven_zip_state(parent).hovered_navigation_row) {
                gui::platform::fill_rectangle_color(parent.native, 9, row_y - 17,
                                                    navigation_width - 2, 22, kHover);
            }
            gui::platform::draw_text_color(parent.native,
                                           static_cast<int>(index) == seven_zip_state(parent).navigation_selection
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
            } else if (static_cast<int>(index) == seven_zip_state(parent).hovered_list_row) {
                gui::platform::fill_rectangle_color(parent.native, list_x + 1, row_y - 17,
                                                    std::max(list_width - 2, 1), 21, kHover);
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
    const std::string status_text = !seven_zip_state(parent).last_operation_status.empty()
                                        ? seven_zip_state(parent).last_operation_status
                                        : seven_zip_state(parent).address_error ? "Pasta nao encontrada"
                                                                : "Visualizacao experimental";
    gui::platform::draw_text_color(parent.native, status_text.c_str(), 10,
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
            if (static_cast<int>(index) == seven_zip_state(parent).hovered_menu_item && !disabled && !separator) {
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

std::vector<ListViewRow> collect_seven_zip_directory_rows(
    const std::filesystem::path& directory,
    const std::size_t max_rows) noexcept {
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


class SevenZipExtension final : public GuiExtension {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return "7zip";
    }

    [[nodiscard]] std::unique_ptr<GuiExtensionRuntime> create_runtime() const noexcept override {
        try {
            return std::make_unique<SevenZipRuntime>();
        } catch (...) {
            return nullptr;
        }
    }

    [[nodiscard]] bool matches(const WindowSlot& parent) const noexcept override {
        return is_seven_zip_file_manager(parent);
    }

    [[nodiscard]] bool render(GuiExtensionRuntime&, WindowSlot& parent,
                              const std::span<WindowSlot> windows) const noexcept override {
        render_seven_zip_file_manager(parent, windows);
        return true;
    }

    [[nodiscard]] bool handle_key(GuiExtensionRuntime&, WindowSlot& parent,
                                  const std::span<WindowSlot> windows,
                                  WindowSlot*& focused_control,
                                  const gui::WindowEvent& event) const noexcept override {
        if (!is_seven_zip_file_manager(parent)) {
            return false;
        }
        SevenZipWindowState& state = seven_zip_state(parent);
        if (parent.open_menu_index >= 0 && event.type == gui::WindowEventType::KeyDown) {
            if (event.keysym == 0xFF1BUL) {
                close_seven_zip_menu(parent);
                render_controls(parent, windows);
                return true;
            }
            const SevenZipPopupGeometry geometry = seven_zip_popup_geometry(parent);
            if (event.keysym == 0xFF54UL || event.keysym == 0xFF52UL) {
                const int direction = event.keysym == 0xFF54UL ? 1 : -1;
                const int next = next_seven_zip_menu_item(geometry, state.hovered_menu_item, direction);
                if (next >= 0) {
                    state.hovered_menu_item = next;
                    state.pressed_menu_item = -1;
                    render_controls(parent, windows);
                }
                return true;
            }
            if (event.keysym == 0xFF0DUL) {
                if (geometry.menu != nullptr && state.hovered_menu_item >= 0 &&
                    static_cast<std::size_t>(state.hovered_menu_item) <
                        geometry.menu->logical_items.size() &&
                    geometry.menu->logical_items[static_cast<std::size_t>(state.hovered_menu_item)]
                            .submenu != nullptr) {
                    (void)open_seven_zip_submenu(parent, state.hovered_menu_item);
                    render_controls(parent, windows);
                    return true;
                }
                activate_seven_zip_menu_item(parent, geometry, state.hovered_menu_item);
                close_seven_zip_menu(parent);
                render_controls(parent, windows);
                return true;
            }
            if (event.keysym == 0xFF53UL) {
                if (geometry.menu != nullptr && state.hovered_menu_item >= 0 &&
                    static_cast<std::size_t>(state.hovered_menu_item) <
                        geometry.menu->logical_items.size() &&
                    geometry.menu->logical_items[static_cast<std::size_t>(state.hovered_menu_item)]
                            .submenu != nullptr &&
                    open_seven_zip_submenu(parent, state.hovered_menu_item)) {
                    render_controls(parent, windows);
                }
                return true;
            }
            if (event.keysym == 0xFF51UL) {
                if (state.open_menu_path.size() > 1U) {
                    state.open_menu_path.pop_back();
                    state.hovered_menu_item = -1;
                    state.pressed_menu_item = -1;
                    render_controls(parent, windows);
                } else {
                    close_seven_zip_menu(parent);
                    render_controls(parent, windows);
                }
                return true;
            }
            return true;
        }
        if (parent.open_menu_index < 0 && state.address_editing &&
            event.type == gui::WindowEventType::KeyDown) {
            if (event.keysym == 0xFF1BUL) {
                clear_seven_zip_address_edit(parent);
                render_controls(parent, windows);
                return true;
            }
            if (event.keysym == 0xFF0DUL) {
                const std::filesystem::path target =
                    seven_zip_path_from_address(parent, state.address_text);
                std::error_code error;
                if (!target.empty() && std::filesystem::is_directory(target, error) && !error) {
                    if (state.visual_root_directory.empty()) {
                        state.visual_root_directory = seven_zip_root_directory(parent);
                    }
                    state.visual_directory = target;
                    parent.list_selection = 0;
                    state.last_list_press_row = -1;
                    state.last_list_press_time = {};
                    clear_seven_zip_address_edit(parent);
                } else {
                    state.address_error = true;
                }
                render_controls(parent, windows);
                return true;
            }
            if (event.keysym == 0xFF08UL || event.keysym == 0xFFFFUL) {
                if (!state.address_text.empty()) {
                    state.address_text.pop_back();
                }
                state.address_error = false;
                render_controls(parent, windows);
                return true;
            }
            if (event.character >= 0x20 && event.character != 0x7F) {
                state.address_text.push_back(event.character);
                state.address_error = false;
                render_controls(parent, windows);
            }
            return true;
        }
        if (parent.open_menu_index < 0 && event.type == gui::WindowEventType::KeyDown &&
            event.keysym == 0xFF0DUL && parent.list_selection >= 0) {
            if (open_seven_zip_directory_row(parent, parent.list_selection)) {
                render_controls(parent, windows);
            }
            return true;
        }
        if (parent.open_menu_index < 0 && event.type == gui::WindowEventType::KeyDown &&
            (event.keysym == 0xFF52UL || event.keysym == 0xFF54UL)) {
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
            }
            return true;
        }
        (void)focused_control;
        return false;
    }

    [[nodiscard]] bool handle_mouse(GuiExtensionRuntime&, WindowSlot& parent,
                                    const std::span<WindowSlot> windows,
                                    WindowSlot*& focused_control,
                                    const gui::WindowEvent& event) const noexcept override {
        if (!is_seven_zip_file_manager(parent)) {
            return false;
        }
        SevenZipWindowState& state = seven_zip_state(parent);
        if (event.type == gui::WindowEventType::Press && state.address_editing &&
            !seven_zip_address_hit(parent, event.x, event.y)) {
            clear_seven_zip_address_edit(parent);
            render_controls(parent, windows);
        }
        if (handle_seven_zip_menu_mouse(parent, windows, event) ||
            handle_seven_zip_hover_mouse(parent, windows, event) ||
            handle_seven_zip_file_list_mouse(parent, windows, event) ||
            handle_seven_zip_navigation_mouse(parent, windows, event) ||
            handle_seven_zip_address_mouse(parent, windows, focused_control, event)) {
            return true;
        }

        WindowSlot* const toolbar = seven_zip_toolbar_control(parent, windows);
        const int toolbar_index = toolbar == nullptr
                                      ? -1
                                      : seven_zip_toolbar_button_at(*toolbar, event.x, event.y);
        if (toolbar != nullptr && event.type == gui::WindowEventType::Press &&
            toolbar_index >= 0) {
            set_focus_control(nullptr, focused_control);
            toolbar->pressed = true;
            toolbar->pressed_toolbar_index = toolbar_index;
            toolbar->hovered_toolbar_index = toolbar_index;
            render_controls(parent, windows);
            return true;
        }
        if (toolbar != nullptr && event.type == gui::WindowEventType::Release &&
            toolbar->pressed) {
            const int index = toolbar_index;
            const bool released_over_pressed_control = index >= 0;
            toolbar->pressed = false;
            if (released_over_pressed_control && index == toolbar->pressed_toolbar_index &&
                static_cast<std::size_t>(index) < toolbar->toolbar_buttons.size()) {
                const std::uintptr_t command_id = static_cast<std::uintptr_t>(
                    toolbar->toolbar_buttons[static_cast<std::size_t>(index)].command_id);
                perform_seven_zip_command(parent, command_id);
                queue_command_id(*toolbar, 0, command_id);
            }
            toolbar->hovered_toolbar_index = index;
            toolbar->pressed_toolbar_index = -1;
            render_controls(parent, windows);
            return true;
        }
        return false;
    }
};

}  // namespace

SevenZipExtension g_seven_zip_extension{};

void register_seven_zip_extension() noexcept {
    register_gui_extension(g_seven_zip_extension);
}

}  // namespace tradutorlinux::runtime_gui

namespace tradutorlinux::compat::seven_zip {

void register_extension() noexcept {
    runtime_gui::register_seven_zip_extension();
}

}  // namespace tradutorlinux::compat::seven_zip
