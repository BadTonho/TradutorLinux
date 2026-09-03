#include "tradutorlinux/runtime/winapi.hpp"
#include "runtime_context.hpp"
#include "tradutorlinux/gui/platform.hpp"
#include "tradutorlinux/runtime/dialog_template.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace tradutorlinux {

namespace {

using WndProc = TL_MSABI abi::Lresult (*)(abi::HWnd, std::uint32_t, abi::Wparam, abi::Lparam);

constexpr int kMaxGuestWindowDimension = 8192;

[[nodiscard]] int guest_window_dimension(const int value, const int fallback) noexcept {
    return value > 0 && value <= kMaxGuestWindowDimension ? value : fallback;
}

abi::Lresult call_wndproc(const std::uintptr_t wndproc, const abi::HWnd hwnd,
                          const std::uint32_t message, const abi::Wparam wparam,
                          const abi::Lparam lparam) noexcept {
    return std::bit_cast<WndProc>(wndproc)(hwnd, message, wparam, lparam);
}

void render_controls(WindowSlot& parent) noexcept {
    runtime_gui::render_controls(parent, std::span<WindowSlot>{g_windows});
}

void handle_control_key(WindowSlot& parent, const gui::WindowEvent& event) noexcept {
    runtime_gui::handle_control_key(parent, std::span<WindowSlot>{g_windows}, g_focused_control,
                                    event);
}

void handle_control_mouse(WindowSlot& parent, const gui::WindowEvent& event) noexcept {
    runtime_gui::handle_control_mouse(parent, std::span<WindowSlot>{g_windows}, g_focused_control,
                                      event);
}

void set_focus_control(WindowSlot* control) noexcept {
    runtime_gui::set_focus_control(control, g_focused_control);
}

void write_guest_msg(void* const msg, const abi::HWnd hwnd, const std::uint32_t message,
                     const abi::Wparam wparam, const abi::Lparam lparam) noexcept {
    abi::GuestMsg* const out = static_cast<abi::GuestMsg*>(msg);
    out->hwnd = hwnd;
    out->message = message;
    out->padding = 0;
    out->wparam = wparam;
    out->lparam = lparam;
    out->time = 0;
    out->pt_x = 0;
    out->pt_y = 0;
}

constexpr unsigned long kKeysymBackspace = 0xFF08;
constexpr unsigned long kKeysymTab = 0xFF09;
constexpr unsigned long kKeysymReturn = 0xFF0D;
constexpr unsigned long kKeysymEscape = 0xFF1B;
constexpr unsigned long kKeysymLeft = 0xFF51;
constexpr unsigned long kKeysymUp = 0xFF52;
constexpr unsigned long kKeysymRight = 0xFF53;
constexpr unsigned long kKeysymDown = 0xFF54;
constexpr unsigned long kKeysymDelete = 0xFFFF;

constexpr std::uint32_t kWsVisible = 0x10000000U;
constexpr std::uint32_t kWsDisabled = 0x08000000U;
constexpr std::uint32_t kWsTabStop = 0x00010000U;
constexpr std::uint32_t kImageIcon = 1U;
constexpr int kIdOk = 1;
constexpr int kIdCancel = 2;

struct ImageSlot {
    bool used{false};
    const void* source{nullptr};
};

std::array<ImageSlot, 32> g_image_slots{};

[[nodiscard]] bool guest_callback_address_valid(const std::uintptr_t address) noexcept {
    if (address == 0 || g_guest_image_base == nullptr ||
        address < reinterpret_cast<std::uintptr_t>(g_guest_image_base) ||
        address - reinterpret_cast<std::uintptr_t>(g_guest_image_base) >= g_guest_image_size) {
        return false;
    }
    return runtime::validate_mapped_range(reinterpret_cast<const void*>(address), 1, false);
}

[[nodiscard]] bool guest_resource_or_wstring_valid(const std::uint16_t* value) noexcept {
    const auto raw = reinterpret_cast<std::uintptr_t>(value);
    return raw <= 0xFFFFU || mapped_guest_wstring(value);
}

[[nodiscard]] WindowSlot* dialog_control_by_id(WindowSlot& dialog, const int identifier) noexcept {
    for (WindowSlot* const child : dialog.dialog_children) {
        if (child != nullptr && child->used &&
            child->parent == &dialog &&
            child->control_id == static_cast<std::uint16_t>(identifier)) {
            return child;
        }
    }
    return nullptr;
}

[[nodiscard]] bool dialog_control_eligible(const WindowSlot& control) noexcept {
    return control.used && control.is_control && control.visible && control.enabled &&
           (control.style & kWsTabStop) != 0U;
}

[[nodiscard]] WindowSlot* next_dialog_tab_item(WindowSlot& dialog, WindowSlot* current,
                                                const bool previous) noexcept {
    std::vector<WindowSlot*> eligible;
    for (WindowSlot* const child : dialog.dialog_children) {
        if (child != nullptr && child->parent == &dialog && dialog_control_eligible(*child)) {
            eligible.push_back(child);
        }
    }
    if (eligible.empty()) {
        return nullptr;
    }
    if (current == nullptr) {
        return previous ? eligible.back() : eligible.front();
    }
    const auto found = std::find(eligible.begin(), eligible.end(), current);
    if (found == eligible.end()) {
        return previous ? eligible.back() : eligible.front();
    }
    const std::size_t index = static_cast<std::size_t>(found - eligible.begin());
    if (previous) {
        return eligible[(index + eligible.size() - 1U) % eligible.size()];
    }
    return eligible[(index + 1U) % eligible.size()];
}

[[nodiscard]] abi::Wparam keydown_vkey(const unsigned long keysym,
                                       const char character) noexcept {
    switch (keysym) {
        case kKeysymBackspace: return abi::kVkBack;
        case kKeysymTab: return abi::kVkTab;
        case kKeysymReturn: return abi::kVkReturn;
        case kKeysymEscape: return abi::kVkEscape;
        case kKeysymLeft: return abi::kVkLeft;
        case kKeysymUp: return abi::kVkUp;
        case kKeysymRight: return abi::kVkRight;
        case kKeysymDown: return abi::kVkDown;
        case kKeysymDelete: return abi::kVkDelete;
        default: break;
    }
    const auto value = static_cast<std::uint32_t>(static_cast<unsigned char>(character));
    if (value >= 'a' && value <= 'z') {
        return static_cast<abi::Wparam>(value - 0x20U);
    }
    return static_cast<abi::Wparam>(value);
}

}  // namespace

extern "C" {

TL_MSABI int tl_MessageBoxA(const void* const window, const char* const text,
                            const char* const caption, const std::uint32_t type) noexcept {
    (void)window;
    if (type != 0 || !mapped_guest_cstring(text) || !mapped_guest_cstring(caption)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uint32_t result = gui::platform::message_box(text, caption);
    set_last_error(result == 0 ? abi::kErrorAccessDenied : abi::kErrorSuccess);
    return static_cast<int>(result);
}

TL_MSABI abi::Atom tl_RegisterClassExA(const void* const wnd_class) noexcept {
    if (wnd_class == nullptr ||
        !mapped_guest_range(wnd_class, sizeof(abi::GuestWndClassExA), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("RegisterClassExA", "wnd-class", "estrutura WNDCLASSEXA inválida");
        return 0;
    }
    const auto* const wc = static_cast<const abi::GuestWndClassExA*>(wnd_class);
    if (wc->cb_size < sizeof(abi::GuestWndClassExA) || wc->window_proc == 0 ||
        wc->class_name == nullptr || !mapped_guest_cstring(wc->class_name) ||
        !mapped_guest_range(std::bit_cast<const void*>(wc->window_proc), 1, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("RegisterClassExA", "wnd-class", "cbSize, window_proc ou class_name inválido");
        return 0;
    }
    if (find_class_slot(wc->class_name) != nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto free_it = std::find_if(g_classes.begin(), g_classes.end(),
                                      [](const ClassSlot& slot) { return !slot.used; });
    if (free_it == g_classes.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    ClassSlot& slot = *free_it;
    slot.used = true;
    slot.name = wc->class_name;
    slot.wndproc = wc->window_proc;
    const abi::Atom atom =
        static_cast<abi::Atom>(static_cast<std::size_t>(free_it - g_classes.begin()) + 1U);
    slot.atom = atom;
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "RegisterClassExA"},
        diagnostics::TraceField{"class", slot.name},
        diagnostics::TraceField{"atom", std::to_string(atom)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("RegisterClassExA", fields, 4);
    return atom;
}

TL_MSABI abi::Atom tl_RegisterClassA(const void* wnd_class) noexcept {
    if (wnd_class == nullptr || !mapped_guest_range(wnd_class, sizeof(abi::GuestWndClassA), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto* wc = static_cast<const abi::GuestWndClassA*>(wnd_class);
    abi::GuestWndClassExA ex{};
    ex.cb_size = sizeof(ex);
    ex.style = wc->style;
    ex.window_proc = wc->window_proc;
    ex.class_extra = wc->class_extra;
    ex.window_extra = wc->window_extra;
    ex.instance = wc->instance;
    ex.icon = wc->icon;
    ex.cursor = wc->cursor;
    ex.background = wc->background;
    ex.menu_name = wc->menu_name;
    ex.class_name = wc->class_name;
    return tl_RegisterClassExA(&ex);
}

TL_MSABI abi::Atom tl_RegisterClassExW(const void* const wnd_class) noexcept {
    if (wnd_class == nullptr ||
        !mapped_guest_range(wnd_class, sizeof(abi::GuestWndClassExW), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("RegisterClassExW", "wnd-class", "estrutura WNDCLASSEXW inválida");
        return 0;
    }
    const auto* const wc = static_cast<const abi::GuestWndClassExW*>(wnd_class);
    if (wc->cb_size < sizeof(abi::GuestWndClassExW) || wc->window_proc == 0 ||
        wc->class_name == nullptr || !mapped_guest_wstring(wc->class_name) ||
        (wc->menu_name != nullptr && !mapped_guest_wstring(wc->menu_name)) ||
        !mapped_guest_range(std::bit_cast<const void*>(wc->window_proc), 1, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("RegisterClassExW", "wnd-class", "cbSize, window_proc ou class_name inválido");
        return 0;
    }
    std::string utf8_class = util::wide_to_utf8(wc->class_name);
    std::string utf8_menu;
    const char* menu_cstr = nullptr;
    if (wc->menu_name != nullptr) {
        utf8_menu = util::wide_to_utf8(wc->menu_name);
        menu_cstr = utf8_menu.c_str();
    }
    abi::GuestWndClassExA exA{};
    exA.cb_size = sizeof(exA);
    exA.style = wc->style;
    exA.window_proc = wc->window_proc;
    exA.class_extra = wc->class_extra;
    exA.window_extra = wc->window_extra;
    exA.instance = wc->instance;
    exA.icon = wc->icon;
    exA.cursor = wc->cursor;
    exA.background = wc->background;
    exA.menu_name = menu_cstr;
    exA.class_name = utf8_class.c_str();
    exA.icon_sm = wc->icon_sm;
    return tl_RegisterClassExA(&exA);
}

TL_MSABI abi::Atom tl_RegisterClassW(const void* wnd_class) noexcept {
    if (wnd_class == nullptr || !mapped_guest_range(wnd_class, sizeof(abi::GuestWndClassW), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto* wc = static_cast<const abi::GuestWndClassW*>(wnd_class);
    if (wc->window_proc == 0 || wc->class_name == nullptr || !mapped_guest_wstring(wc->class_name) ||
        (wc->menu_name != nullptr && !guest_resource_or_wstring_valid(wc->menu_name))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string utf8_class = util::wide_to_utf8(wc->class_name);
    std::string utf8_menu;
    const char* menu_cstr = nullptr;
    if (wc->menu_name != nullptr && reinterpret_cast<std::uintptr_t>(wc->menu_name) > 0xFFFFU) {
        utf8_menu = util::wide_to_utf8(wc->menu_name);
        menu_cstr = utf8_menu.c_str();
    }
    (void)menu_cstr;
    if (find_class_slot(utf8_class.c_str()) != nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto free_it = std::find_if(g_classes.begin(), g_classes.end(),
                                      [](const ClassSlot& slot) { return !slot.used; });
    if (free_it == g_classes.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    ClassSlot& slot = *free_it;
    slot.used = true;
    slot.name = utf8_class;
    slot.wndproc = wc->window_proc;
    slot.atom = static_cast<abi::Atom>(static_cast<std::size_t>(free_it - g_classes.begin()) + 1U);
    set_last_error(abi::kErrorSuccess);
    return slot.atom;
}

TL_MSABI abi::HWnd tl_CreateWindowExA(const std::uint32_t ex_style,
                                      const char* const class_name, const char* const window_name,
                                      const std::uint32_t style, const int x, const int y,
                                      const int width, const int height, const void* const parent,
                                      const void* const menu, const void* const instance,
                                      const void* const param) noexcept {
    (void)ex_style;
    (void)instance;
    (void)param;
    const auto class_val = reinterpret_cast<std::uintptr_t>(class_name);
    const int resolved_width = guest_window_dimension(width, 800);
    const int resolved_height = guest_window_dimension(height, 600);
    if (class_name == nullptr || (class_val > 0xFFFFU && !mapped_guest_cstring(class_name)) ||
        (window_name != nullptr && !mapped_guest_cstring(window_name))) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateWindowExA", "strings", "nome de classe ou janela inválido");
        return nullptr;
    }
    ClassSlot* const cls = find_class_slot(class_name);
    const bool generic_child = cls == nullptr && class_val > 0xFFFFU && parent != nullptr;
    if (cls == nullptr && (class_val <= 0xFFFFU ||
                           (!runtime_gui::is_builtin_control(class_name) && !generic_child))) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateWindowExA", "class-lookup", "classe não registrada");
        return nullptr;
    }
    const auto free_it = std::find_if(g_windows.begin(), g_windows.end(),
                                      [](const WindowSlot& slot) { return !slot.used; });
    if (free_it == g_windows.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    if (class_val > 0xFFFFU && (runtime_gui::is_builtin_control(class_name) || generic_child)) {
        WindowSlot& slot = *free_it;
        WindowSlot* parent_slot = find_window_slot(parent);
        if (parent_slot == nullptr || parent_slot->is_control) {
            set_last_error(abi::kErrorInvalidHandle);
            return nullptr;
        }
        slot = {};
        slot.used = true;
        slot.class_name = class_name;
        slot.is_control = true;
        slot.control_kind = runtime_gui::is_builtin_control(class_name)
                                ? runtime_gui::control_kind_for(class_name)
                                : runtime_gui::ControlKind::Generic;
        slot.parent = parent_slot;
        slot.control_id = reinterpret_cast<std::uintptr_t>(menu);
        slot.style = style;
        slot.x = x;
        slot.y = y;
        slot.width = guest_window_dimension(width, 1);
        slot.height = guest_window_dimension(height, 1);
        slot.text = window_name != nullptr ? window_name : "";
        slot.visible = (style & kWsVisible) != 0U || style == 0U;
        slot.enabled = (style & kWsDisabled) == 0U;
        slot.combo_selection = -1;
        if (generic_child) {
            const std::array<diagnostics::TraceField, 4> fields{
                diagnostics::TraceField{"class", class_name},
                diagnostics::TraceField{"status", "generic-child"},
                diagnostics::TraceField{"width", std::to_string(slot.width)},
                diagnostics::TraceField{"height", std::to_string(slot.height)}};
            runtime_trace("CreateWindowExA", fields, 4);
        }
        set_last_error(abi::kErrorSuccess);
        return &slot;
    }
    const char* const caption = window_name != nullptr ? window_name : cls->name.c_str();
    gui::NativeWindow native = gui::platform::create_window(caption, resolved_width, resolved_height);
    if (native == nullptr) {
        set_last_error(abi::kErrorAccessDenied);
        trace_guest_failure("CreateWindowExA", "platform", "falha ao criar janela no backend gráfico selecionado");
        return nullptr;
    }
    WindowSlot& slot = *free_it;
    slot.used = true;
    slot.wndproc = cls->wndproc;
    slot.class_name = cls->name;
    slot.window_title = caption;
    slot.native = native;
    slot.x = x;
    slot.y = y;
    slot.width = resolved_width;
    slot.height = resolved_height;
    slot.style = style;
    slot.visible = (style & kWsVisible) != 0U;
    // A janela principal precisa estar presente no compositor enquanto o
    // WM_CREATE é executado. Alguns aplicativos fazem parte da inicialização
    // dentro desse callback; se ele bloquear, uma janela criada mas ainda não
    // mapeada torna o diagnóstico impossível e parece que nada aconteceu.
    slot.mapped = gui::platform::map_window(slot.native);
    slot.visible = slot.mapped || slot.visible;
    if (slot.mapped) {
        // Primeiro frame de segurança: o aplicativo pode executar uma parte
        // longa de sua inicialização no WM_CREATE. Ainda assim a janela deve
        // apresentar conteúdo imediatamente, em vez de parecer congelada em
        // branco enquanto o callback convidado não retorna.
        gui::platform::fill_rectangle_color(slot.native, 0, 0, slot.width, slot.height,
                                            0xE2E8F0U);
        gui::platform::fill_rectangle_color(slot.native, 0, 0, slot.width, 56,
                                            0x1D4ED8U);
        gui::platform::draw_text_color(slot.native, "TradutorLinux - inicializando 7-Zip", 24, 34,
                                       0xFFFFFFU, true);
        gui::platform::draw_text_color(slot.native,
                                       "Erro: o aplicativo nao concluiu WM_CREATE",
                                       24, 104, 0xB91C1CU, true);
        gui::platform::draw_text_color(slot.native,
                                       "A inicializacao do aplicativo esta bloqueada.",
                                       24, 132, 0x334155U, false);
        gui::platform::flush_window(slot.native);
    }
    struct GuestCreateStructA {
        const void* lpCreateParams;
        const void* hInstance;
        const void* hMenu;
        const void* hwndParent;
        int cy;
        int cx;
        int y;
        int x;
        std::uint32_t style;
        std::uint32_t pad0;
        const char* lpszName;
        const char* lpszClass;
        std::uint32_t dwExStyle;
        std::uint32_t pad1;
    };
    GuestCreateStructA cs{};
    cs.lpCreateParams = param;
    cs.hInstance = instance;
    cs.hMenu = menu;
    cs.hwndParent = parent;
    cs.cy = slot.height;
    cs.cx = slot.width;
    cs.y = y;
    cs.x = x;
    cs.style = style;
    cs.lpszName = caption;
    cs.lpszClass = cls->name.c_str();
    cs.dwExStyle = ex_style;
    const std::array<diagnostics::TraceField, 4> create_begin_fields{
        diagnostics::TraceField{"symbol", "CreateWindowExA"},
        diagnostics::TraceField{"stage", "WM_CREATE-begin"},
        diagnostics::TraceField{},
        diagnostics::TraceField{},
    };
    runtime_trace("CreateWindowExA", create_begin_fields, 2);
    const bool skip_toplevel_create = std::getenv("TL_SKIP_TOPLEVEL_WM_CREATE") != nullptr &&
                                      slot.parent == nullptr;
    const abi::Lresult create_result = skip_toplevel_create
                                            ? 0
                                            : call_wndproc(slot.wndproc, &slot, abi::kWmCreate, 0,
                                                           reinterpret_cast<abi::Lparam>(&cs));
    const std::array<diagnostics::TraceField, 4> create_end_fields{
        diagnostics::TraceField{"symbol", "CreateWindowExA"},
        diagnostics::TraceField{"stage", "WM_CREATE-end"},
        diagnostics::TraceField{"result", std::to_string(create_result)},
        diagnostics::TraceField{},
    };
    runtime_trace("CreateWindowExA", create_end_fields, 3);
    if (create_result == -1) {
        gui::platform::destroy_window(slot.native);
        slot = {};
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateWindowExA", "wm-create", "WM_CREATE rejeitou a criação");
        return nullptr;
    }
    render_controls(slot);
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "CreateWindowExA"},
        diagnostics::TraceField{"class", slot.class_name},
        diagnostics::TraceField{"window", caption},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("CreateWindowExA", fields, 4);
    return &slot;
}

TL_MSABI abi::HWnd tl_CreateWindowExW(const std::uint32_t ex_style,
                                      const std::uint16_t* const class_name,
                                      const std::uint16_t* const window_name,
                                      const std::uint32_t style, const int x, const int y,
                                      const int width, const int height,
                                      const void* const parent, const void* const menu,
                                      const void* const instance,
                                      const void* const param) noexcept {
    const auto class_val = reinterpret_cast<std::uintptr_t>(class_name);
    if (class_name == nullptr || (class_val > 0xFFFFU && !mapped_guest_wstring(class_name)) ||
        (window_name != nullptr && !mapped_guest_wstring(window_name))) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateWindowExW", "strings", "nome de classe ou janela inválido");
        return nullptr;
    }
    std::string utf8_window;
    const char* win_cstr = nullptr;
    if (window_name != nullptr) {
        utf8_window = util::wide_to_utf8(window_name);
        win_cstr = utf8_window.c_str();
    }
    if (class_val <= 0xFFFFU) {
        return tl_CreateWindowExA(ex_style, reinterpret_cast<const char*>(class_val), win_cstr, style, x, y, width, height, parent,
                                  menu, instance, param);
    }
    const std::string utf8_class = util::wide_to_utf8(class_name);
    return tl_CreateWindowExA(ex_style, utf8_class.c_str(), win_cstr, style, x, y, width, height, parent,
                              menu, instance, param);
}

TL_MSABI int tl_ShowWindow(const void* const window, const int cmd_show) noexcept {
    const std::array<diagnostics::TraceField, 4> show_begin{
        diagnostics::TraceField{"symbol", "ShowWindow"},
        diagnostics::TraceField{"stage", "begin"}, diagnostics::TraceField{}, diagnostics::TraceField{}};
    runtime_trace("ShowWindow", show_begin, 2);
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("ShowWindow", "handle-validation", "handle inválido");
        return 0;
    }
    const bool was_visible = slot->visible;
    if (cmd_show == 0) {
        slot->visible = false;
        if (slot->native != nullptr) {
            gui::platform::unmap_window(slot->native);
        }
    } else if (slot->native != nullptr) {
        slot->mapped = gui::platform::map_window(slot->native);
        slot->visible = true;
    } else {
        slot->visible = true;
    }
    if (slot->is_control && slot->parent != nullptr) {
        render_controls(*slot->parent);
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> show_end{
        diagnostics::TraceField{"symbol", "ShowWindow"},
        diagnostics::TraceField{"stage", "end"}, diagnostics::TraceField{}, diagnostics::TraceField{}};
    runtime_trace("ShowWindow", show_end, 2);
    return was_visible ? 1 : 0;
}

TL_MSABI int tl_UpdateWindow(const void* const window) noexcept {
    const std::array<diagnostics::TraceField, 4> update_begin{
        diagnostics::TraceField{"symbol", "UpdateWindow"},
        diagnostics::TraceField{"stage", "begin"}, diagnostics::TraceField{}, diagnostics::TraceField{}};
    runtime_trace("UpdateWindow", update_begin, 2);
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("UpdateWindow", "handle-validation", "handle inválido");
        return 0;
    }
    if (slot->wndproc != 0) {
        call_wndproc(slot->wndproc, const_cast<abi::HWnd>(window), abi::kWmPaint, 0, 0);
    }
    render_controls(*slot);
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> update_end{
        diagnostics::TraceField{"symbol", "UpdateWindow"},
        diagnostics::TraceField{"stage", "end"}, diagnostics::TraceField{}, diagnostics::TraceField{}};
    runtime_trace("UpdateWindow", update_end, 2);
    return 1;
}

TL_MSABI int tl_GetMessageA(void* const msg, const void* const window,
                            const std::uint32_t filter_min,
                            const std::uint32_t filter_max) noexcept {
    if (msg == nullptr || !mapped_guest_range(msg, sizeof(abi::GuestMsg), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("GetMessageA", "output-message", "ponteiro sem permissão de escrita");
        return -1;
    }
    (void)filter_min;
    (void)filter_max;
    if (g_quit_requested) {
        g_quit_requested = false;
        write_guest_msg(msg, nullptr, abi::kWmQuit, g_quit_code, 0);
        const std::array<diagnostics::TraceField, 4> fields{
            diagnostics::TraceField{"symbol", "GetMessageA"},
            diagnostics::TraceField{"message", "WM_QUIT"},
            diagnostics::TraceField{"exit-code", std::to_string(g_quit_code)},
            diagnostics::TraceField{"result", "quit"},
        };
        runtime_trace("GetMessageA", fields, 4);
        set_last_error(abi::kErrorSuccess);
        return 0;
    }
    for (WindowSlot& slot : g_windows) {
        if (!slot.used || (window != nullptr && window != &slot)) {
            continue;
        }
        if (slot.has_pending) {
            write_guest_msg(msg, &slot, slot.pending.message, slot.pending.wparam,
                            slot.pending.lparam);
            slot.has_pending = false;
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
        if (!slot.queued_messages.empty()) {
            const abi::GuestMsg queued = slot.queued_messages.front();
            slot.queued_messages.pop_front();
            write_guest_msg(msg, queued.hwnd, queued.message, queued.wparam, queued.lparam);
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
    }
    for (;;) {
        for (WindowSlot& slot : g_windows) {
            if (!slot.used || slot.native == nullptr || (window != nullptr && window != &slot)) {
                continue;
            }
            const gui::WindowEvent event = gui::platform::next_window_event(slot.native);
            if (event.type == gui::WindowEventType::Redraw) {
                render_controls(slot);
                write_guest_msg(msg, &slot, abi::kWmPaint, 0, 0);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::Press) {
                handle_control_mouse(slot, event);
                slot.left_button_down = true;
                const abi::Lparam lparam =
                    (static_cast<std::intptr_t>(event.y & 0xFFFF) << 16) |
                    static_cast<std::intptr_t>(event.x & 0xFFFF);
                write_guest_msg(msg, &slot, abi::kWmLButtonDown, abi::kMkLButton, lparam);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::Release) {
                handle_control_mouse(slot, event);
                slot.left_button_down = false;
                const abi::Lparam lparam =
                    (static_cast<std::intptr_t>(event.y & 0xFFFF) << 16) |
                    static_cast<std::intptr_t>(event.x & 0xFFFF);
                write_guest_msg(msg, &slot, abi::kWmLButtonUp, 0, lparam);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::MouseMove) {
                const abi::Lparam lparam =
                    (static_cast<std::intptr_t>(event.y & 0xFFFF) << 16) |
                    static_cast<std::intptr_t>(event.x & 0xFFFF);
                const abi::Wparam wparam = slot.left_button_down ? abi::kMkLButton : 0;
                write_guest_msg(msg, &slot, abi::kWmMouseMove, wparam, lparam);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::KeyDown) {
                slot.last_key = event.character;
                handle_control_key(slot, event);
                write_guest_msg(msg, &slot, abi::kWmKeyDown,
                                keydown_vkey(event.keysym, event.character), 0);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::RightPress) {
                write_guest_msg(msg, &slot, abi::kWmTrayIcon, 0, 0x0205);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::KeyUp) {
                write_guest_msg(msg, &slot, abi::kWmKeyUp,
                                keydown_vkey(event.keysym, event.character), 0);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::CloseRequested) {
                write_guest_msg(msg, &slot, abi::kWmClose, 0, 0);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        for (WindowSlot& slot : g_windows) {
            if (!slot.used || (window != nullptr && window != &slot)) {
                continue;
            }
            for (GuestTimer& timer : slot.timers) {
                if (now >= timer.deadline) {
                    write_guest_msg(msg, &slot, abi::kWmTimer, timer.id, 0);
                    timer.deadline = std::chrono::steady_clock::now() + timer.interval;
                    const std::array<diagnostics::TraceField, 4> fields{
                        diagnostics::TraceField{"symbol", "GetMessageA"},
                        diagnostics::TraceField{"message", "WM_TIMER"},
                        diagnostics::TraceField{"id", std::to_string(timer.id)},
                        diagnostics::TraceField{"status", "delivered"},
                    };
                    runtime_trace("GetMessageA", fields, 4);
                    set_last_error(abi::kErrorSuccess);
                    return 1;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

TL_MSABI int tl_GetMessageW(void* const msg, const void* const window,
                            const std::uint32_t filter_min,
                            const std::uint32_t filter_max) noexcept {
    return tl_GetMessageA(msg, window, filter_min, filter_max);
}

TL_MSABI int tl_TranslateMessage(const void* const msg) noexcept {
    if (msg == nullptr || !mapped_guest_range(msg, sizeof(abi::GuestMsg), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto* const message = static_cast<const abi::GuestMsg*>(msg);
    if (message->message == abi::kWmKeyDown) {
        WindowSlot* const slot = find_window_slot(message->hwnd);
        if (slot != nullptr && slot->last_key != '\0' && !slot->has_pending) {
            slot->pending = {};
            slot->pending.message = abi::kWmChar;
            slot->pending.wparam =
                static_cast<abi::Wparam>(static_cast<unsigned char>(slot->last_key));
            slot->pending.lparam = 0;
            slot->last_key = '\0';
            slot->has_pending = true;
            const std::array<diagnostics::TraceField, 4> fields{
                diagnostics::TraceField{"symbol", "TranslateMessage"},
                diagnostics::TraceField{"message", "WM_CHAR"},
                diagnostics::TraceField{"wparam", std::to_string(slot->pending.wparam)},
                diagnostics::TraceField{"status", "translated"},
            };
            runtime_trace("TranslateMessage", fields, 4);
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI abi::Lresult tl_DispatchMessageA(const void* const msg) noexcept {
    if (msg == nullptr || !mapped_guest_range(msg, sizeof(abi::GuestMsg), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto* const message = static_cast<const abi::GuestMsg*>(msg);
    WindowSlot* const slot = find_window_slot(message->hwnd);
    if (slot == nullptr || slot->wndproc == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return call_wndproc(slot->wndproc, message->hwnd, message->message, message->wparam,
                        message->lparam);
}

TL_MSABI abi::Lresult tl_DispatchMessageW(const void* const msg) noexcept {
    return tl_DispatchMessageA(msg);
}

TL_MSABI abi::Lresult tl_DefWindowProcA(const void* const window,
                                        const std::uint32_t message,
                                        const abi::Wparam wparam, const abi::Lparam lparam) noexcept {
    (void)wparam;
    (void)lparam;
    if (message == abi::kWmClose) {
        tl_DestroyWindow(window);
        return 0;
    }
    return 0;
}

TL_MSABI abi::Lresult tl_DefWindowProcW(const void* const window,
                                        const std::uint32_t message,
                                        const abi::Wparam wparam, const abi::Lparam lparam) noexcept {
    return tl_DefWindowProcA(window, message, wparam, lparam);
}

TL_MSABI int tl_DestroyWindow(const void* const window) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    // Child controls are logical side-table entries.  Destroy them with the
    // parent so a modal dialog cannot leave stale HWND tokens behind.
    for (WindowSlot& child : g_windows) {
        if (child.used && child.parent == slot) {
            if (g_focused_control == &child) {
                g_focused_control = nullptr;
            }
            child = {};
        }
    }
    if (slot->native != nullptr) {
        gui::platform::destroy_window(slot->native);
    }
    WindowSlot* const parent = slot->parent;
    if (g_focused_control == slot) {
        g_focused_control = nullptr;
    }
    slot->native = nullptr;
    slot->mapped = false;
    const abi::HWnd hwnd = const_cast<abi::HWnd>(window);
    const std::uintptr_t wndproc = slot->wndproc;
    *slot = {};
    if (wndproc != 0) {
        call_wndproc(wndproc, hwnd, abi::kWmDestroy, 0, 0);
    }
    if (parent != nullptr) {
        render_controls(*parent);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void tl_PostQuitMessage(const int exit_code) noexcept {
    g_quit_code = static_cast<std::uint32_t>(exit_code);
    g_quit_requested = true;
}

TL_MSABI std::uintptr_t tl_SetTimer(const void* const window,
                                    const std::uintptr_t id,
                                    const std::uint32_t elapsed_ms,
                                    const void* const timer_proc) noexcept {
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr || elapsed_ms == 0 || timer_proc != nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto interval = std::chrono::milliseconds{elapsed_ms};
    GuestTimer* timer = nullptr;
    const auto found = std::find_if(slot->timers.begin(), slot->timers.end(),
                                    [id](const GuestTimer& entry) { return entry.id == id; });
    if (found != slot->timers.end()) {
        timer = &*found;
    } else {
        slot->timers.push_back(GuestTimer{});
        timer = &slot->timers.back();
        timer->id = id;
    }
    timer->interval = interval;
    timer->deadline = std::chrono::steady_clock::now() + interval;
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "SetTimer"},
        diagnostics::TraceField{"id", std::to_string(id)},
        diagnostics::TraceField{"elapsed-ms", std::to_string(elapsed_ms)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("SetTimer", fields, 4);
    set_last_error(abi::kErrorSuccess);
    return id;
}

TL_MSABI int tl_KillTimer(const void* const window, const std::uintptr_t id) noexcept {
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto found = std::find_if(slot->timers.begin(), slot->timers.end(),
                                    [id](const GuestTimer& timer) { return timer.id == id; });
    if (found == slot->timers.end()) {
        set_last_error(abi::kErrorSuccess);
        return 0;
    }
    slot->timers.erase(found);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "KillTimer"},
        diagnostics::TraceField{"id", std::to_string(id)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("KillTimer", fields, 3);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_BeginPaint(const void* const window, void* const paint_struct) noexcept {
    if (paint_struct == nullptr ||
        !mapped_guest_range(paint_struct, sizeof(abi::GuestPaintStruct), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr || slot->native == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }
    auto* const ps = static_cast<abi::GuestPaintStruct*>(paint_struct);
    *ps = {};
    ps->hdc = const_cast<void*>(window);
    ps->f_erase = 1;
    ps->rc_paint = {0, 0, slot->width, slot->height};
    slot->painting = true;
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "BeginPaint"},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("BeginPaint", fields, 2);
    set_last_error(abi::kErrorSuccess);
    return ps->hdc;
}

TL_MSABI int tl_EndPaint(const void* const window, const void* const paint_struct) noexcept {
    if (paint_struct == nullptr ||
        !mapped_guest_range(paint_struct, sizeof(abi::GuestPaintStruct), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    slot->painting = false;
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "EndPaint"},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("EndPaint", fields, 2);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetClientRect(const void* window, void* rect) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr || rect == nullptr || !mapped_guest_range(rect, sizeof(abi::GuestRect), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    auto* out = static_cast<abi::GuestRect*>(rect);
    *out = {0, 0, slot->width, slot->height};
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetWindowRect(const void* window, void* rect) noexcept {
    const WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr || rect == nullptr ||
        !mapped_guest_range(rect, sizeof(abi::GuestRect), true)) {
        set_last_error(slot == nullptr ? abi::kErrorInvalidHandle : abi::kErrorInvalidParameter);
        return 0;
    }
    std::int32_t left = slot->x;
    std::int32_t top = slot->y;
    for (const WindowSlot* parent = slot->parent; parent != nullptr; parent = parent->parent) {
        left += parent->x;
        top += parent->y;
    }
    auto* const output = static_cast<abi::GuestRect*>(rect);
    *output = {left, top, left + slot->width, top + slot->height};
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetCursorPos(void* point) noexcept {
    if (point == nullptr || !mapped_guest_range(point, sizeof(std::int32_t) * 2U, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    auto* coordinates = static_cast<std::int32_t*>(point);
    coordinates[0] = 0;
    coordinates[1] = 0;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_MoveWindow(const void* window, int x, int y, int width, int height,
                           int repaint) noexcept {
    (void)repaint;
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    slot->x = x;
    slot->y = y;
    slot->width = width;
    slot->height = height;
    if (slot->parent != nullptr) {
        render_controls(*slot->parent);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::intptr_t tl_SetWindowPos(const void* window, const void* insert_after, int x, int y,
                                       int width, int height, std::uint32_t flags) noexcept {
    (void)insert_after;
    (void)flags;
    return tl_MoveWindow(window, x, y, width, height, 1);
}

TL_MSABI int tl_SetWindowTextA(const void* window, const char* text) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr || text == nullptr || !mapped_guest_cstring(text)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    slot->text = text;
    if (slot->parent != nullptr) {
        render_controls(*slot->parent);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetWindowTextA(const void* window, char* text, int capacity) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr || text == nullptr || capacity <= 0 ||
        !mapped_guest_range(text, static_cast<std::size_t>(capacity), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    runtime_gui::copy_control_text(*slot, text, capacity);
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(std::strlen(text));
}

TL_MSABI int tl_SetWindowTextW(const void* window, const std::uint16_t* text) noexcept {
    if (text == nullptr || !mapped_guest_wstring(text)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string utf8 = util::wide_to_utf8(text);
    return tl_SetWindowTextA(window, utf8.c_str());
}

TL_MSABI int tl_GetWindowTextW(const void* window, std::uint16_t* text, int capacity) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr || text == nullptr || capacity <= 0 ||
        !mapped_guest_range(text, static_cast<std::size_t>(capacity) * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string utf8;
    {
        // copy_control_text já cuida de truncamento; pegamos utf8 do slot
        char tmp[512] = {};
        runtime_gui::copy_control_text(*slot, tmp, sizeof(tmp));
        utf8 = tmp;
        if (utf8.empty()) utf8 = slot->text;
    }
    const std::u16string wide = util::utf8_to_wide(utf8);
    const std::size_t to_copy = std::min<std::size_t>(wide.size(), static_cast<std::size_t>(capacity - 1));
    for (std::size_t i = 0; i < to_copy; ++i) text[i] = wide[i];
    text[to_copy] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(to_copy);
}

TL_MSABI int tl_GetWindowTextLengthA(const void* window) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    // Usa texto do controle ou título
    const std::string& src = !slot->text.empty() ? slot->text : slot->window_title;
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(src.size());
}

TL_MSABI int tl_GetWindowTextLengthW(const void* window) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    const std::string& src = !slot->text.empty() ? slot->text : slot->window_title;
    const std::u16string wide = util::utf8_to_wide(src);
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(wide.size());
}

TL_MSABI int tl_EnableWindow(const void* window, int enable) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    const bool previous = slot->enabled;
    slot->enabled = enable != 0;
    set_last_error(abi::kErrorSuccess);
    return previous ? 1 : 0;
}

TL_MSABI const void* tl_SetFocus(const void* window) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr || !slot->is_control) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }
    WindowSlot* previous = g_focused_control;
    set_focus_control(slot);
    set_last_error(abi::kErrorSuccess);
    return previous;
}

TL_MSABI int tl_IsWindowVisible(const void* window) noexcept {
    const WindowSlot* slot = find_window_slot(window);
    return slot != nullptr && slot->visible ? 1 : 0;
}

TL_MSABI int tl_InvalidateRect(const void* window, const void* rect, int erase) noexcept {
    (void)rect;
    (void)erase;
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (slot->native != nullptr) {
        gui::platform::flush_window(slot->native);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI const void* tl_FindWindowA(const char* class_name, const char* window_name) noexcept {
    for (const WindowSlot& slot : g_windows) {
        if (!slot.used || slot.native == nullptr ||
            (class_name != nullptr && !util::ascii_iequals(slot.class_name, class_name))) {
            continue;
        }
        if (window_name == nullptr || window_name[0] == '\0' ||
            slot.window_title == window_name) {
            return &slot;
        }
    }
    return nullptr;
}

TL_MSABI const void* tl_FindWindowW(const std::uint16_t* class_name, const std::uint16_t* window_name) noexcept {
    std::string utf8_class, utf8_window;
    const char* class_cstr = nullptr;
    const char* window_cstr = nullptr;
    if (class_name != nullptr) {
        if (!mapped_guest_wstring(class_name)) {
            set_last_error(abi::kErrorInvalidParameter);
            return nullptr;
        }
        if (class_name[0] != 0) {
            utf8_class = util::wide_to_utf8(class_name);
            class_cstr = utf8_class.c_str();
        }
    }
    if (window_name != nullptr) {
        if (!mapped_guest_wstring(window_name)) {
            set_last_error(abi::kErrorInvalidParameter);
            return nullptr;
        }
        if (window_name[0] != 0) {
            utf8_window = util::wide_to_utf8(window_name);
            window_cstr = utf8_window.c_str();
        }
    }
    const void* res = tl_FindWindowA(class_cstr, window_cstr);
    set_last_error(abi::kErrorSuccess);
    return res;
}

TL_MSABI std::uintptr_t tl_LoadCursorA(const void* instance, const char* name) noexcept {
    (void)instance;
    (void)name;
    return 1;
}

TL_MSABI std::uintptr_t tl_LoadCursorW(const void* instance, const std::uint16_t* name) noexcept {
    (void)instance;
    if (name != nullptr && !mapped_guest_wstring(name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uintptr_t tl_LoadIconA(const void* instance, const char* name) noexcept {
    (void)instance;
    (void)name;
    return 1;
}

TL_MSABI std::uintptr_t tl_LoadIconW(const void* instance, const std::uint16_t* name) noexcept {
    (void)instance;
    if (name != nullptr && !mapped_guest_wstring(name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::intptr_t tl_SetClassLongPtrA(const void* window, int index,
                                            std::intptr_t value) noexcept {
    (void)window;
    (void)index;
    (void)value;
    return 0;
}

TL_MSABI std::intptr_t tl_SetClassLongPtrW(const void* window, int index,
                                            std::intptr_t value) noexcept {
    return tl_SetClassLongPtrA(window, index, value);
}

TL_MSABI int tl_SetForegroundWindow(const void* window) noexcept {
    (void)window;
    return 1;
}

TL_MSABI int tl_SendMessageA(const void* window, const std::uint32_t message,
                             const abi::Wparam wparam, const abi::Lparam lparam) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (slot->is_control) {
        if (message == abi::kWmSetFont) {
            return 0;
        }
        if (slot->control_kind == ControlKind::Edit) {
            if (message == 0x000C && lparam != 0 &&
                mapped_guest_cstring(reinterpret_cast<const char*>(lparam))) {
                slot->text = reinterpret_cast<const char*>(lparam);
                if (slot->parent != nullptr) {
                    render_controls(*slot->parent);
                }
                return 1;
            }
        }
        if (slot->control_kind == ControlKind::ComboBox) {
            if (message == abi::kCbAddString && lparam != 0 &&
                mapped_guest_cstring(reinterpret_cast<const char*>(lparam))) {
                slot->combo_items.emplace_back(reinterpret_cast<const char*>(lparam));
                return static_cast<int>(slot->combo_items.size() - 1U);
            }
            if (message == abi::kCbSetCurSel) {
                slot->combo_selection = static_cast<int>(wparam);
                if (slot->parent != nullptr) {
                    render_controls(*slot->parent);
                }
                return slot->combo_selection;
            }
            if (message == abi::kCbGetCurSel) {
                return slot->combo_selection;
            }
        }
        if (slot->control_kind == ControlKind::ListView) {
            if (message == abi::kLvmSetExtendedListViewStyle) {
                return 0;
            }
            if (message == abi::kLvmInsertColumnA) {
                return static_cast<int>(wparam);
            }
            if (message == abi::kLvmDeleteAllItems) {
                slot->list_rows.clear();
                slot->list_selection = -1;
                if (slot->parent != nullptr) {
                    render_controls(*slot->parent);
                }
                return 1;
            }
            if (message == abi::kLvmInsertItemA && lparam != 0 &&
                mapped_guest_range(reinterpret_cast<const void*>(lparam), sizeof(abi::GuestLvItemA),
                                    false)) {
                const auto* item = reinterpret_cast<const abi::GuestLvItemA*>(lparam);
                ListViewRow row;
                row.columns.resize(6);
                row.param = item->param;
                if (item->text != nullptr && mapped_guest_cstring(item->text)) {
                    row.columns[0] = item->text;
                }
                int index = item->item;
                if (index < 0 || index > static_cast<int>(slot->list_rows.size())) {
                    index = static_cast<int>(slot->list_rows.size());
                }
                slot->list_rows.insert(slot->list_rows.begin() + index, std::move(row));
                if (slot->parent != nullptr) {
                    render_controls(*slot->parent);
                }
                return index;
            }
            if (message == abi::kLvmSetItemTextA && lparam != 0 &&
                mapped_guest_range(reinterpret_cast<const void*>(lparam), sizeof(abi::GuestLvItemA),
                                    false)) {
                const int index = static_cast<int>(wparam);
                const auto* item = reinterpret_cast<const abi::GuestLvItemA*>(lparam);
                if (index >= 0 && static_cast<std::size_t>(index) < slot->list_rows.size() &&
                    item->subitem >= 0 && item->subitem < 6 && item->text != nullptr &&
                    mapped_guest_cstring(item->text)) {
                    slot->list_rows[static_cast<std::size_t>(index)].columns[static_cast<std::size_t>(item->subitem)] =
                        item->text;
                    if (slot->parent != nullptr) {
                        render_controls(*slot->parent);
                    }
                    return 1;
                }
                return 0;
            }
            if (message == abi::kLvmGetNextItem) {
                const std::int32_t start = static_cast<std::int32_t>(wparam);
                if (slot->list_selection < 0 ||
                    (start >= 0 && slot->list_selection <= start)) {
                    return -1;
                }
                return slot->list_selection;
            }
            if (message == abi::kLvmGetItemA && lparam != 0 &&
                mapped_guest_range(reinterpret_cast<const void*>(lparam), sizeof(abi::GuestLvItemA),
                                    true)) {
                const int index = static_cast<int>(wparam);
                auto* item = reinterpret_cast<abi::GuestLvItemA*>(lparam);
                if (index >= 0 && static_cast<std::size_t>(index) < slot->list_rows.size()) {
                    item->param = slot->list_rows[static_cast<std::size_t>(index)].param;
                    return 1;
                }
                return 0;
            }
            if (message == abi::kLvmGetItemTextA && lparam != 0 &&
                mapped_guest_range(reinterpret_cast<const void*>(lparam), sizeof(abi::GuestLvItemA),
                                    true)) {
                const int index = static_cast<int>(wparam);
                auto* item = reinterpret_cast<abi::GuestLvItemA*>(lparam);
                if (index >= 0 && static_cast<std::size_t>(index) < slot->list_rows.size() &&
                    item->subitem >= 0 && item->subitem < 6 && item->text != nullptr &&
                    item->text_capacity > 0 &&
                    mapped_guest_range(item->text, static_cast<std::size_t>(item->text_capacity), true)) {
                    const std::string& value = slot->list_rows[static_cast<std::size_t>(index)].columns[
                        static_cast<std::size_t>(item->subitem)];
                    const std::size_t count = std::min<std::size_t>(value.size(),
                                                                     static_cast<std::size_t>(item->text_capacity - 1));
                    std::memcpy(item->text, value.data(), count);
                    item->text[count] = '\0';
                    return static_cast<int>(count);
                }
                return 0;
            }
            if (message == abi::kLvmSortItemsEx) {
                return 1;
            }
        }
        set_last_error(abi::kErrorSuccess);
        return 0;
    }
    if (slot->wndproc != 0) {
        return static_cast<int>(call_wndproc(slot->wndproc, const_cast<abi::HWnd>(window), message,
                                             wparam, lparam));
    }
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI int tl_SendMessageW(const void* window, const std::uint32_t message,
                              const abi::Wparam wparam, const abi::Lparam lparam) noexcept {
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (slot->is_control) {
        if (message == 0x000C && lparam != 0 &&
            mapped_guest_wstring(reinterpret_cast<const std::uint16_t*>(lparam))) {
            const std::string utf8 = util::wide_to_utf8(reinterpret_cast<const std::uint16_t*>(lparam));
            return tl_SendMessageA(window, message, wparam, reinterpret_cast<abi::Lparam>(utf8.c_str()));
        }
        if (message == abi::kCbAddString && lparam != 0 &&
            mapped_guest_wstring(reinterpret_cast<const std::uint16_t*>(lparam))) {
            const std::string utf8 = util::wide_to_utf8(reinterpret_cast<const std::uint16_t*>(lparam));
            return tl_SendMessageA(window, message, wparam, reinterpret_cast<abi::Lparam>(utf8.c_str()));
        }
        return tl_SendMessageA(window, message, wparam, lparam);
    }
    if (slot->wndproc != 0) {
        return static_cast<int>(call_wndproc(slot->wndproc, const_cast<abi::HWnd>(window), message,
                                             wparam, lparam));
    }
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI void* tl_GetDlgItem(const void* dialog, const int identifier) noexcept {
    WindowSlot* const slot = find_window_slot(dialog);
    if (slot == nullptr || !slot->is_dialog) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }
    WindowSlot* const child = dialog_control_by_id(*slot, identifier);
    set_last_error(child == nullptr ? abi::kErrorFileNotFound : abi::kErrorSuccess);
    return child;
}

TL_MSABI int tl_SetDlgItemTextW(const void* dialog, const int identifier,
                                const std::uint16_t* const text) noexcept {
    if (text == nullptr || !mapped_guest_wstring(text)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    void* const child = tl_GetDlgItem(dialog, identifier);
    if (child == nullptr) {
        return 0;
    }
    return tl_SetWindowTextW(child, text);
}

TL_MSABI abi::Lresult tl_SendDlgItemMessageW(const void* dialog, const int identifier,
                                             const std::uint32_t message,
                                             const abi::Wparam wparam,
                                             const abi::Lparam lparam) noexcept {
    void* const child = tl_GetDlgItem(dialog, identifier);
    if (child == nullptr) {
        return 0;
    }
    return static_cast<abi::Lresult>(tl_SendMessageW(child, message, wparam, lparam));
}

TL_MSABI void* tl_GetNextDlgTabItem(const void* dialog, const void* control,
                                    const int previous) noexcept {
    WindowSlot* const slot = find_window_slot(dialog);
    WindowSlot* current = nullptr;
    if (control != nullptr) {
        current = find_window_slot(control);
        if (current == nullptr || current->parent != slot) {
            set_last_error(abi::kErrorInvalidHandle);
            return nullptr;
        }
    }
    if (slot == nullptr || !slot->is_dialog) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }
    WindowSlot* const next = next_dialog_tab_item(*slot, current, previous != 0);
    set_last_error(next == nullptr ? abi::kErrorFileNotFound : abi::kErrorSuccess);
    return next;
}

TL_MSABI int tl_IsDialogMessageW(const void* dialog, const void* message) noexcept {
    WindowSlot* const slot = find_window_slot(dialog);
    if (slot == nullptr || !slot->is_dialog || message == nullptr ||
        !mapped_guest_range(message, sizeof(abi::GuestMsg), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto* const input = static_cast<const abi::GuestMsg*>(message);
    if (input->message != abi::kWmKeyDown || input->hwnd != slot) {
        set_last_error(abi::kErrorSuccess);
        return 0;
    }
    if (input->wparam == abi::kVkTab) {
        WindowSlot* const next = next_dialog_tab_item(*slot, g_focused_control, false);
        if (next == nullptr) {
            set_last_error(abi::kErrorSuccess);
            return 0;
        }
        set_focus_control(next);
        const std::array<diagnostics::TraceField, 4> fields{
            diagnostics::TraceField{"symbol", "IsDialogMessageW"},
            diagnostics::TraceField{"action", "tab"},
            diagnostics::TraceField{"status", "handled"},
            diagnostics::TraceField{"control", std::to_string(next->control_id)},
        };
        runtime_trace("IsDialogMessageW", fields, 4);
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    int command_id = 0;
    const char* action = nullptr;
    if (input->wparam == abi::kVkReturn) {
        command_id = kIdOk;
        action = "enter";
    } else if (input->wparam == abi::kVkEscape) {
        command_id = kIdCancel;
        action = "escape";
    }
    if (command_id != 0 && dialog_control_by_id(*slot, command_id) != nullptr) {
        queue_window_message(*slot, abi::kWmCommand, static_cast<abi::Wparam>(command_id),
                             reinterpret_cast<abi::Lparam>(dialog_control_by_id(*slot, command_id)));
        const std::array<diagnostics::TraceField, 4> fields{
            diagnostics::TraceField{"symbol", "IsDialogMessageW"},
            diagnostics::TraceField{"action", action},
            diagnostics::TraceField{"status", "handled"},
            diagnostics::TraceField{"control", std::to_string(command_id)},
        };
        runtime_trace("IsDialogMessageW", fields, 4);
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI int tl_EndDialog(const void* dialog, const std::intptr_t result) noexcept {
    WindowSlot* const slot = find_window_slot(dialog);
    WindowSlot* modal_parent = nullptr;
    bool modal_parent_was_enabled = true;
    {
        std::lock_guard lock(g_modal_mutex);
        if (slot == nullptr || !slot->is_dialog || slot != g_active_dialog || g_modal_done) {
            set_last_error(abi::kErrorInvalidHandle);
            return 0;
        }
        g_modal_result = result;
        g_modal_done = true;
        modal_parent = g_modal_parent;
        modal_parent_was_enabled = g_modal_parent_was_enabled;
    }
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    const abi::HWnd hwnd = slot;
    tl_DestroyWindow(hwnd);
    if (modal_parent != nullptr) {
        WindowSlot* const parent = find_window_slot(modal_parent);
        if (parent != nullptr) {
            parent->enabled = modal_parent_was_enabled;
            render_controls(*parent);
        }
    }
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "EndDialog"},
        diagnostics::TraceField{"result", std::to_string(result)},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"modal", "closed"},
    };
    runtime_trace("EndDialog", fields, 4);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::intptr_t tl_DialogBoxParamW(const void* const instance,
                                           const std::uint16_t* const template_name,
                                           const void* const parent,
                                           const std::uintptr_t dialog_proc,
                                           const abi::Lparam init_param) noexcept {
    if (g_active_dialog != nullptr) {
        set_last_error(abi::kErrorNotSupported);
        return -1;
    }
    if ((instance != nullptr && reinterpret_cast<std::uintptr_t>(instance) != 0x1000U &&
         reinterpret_cast<std::uintptr_t>(instance) !=
             reinterpret_cast<std::uintptr_t>(g_guest_image_base)) ||
        template_name == nullptr ||
        !guest_callback_address_valid(dialog_proc)) {
        set_last_error(abi::kErrorInvalidParameter);
        return -1;
    }
    WindowSlot* parent_slot = nullptr;
    if (parent != nullptr) {
        parent_slot = find_window_slot(parent);
        if (parent_slot == nullptr || parent_slot->is_control) {
            set_last_error(abi::kErrorInvalidHandle);
            return -1;
        }
    }
    const auto* const resource_name = template_name;
    const auto* const resource_type = reinterpret_cast<const std::uint16_t*>(5U);
    void* const resource = tl_FindResourceW(nullptr, resource_name, resource_type);
    void* const loaded = resource == nullptr ? nullptr : tl_LoadResource(nullptr, resource);
    const std::uint32_t resource_size = loaded == nullptr ? 0 : tl_SizeofResource(nullptr, resource);
    const void* const resource_data = loaded == nullptr ? nullptr : tl_LockResource(loaded);
    if (resource_data == nullptr || resource_size == 0) {
        set_last_error(abi::kErrorResourceNotFound);
        return -1;
    }
    runtime::DialogTemplate parsed{};
    const auto status = runtime::parse_dialog_template(
        std::span<const std::byte>{static_cast<const std::byte*>(resource_data), resource_size}, parsed);
    if (status != runtime::DialogTemplateStatus::Success) {
        set_last_error(status == runtime::DialogTemplateStatus::DialogEx
                           ? abi::kErrorNotSupported
                           : abi::kErrorInvalidParameter);
        return -1;
    }
    bool parent_was_enabled = true;
    if (parent_slot != nullptr) {
        parent_was_enabled = parent_slot->enabled;
        {
            std::lock_guard lock(g_modal_mutex);
            g_modal_parent = parent_slot;
            g_modal_parent_was_enabled = parent_was_enabled;
        }
        parent_slot->enabled = false;
        render_controls(*parent_slot);
    } else {
        std::lock_guard lock(g_modal_mutex);
        g_modal_parent = nullptr;
        g_modal_parent_was_enabled = true;
    }

    const std::string title = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(parsed.title.c_str()));
    gui::NativeWindow native = gui::platform::create_window(title.c_str(), parsed.width, parsed.height);
    if (native == nullptr) {
        if (parent_slot != nullptr) {
            parent_slot->enabled = parent_was_enabled;
            render_controls(*parent_slot);
        }
        set_last_error(abi::kErrorAccessDenied);
        return -1;
    }
    const auto free_it = std::find_if(g_windows.begin(), g_windows.end(),
                                      [](const WindowSlot& slot) { return !slot.used; });
    if (free_it == g_windows.end()) {
        gui::platform::destroy_window(native);
        if (parent_slot != nullptr) {
            parent_slot->enabled = parent_was_enabled;
            render_controls(*parent_slot);
        }
        set_last_error(abi::kErrorNotEnoughMemory);
        return -1;
    }
    WindowSlot& dialog = *free_it;
    dialog = {};
    dialog.used = true;
    dialog.wndproc = dialog_proc;
    dialog.class_name = "#32770";
    dialog.window_title = title;
    dialog.text = title;
    dialog.native = native;
    dialog.mapped = gui::platform::map_window(native);
    dialog.width = parsed.width > 0 ? parsed.width : 1;
    dialog.height = parsed.height > 0 ? parsed.height : 1;
    dialog.x = parsed.x;
    dialog.y = parsed.y;
    dialog.style = parsed.style;
    dialog.extended_style = parsed.extended_style;
    dialog.is_dialog = true;
    dialog.parent = parent_slot;
    for (const runtime::DialogControl& item : parsed.controls) {
        const auto child_it = std::find_if(g_windows.begin(), g_windows.end(),
                                           [](const WindowSlot& slot) { return !slot.used; });
        if (child_it == g_windows.end()) {
            tl_DestroyWindow(&dialog);
            if (parent_slot != nullptr) {
                parent_slot->enabled = parent_was_enabled;
                render_controls(*parent_slot);
            }
            set_last_error(abi::kErrorNotEnoughMemory);
            return -1;
        }
        WindowSlot& child = *child_it;
        child = {};
        child.used = true;
        child.is_control = true;
        child.parent = &dialog;
        child.control_id = item.id;
        child.x = item.x;
        child.y = item.y;
        child.width = item.width > 0 ? item.width : 1;
        child.height = item.height > 0 ? item.height : 1;
        child.style = item.style;
        child.extended_style = item.extended_style;
        child.visible = (item.style & kWsVisible) != 0U || item.style == 0U;
        child.enabled = (item.style & kWsDisabled) == 0U;
        child.combo_selection = -1;
        child.text = util::wide_to_utf8(reinterpret_cast<const std::uint16_t*>(item.title.c_str()));
        switch (item.control_class) {
            case runtime::DialogControlClass::Button: child.class_name = "BUTTON"; child.control_kind = ControlKind::Button; break;
            case runtime::DialogControlClass::Edit: child.class_name = "EDIT"; child.control_kind = ControlKind::Edit; break;
            case runtime::DialogControlClass::Static: child.class_name = "STATIC"; child.control_kind = ControlKind::Static; break;
            case runtime::DialogControlClass::ComboBox: child.class_name = "COMBOBOX"; child.control_kind = ControlKind::ComboBox; break;
        }
        dialog.dialog_children.push_back(&child);
    }
    {
        std::lock_guard lock(g_modal_mutex);
        g_active_dialog = &dialog;
        g_modal_done = false;
        g_modal_result = 0;
    }
    render_controls(dialog);
    if (next_dialog_tab_item(dialog, nullptr, false) != nullptr) {
        set_focus_control(next_dialog_tab_item(dialog, nullptr, false));
    }
    static_cast<void>(call_wndproc(dialog.wndproc, &dialog, 0x0110U, 0, init_param)); // WM_INITDIALOG
    const std::array<diagnostics::TraceField, 4> created_fields{
        diagnostics::TraceField{"symbol", "DialogBoxParamW"},
        diagnostics::TraceField{"template", std::to_string(reinterpret_cast<std::uintptr_t>(template_name))},
        diagnostics::TraceField{"controls", std::to_string(parsed.controls.size())},
        diagnostics::TraceField{"status", "created"},
    };
    runtime_trace("DialogBoxParamW", created_fields, 4);

    while (true) {
        {
            std::lock_guard lock(g_modal_mutex);
            if (g_modal_done) {
                break;
            }
        }
        abi::GuestMsg message{};
        const int received = tl_GetMessageW(&message, &dialog, 0, 0);
        if (received <= 0) {
            static_cast<void>(tl_EndDialog(&dialog, kIdCancel));
            break;
        }
        if (tl_IsDialogMessageW(&dialog, &message) != 0) {
            continue;
        }
        const abi::Lresult handled = call_wndproc(dialog.wndproc, &dialog, message.message,
                                                  message.wparam, message.lparam);
        {
            std::lock_guard lock(g_modal_mutex);
            if (g_modal_done) {
                continue;
            }
        }
        if (handled == 0 && message.message == abi::kWmClose) {
            static_cast<void>(tl_EndDialog(&dialog, kIdCancel));
        } else if (handled == 0 && message.message == abi::kWmCommand) {
            const int identifier = static_cast<int>(message.wparam & 0xFFFFU);
            if (identifier == kIdOk || identifier == kIdCancel) {
                static_cast<void>(tl_EndDialog(&dialog, identifier));
            }
        }
    }
    std::intptr_t result = 0;
    {
        std::lock_guard lock(g_modal_mutex);
        result = g_modal_result;
        g_active_dialog = nullptr;
        g_modal_parent = nullptr;
        g_modal_done = false;
    }
    const std::array<diagnostics::TraceField, 4> returned_fields{
        diagnostics::TraceField{"symbol", "DialogBoxParamW"},
        diagnostics::TraceField{"result", std::to_string(result)},
        diagnostics::TraceField{"status", "returned"},
        diagnostics::TraceField{"modal", "complete"},
    };
    runtime_trace("DialogBoxParamW", returned_fields, 4);
    set_last_error(abi::kErrorSuccess);
    return result;
}

TL_MSABI void* tl_CopyImage(const void* image, const std::uint32_t image_type, const int width,
                            const int height, const std::uint32_t flags) noexcept {
    (void)flags;
    if (image == nullptr || image_type != kImageIcon || width < 0 || height < 0 ||
        (image != reinterpret_cast<const void*>(1U) &&
         std::none_of(g_image_slots.begin(), g_image_slots.end(),
                      [image](const ImageSlot& slot) { return slot.used && &slot == image; }))) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const auto free_it = std::find_if(g_image_slots.begin(), g_image_slots.end(),
                                      [](const ImageSlot& slot) { return !slot.used; });
    if (free_it == g_image_slots.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    free_it->used = true;
    free_it->source = image;
    set_last_error(abi::kErrorSuccess);
    return &*free_it;
}

TL_MSABI int tl_DestroyIcon(const void* icon) noexcept {
    if (icon == reinterpret_cast<const void*>(1U)) {
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    const auto it = std::find_if(g_image_slots.begin(), g_image_slots.end(),
                                 [icon](const ImageSlot& slot) { return slot.used && &slot == icon; });
    if (it == g_image_slots.end()) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    *it = {};
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_PostMessageA(const void* window, const std::uint32_t message,
                             const abi::Wparam wparam, const abi::Lparam lparam) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    queue_window_message(*slot, message, wparam, lparam);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_PostMessageW(const void* window, const std::uint32_t message,
                              const abi::Wparam wparam, const abi::Lparam lparam) noexcept {
    return tl_PostMessageA(window, message, wparam, lparam);
}

TL_MSABI void* tl_CreatePopupMenu() noexcept {
    const auto free_it = std::find_if(g_menus.begin(), g_menus.end(),
                                      [](const MenuSlot& menu) { return !menu.used; });
    if (free_it == g_menus.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    free_it->used = true;
    free_it->items.clear();
    set_last_error(abi::kErrorSuccess);
    return &*free_it;
}

TL_MSABI int tl_AppendMenuA(const void* menu, std::uint32_t flags, std::uintptr_t command,
                            const char* text) noexcept {
    const auto it = std::find_if(g_menus.begin(), g_menus.end(),
                                 [menu](const MenuSlot& entry) { return entry.used && &entry == menu; });
    if (it == g_menus.end()) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    it->items.push_back(gui::PopupMenuItem{.command = static_cast<std::uint32_t>(command),
                                           .text = text != nullptr ? text : "",
                                           .separator = (flags & 0x00000800U) != 0});
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_AppendMenuW(const void* menu, std::uint32_t flags, std::uintptr_t command,
                             const std::uint16_t* text) noexcept {
    if (text != nullptr && !mapped_guest_wstring(text)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string utf8;
    if (text != nullptr) utf8 = util::wide_to_utf8(text);
    return tl_AppendMenuA(menu, flags, command, text != nullptr ? utf8.c_str() : nullptr);
}

TL_MSABI int tl_DestroyMenu(const void* menu) noexcept {
    const auto it = std::find_if(g_menus.begin(), g_menus.end(),
                                 [menu](const MenuSlot& entry) { return entry.used && &entry == menu; });
    if (it == g_menus.end()) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    *it = {};
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_TrackPopupMenu(const void* menu, std::uint32_t flags, int x, int y, int reserved,
                               const void* owner, const void* rect) noexcept {
    (void)flags;
    (void)reserved;
    (void)rect;
    const auto it = std::find_if(g_menus.begin(), g_menus.end(),
                                 [menu](const MenuSlot& entry) { return entry.used && &entry == menu; });
    WindowSlot* owner_slot = find_window_slot(owner);
    if (it == g_menus.end() || owner_slot == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uint32_t command = gui::platform::track_popup_menu(it->items, x, y);
    if (command != 0) {
        queue_window_message(*owner_slot, abi::kWmCommand, command,
                             reinterpret_cast<abi::Lparam>(menu));
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_MsgWaitForMultipleObjectsEx(const std::uint32_t count,
                                                      const void* const* const handles,
                                                      const std::uint32_t milliseconds,
                                                      const std::uint32_t wake_mask,
                                                      const std::uint32_t flags) noexcept {
    (void)wake_mask;
    (void)flags;
    if (count > 64 || (count > 0 && (handles == nullptr || !mapped_guest_range(handles, count * sizeof(void*), false)))) {
        set_last_error(abi::kErrorInvalidParameter);
        return abi::kWaitFailed;
    }
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t res = tl_WaitForSingleObject(handles[i], 0);
            if (res == abi::kWaitObject0) {
                set_last_error(abi::kErrorSuccess);
                return abi::kWaitObject0 + i;
            }
        }
        for (const auto& w : g_windows) {
            if (w.used && (w.has_pending || !w.queued_messages.empty())) {
                set_last_error(abi::kErrorSuccess);
                return abi::kWaitObject0 + count;
            }
        }
        if (g_quit_requested) {
            set_last_error(abi::kErrorSuccess);
            return abi::kWaitObject0 + count;
        }
        if (milliseconds == 0) {
            set_last_error(abi::kErrorSuccess);
            return abi::kWaitTimeout;
        }
        if (milliseconds != abi::kInfinite) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= milliseconds) {
                set_last_error(abi::kErrorSuccess);
                return abi::kWaitTimeout;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

TL_MSABI std::uint32_t tl_MsgWaitForMultipleObjects(const std::uint32_t count,
                                                    const void* const* const handles,
                                                    const int wait_all,
                                                    const std::uint32_t milliseconds,
                                                    const std::uint32_t wake_mask) noexcept {
    (void)wait_all;
    return tl_MsgWaitForMultipleObjectsEx(count, handles, milliseconds, wake_mask, 0);
}

TL_MSABI int tl_GetSystemMetrics(const int index) noexcept {
    switch (index) {
        case 0: return 1920; // SM_CXSCREEN
        case 1: return 1080; // SM_CYSCREEN
        case 2: return 16;   // SM_CXVSCROLL
        case 3: return 16;   // SM_CYHSCROLL
        case 4: return 24;   // SM_CYCAPTION
        case 5: return 2;    // SM_CXBORDER
        case 6: return 2;    // SM_CYBORDER
        case 7: return 4;    // SM_CXDLGFRAME
        case 8: return 4;    // SM_CYDLGFRAME
        case 11: return 32;  // SM_CXICON
        case 12: return 32;  // SM_CYICON
        case 13: return 32;  // SM_CXCURSOR
        case 14: return 32;  // SM_CYCURSOR
        case 15: return 20;  // SM_CYMENU
        case 16: return 1920; // SM_CXFULLSCREEN
        case 17: return 1040; // SM_CYFULLSCREEN
        case 43: return 1;   // SM_CMOUSEBUTTONS
        case 74: return 0;   // SM_REMOTESESSION
        case 75: return 0;   // SM_SHUTTINGDOWN
        case 80: return 1;   // SM_CMONITORS
        default: return 0;
    }
}

TL_MSABI std::intptr_t tl_GetWindowLongPtrA(const void* window, const int index) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    switch (index) {
        case -4: return static_cast<std::intptr_t>(slot->wndproc); // GWLP_WNDPROC
        case -6: return 0; // GWLP_HINSTANCE
        case -8: return reinterpret_cast<std::intptr_t>(slot->parent); // GWLP_HWNDPARENT
        case -12: return static_cast<std::intptr_t>(slot->control_id); // GWLP_ID
        case -16: return slot->style != 0 ? slot->style : 0x10000000 | 0x00C00000; // GWL_STYLE
        case -20: return static_cast<std::intptr_t>(slot->extended_style); // GWL_EXSTYLE
        case -21: return reinterpret_cast<std::intptr_t>(slot->user_data); // GWLP_USERDATA
        default:
            if (index >= 0 && static_cast<std::size_t>(index) + sizeof(std::intptr_t) <= slot->extra_bytes.size()) {
                std::intptr_t val = 0;
                std::memcpy(&val, slot->extra_bytes.data() + index, sizeof(std::intptr_t));
                return val;
            }
            return 0;
    }
}

TL_MSABI std::intptr_t tl_GetWindowLongPtrW(const void* window, const int index) noexcept {
    return tl_GetWindowLongPtrA(window, index);
}

TL_MSABI std::intptr_t tl_SetWindowLongPtrA(const void* window, const int index, const std::intptr_t new_long) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    switch (index) {
        case -4: {
            const std::intptr_t prev = static_cast<std::intptr_t>(slot->wndproc);
            slot->wndproc = static_cast<std::uintptr_t>(new_long);
            return prev;
        }
        case -21: {
            const std::intptr_t prev = reinterpret_cast<std::intptr_t>(slot->user_data);
            slot->user_data = reinterpret_cast<void*>(new_long);
            return prev;
        }
        case -12: {
            const std::intptr_t prev = static_cast<std::intptr_t>(slot->control_id);
            slot->control_id = static_cast<std::uintptr_t>(new_long);
            return prev;
        }
        case -16: {
            const std::intptr_t prev = slot->style != 0 ? static_cast<std::intptr_t>(slot->style)
                                                        : 0x10000000 | 0x00C00000;
            slot->style = static_cast<std::uint32_t>(new_long);
            slot->visible = (slot->style & kWsVisible) != 0U || slot->style == 0U;
            slot->enabled = (slot->style & kWsDisabled) == 0U;
            if (slot->is_control && slot->parent != nullptr) {
                render_controls(*slot->parent);
            }
            return prev;
        }
        case -20: {
            const std::intptr_t prev = static_cast<std::intptr_t>(slot->extended_style);
            slot->extended_style = static_cast<std::uint32_t>(new_long);
            return prev;
        }
        default:
            if (index >= 0 && static_cast<std::size_t>(index) + sizeof(std::intptr_t) <= slot->extra_bytes.size()) {
                std::intptr_t prev = 0;
                std::memcpy(&prev, slot->extra_bytes.data() + index, sizeof(std::intptr_t));
                std::memcpy(slot->extra_bytes.data() + index, &new_long, sizeof(std::intptr_t));
                return prev;
            }
            return 0;
    }
}

TL_MSABI std::intptr_t tl_SetWindowLongPtrW(const void* window, const int index, const std::intptr_t new_long) noexcept {
    return tl_SetWindowLongPtrA(window, index, new_long);
}

TL_MSABI std::int32_t tl_GetWindowLongW(const void* window, const int index) noexcept {
    return static_cast<std::int32_t>(tl_GetWindowLongPtrW(window, index));
}

TL_MSABI std::int32_t tl_SetWindowLongW(const void* window, const int index,
                                        const std::int32_t new_long) noexcept {
    return static_cast<std::int32_t>(
        tl_SetWindowLongPtrW(window, index, static_cast<std::intptr_t>(new_long)));
}

TL_MSABI void* tl_GetParent(const void* window) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return slot->parent;
}

TL_MSABI void* tl_SetParent(const void* child_window, const void* new_parent_window) noexcept {
    WindowSlot* child = find_window_slot(child_window);
    if (child == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }
    WindowSlot* old_parent = child->parent;
    child->parent = find_window_slot(new_parent_window);
    set_last_error(abi::kErrorSuccess);
    return old_parent;
}

TL_MSABI int tl_IsWindow(const void* window) noexcept {
    return find_window_slot(window) != nullptr ? 1 : 0;
}

TL_MSABI int tl_MessageBoxW(const void* window, const std::uint16_t* text,
                            const std::uint16_t* caption, const std::uint32_t type) noexcept {
    (void)window;
    if (type != 0 || !mapped_guest_wstring(text) || !mapped_guest_wstring(caption)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string utf8_text = util::wide_to_utf8(text);
    const std::string utf8_cap = util::wide_to_utf8(caption);
    const std::uint32_t result = gui::platform::message_box(utf8_text.c_str(), utf8_cap.c_str());
    set_last_error(result == 0 ? abi::kErrorAccessDenied : abi::kErrorSuccess);
    return static_cast<int>(result);
}

TL_MSABI void* tl_GetDC(const void* window) noexcept {
    if (window == nullptr) {
        static char g_screen_dc_token = 0;
        set_last_error(abi::kErrorSuccess);
        return &g_screen_dc_token;
    }
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return const_cast<void*>(window);
}

TL_MSABI int tl_ReleaseDC(const void* window, const void* dc) noexcept {
    (void)window;
    (void)dc;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_GetWindowDC(const void* window) noexcept {
    return tl_GetDC(window);
}

TL_MSABI void* tl_SetCursor(const void* cursor) noexcept {
    (void)cursor;
    static char g_cursor_token = 0;
    return &g_cursor_token;
}

TL_MSABI int tl_ShowCursor(const int show) noexcept {
    return show >= 0 ? 0 : -1;
}

TL_MSABI int tl_SetCursorPos(const int, const int) noexcept {
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::int16_t tl_GetKeyState(const int) noexcept {
    return 0;
}

TL_MSABI std::int16_t tl_GetAsyncKeyState(const int) noexcept {
    return 0;
}

TL_MSABI int tl_LoadStringA(void* instance, const std::uint32_t id, char* buffer, const int buffer_max) noexcept {
    if (buffer == nullptr || buffer_max <= 0 || !mapped_guest_range(buffer, static_cast<std::size_t>(buffer_max), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::vector<std::uint16_t> wide(static_cast<std::size_t>(buffer_max), 0);
    const int length = tl_LoadStringW(instance, id, wide.data(), buffer_max);
    if (length <= 0) {
        buffer[0] = '\0';
        return 0;
    }
    const std::string utf8 = util::wide_to_utf8(wide.data(), static_cast<std::size_t>(length));
    const std::size_t copied = std::min(utf8.size(), static_cast<std::size_t>(buffer_max - 1));
    std::memcpy(buffer, utf8.data(), copied);
    buffer[copied] = '\0';
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(copied);
}

TL_MSABI int tl_LoadStringW(void* instance, const std::uint32_t id, std::uint16_t* buffer, const int buffer_max) noexcept {
    if (buffer == nullptr || buffer_max <= 0 || !mapped_guest_range(buffer, static_cast<std::size_t>(buffer_max) * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // IMAGE_RESOURCE_DATA_ENTRY type STRING stores 16 strings in a block.
    const auto type = reinterpret_cast<const std::uint16_t*>(static_cast<std::uintptr_t>(6U));
    const auto block = reinterpret_cast<const std::uint16_t*>(
        static_cast<std::uintptr_t>(id / 16U + 1U));
    void* const resource = tl_FindResourceW(instance, block, type);
    void* const loaded = resource == nullptr ? nullptr : tl_LoadResource(instance, resource);
    const auto* const data = loaded == nullptr
                                 ? nullptr
                                 : static_cast<const std::uint16_t*>(tl_LockResource(loaded));
    const std::uint32_t byte_size = resource == nullptr ? 0U : tl_SizeofResource(instance, resource);
    if (data == nullptr || byte_size < sizeof(std::uint16_t)) {
        buffer[0] = 0;
        set_last_error(abi::kErrorResourceNameNotFound);
        return 0;
    }

    const std::size_t unit_count = byte_size / sizeof(std::uint16_t);
    std::size_t offset = 0;
    const std::size_t index = id % 16U;
    for (std::size_t current = 0; current <= index; ++current) {
        if (offset >= unit_count) {
            buffer[0] = 0;
            set_last_error(abi::kErrorResourceDataNotFound);
            return 0;
        }
        const std::size_t length = data[offset++];
        if (length > unit_count - offset) {
            buffer[0] = 0;
            set_last_error(abi::kErrorResourceDataNotFound);
            return 0;
        }
        if (current == index) {
            const std::size_t copied = std::min(length, static_cast<std::size_t>(buffer_max - 1));
            std::memcpy(buffer, data + offset, copied * sizeof(std::uint16_t));
            buffer[copied] = 0;
            set_last_error(abi::kErrorSuccess);
            return static_cast<int>(copied);
        }
        offset += length;
    }
    buffer[0] = 0;
    set_last_error(abi::kErrorResourceNameNotFound);
    return 0;
}

static WindowSlot* g_captured_window = nullptr;

TL_MSABI void* tl_GetDesktopWindow() noexcept {
    return kDesktopHwndToken;
}

TL_MSABI void* tl_GetFocus() noexcept {
    return g_focused_control != nullptr ? g_focused_control : nullptr;
}

TL_MSABI void* tl_SetCapture(const void* const window) noexcept {
    void* const prev = g_captured_window;
    if (window == nullptr) {
        g_captured_window = nullptr;
    } else if (WindowSlot* const slot = find_window_slot(window); slot != nullptr) {
        g_captured_window = slot;
    }
    return prev;
}

TL_MSABI int tl_ReleaseCapture() noexcept {
    g_captured_window = nullptr;
    return 1;
}

TL_MSABI void* tl_GetCapture() noexcept {
    return g_captured_window;
}

TL_MSABI int tl_BringWindowToTop(const void* const window) noexcept {
    if (window == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    const WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    return 1;
}

TL_MSABI void* tl_GetWindow(const void* const window, const std::uint32_t cmd) noexcept {
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }
    if (cmd == kGwChild) {
        for (auto& w : g_windows) {
            if (w.used && w.parent == slot) return &w;
        }
        return nullptr;
    }
    if (cmd == kGwOwner) {
        return slot->parent;
    }
    if (cmd == kGwHwndFirst) {
        for (auto& w : g_windows) {
            if (w.used && w.parent == slot->parent) return &w;
        }
        return nullptr;
    }
    if (cmd == kGwHwndLast) {
        WindowSlot* last = nullptr;
        for (auto& w : g_windows) {
            if (w.used && w.parent == slot->parent) last = &w;
        }
        return last;
    }
    if (cmd == kGwHwndNext) {
        bool found_current = false;
        for (auto& w : g_windows) {
            if (!w.used || w.parent != slot->parent) continue;
            if (found_current) return &w;
            if (&w == slot) found_current = true;
        }
        return nullptr;
    }
    if (cmd == kGwHwndPrev) {
        WindowSlot* prev = nullptr;
        for (auto& w : g_windows) {
            if (!w.used || w.parent != slot->parent) continue;
            if (&w == slot) return prev;
            prev = &w;
        }
        return nullptr;
    }
    set_last_error(abi::kErrorInvalidParameter);
    return nullptr;
}

TL_MSABI int tl_GetClassNameA(const void* const window, char* const class_name,
                              const int max_count) noexcept {
    const WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr || class_name == nullptr || max_count <= 0 ||
        !mapped_guest_range(class_name, static_cast<std::size_t>(max_count), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string& name = slot->class_name;
    const std::size_t len = std::min(name.size(), static_cast<std::size_t>(max_count - 1));
    std::copy_n(name.data(), len, class_name);
    class_name[len] = '\0';
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(len);
}

TL_MSABI int tl_GetClassNameW(const void* const window, std::uint16_t* const class_name,
                              const int max_count) noexcept {
    const WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr || class_name == nullptr || max_count <= 0 ||
        !mapped_guest_range(class_name, static_cast<std::size_t>(max_count) * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::u16string wide = util::utf8_to_wide(slot->class_name);
    const std::size_t len = std::min(wide.size(), static_cast<std::size_t>(max_count - 1));
    std::copy_n(wide.data(), len, class_name);
    class_name[len] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(len);
}

TL_MSABI std::uint32_t tl_GetWindowThreadProcessId(const void* const window,
                                                   std::uint32_t* const process_id) noexcept {
    (void)window;
    if (process_id != nullptr && mapped_guest_range(process_id, sizeof(*process_id), true)) {
        *process_id = static_cast<std::uint32_t>(getpid());
    }
    return g_current_thread_id != 0 ? g_current_thread_id : kMainThreadId;
}

TL_MSABI abi::Lresult tl_CallWindowProcA(const std::uintptr_t prev_wnd_func, const void* const window,
                                        const std::uint32_t message, const abi::Wparam wparam,
                                        const abi::Lparam lparam) noexcept {
    if (prev_wnd_func == 0) return 0;
    return call_wndproc(prev_wnd_func, const_cast<void*>(window), message, wparam, lparam);
}

TL_MSABI abi::Lresult tl_CallWindowProcW(const std::uintptr_t prev_wnd_func, const void* const window,
                                        const std::uint32_t message, const abi::Wparam wparam,
                                        const abi::Lparam lparam) noexcept {
    if (prev_wnd_func == 0) return 0;
    return call_wndproc(prev_wnd_func, const_cast<void*>(window), message, wparam, lparam);
}

TL_MSABI int tl_PeekMessageA(void* const msg, const void* const window,
                             const std::uint32_t filter_min, const std::uint32_t filter_max,
                             const std::uint32_t remove_msg) noexcept {
    (void)filter_min;
    (void)filter_max;
    if (msg == nullptr || !mapped_guest_range(msg, sizeof(abi::GuestMsg), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr && window == nullptr) {
        for (auto& w : g_windows) {
            if (w.used) { slot = &w; break; }
        }
    }
    if (slot == nullptr) return 0;
    if (slot->has_pending) {
        *static_cast<abi::GuestMsg*>(msg) = slot->pending;
        if ((remove_msg & kPmRemove) != 0) slot->has_pending = false;
        return 1;
    }
    if (!slot->queued_messages.empty()) {
        *static_cast<abi::GuestMsg*>(msg) = slot->queued_messages.front();
        if ((remove_msg & kPmRemove) != 0) slot->queued_messages.pop_front();
        return 1;
    }
    return 0;
}

TL_MSABI int tl_PeekMessageW(void* const msg, const void* const window,
                             const std::uint32_t filter_min, const std::uint32_t filter_max,
                             const std::uint32_t remove_msg) noexcept {
    return tl_PeekMessageA(msg, window, filter_min, filter_max, remove_msg);
}

TL_MSABI int tl_RedrawWindow(const void* const window, const void* const update_rect,
                             const void* const update_rgn, const std::uint32_t flags) noexcept {
    (void)update_rect;
    (void)update_rgn;
    (void)flags;
    if (window == nullptr) return 1;
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    queue_window_message(*slot, 0x000FU /* WM_PAINT */, 0, 0);
    return 1;
}

TL_MSABI int tl_PtInRect(const void* const rect, const std::int32_t x, const std::int32_t y) noexcept {
    if (rect == nullptr || !mapped_guest_range(rect, sizeof(abi::GuestRect), false)) {
        return 0;
    }
    const auto* const r = static_cast<const abi::GuestRect*>(rect);
    return (x >= r->left && x < r->right && y >= r->top && y < r->bottom) ? 1 : 0;
}

TL_MSABI int tl_CopyRect(void* const dest_rect, const void* const src_rect) noexcept {
    if (dest_rect == nullptr || src_rect == nullptr ||
        !mapped_guest_range(dest_rect, sizeof(abi::GuestRect), true) ||
        !mapped_guest_range(src_rect, sizeof(abi::GuestRect), false)) {
        return 0;
    }
    *static_cast<abi::GuestRect*>(dest_rect) = *static_cast<const abi::GuestRect*>(src_rect);
    return 1;
}

struct GuestPoint {
    std::int32_t x{};
    std::int32_t y{};
};

TL_MSABI int tl_MapWindowPoints(const void* const from_window, const void* const to_window,
                                void* const points, const std::uint32_t count) noexcept {
    if (points == nullptr || count == 0 ||
        !mapped_guest_range(points, sizeof(GuestPoint) * count, true)) {
        return 0;
    }
    int dx = 0;
    int dy = 0;
    if (from_window != nullptr && from_window != kDesktopHwndToken) {
        if (const WindowSlot* const from_slot = find_window_slot(from_window); from_slot != nullptr) {
            dx += from_slot->x;
            dy += from_slot->y;
        }
    }
    if (to_window != nullptr && to_window != kDesktopHwndToken) {
        if (const WindowSlot* const to_slot = find_window_slot(to_window); to_slot != nullptr) {
            dx -= to_slot->x;
            dy -= to_slot->y;
        }
    }
    auto* const pts = static_cast<GuestPoint*>(points);
    for (std::uint32_t i = 0; i < count; ++i) {
        pts[i].x += dx;
        pts[i].y += dy;
    }
    return (dy << 16) | (dx & 0xFFFF);
}

TL_MSABI void* tl_MonitorFromWindow(const void* const window, const std::uint32_t flags) noexcept {
    (void)window;
    (void)flags;
    return kDefaultMonitorToken;
}

TL_MSABI std::uint32_t tl_GetSysColor(const int index) noexcept {
    switch (index) {
        case kColorWindow: return 0x00FFFFFFU;
        case kColorWindowText:
        case kColorBtnText:
        case kColorCaptionText:
        case kColorMenuText:
        case kColorInfoText: return 0x00000000U;
        case kColorBtnFace:
        case kColor3dLight:
        case kColorMenu: return 0x00F0F0F0U;
        case kColorHighlight: return 0x00D77800U;
        case kColorHighlightText: return 0x00FFFFFFU;
        case kColorBtnShadow:
        case kColorGrayText: return 0x00A0A0A0U;
        case kColor3dDkShadow:
        case kColorWindowFrame: return 0x00696969U;
        case kColorInfoBk: return 0x00E1FFFFU;
        default: return 0x00FFFFFFU;
    }
}

TL_MSABI std::uint16_t* tl_CharUpperW(std::uint16_t* const str) noexcept {
    if (reinterpret_cast<std::uintptr_t>(str) <= 0xFFFFU) {
        auto ch = static_cast<char16_t>(reinterpret_cast<std::uintptr_t>(str));
        if (ch >= u'a' && ch <= u'z') {
            ch = static_cast<char16_t>(ch - u'a' + u'A');
        }
        return reinterpret_cast<std::uint16_t*>(static_cast<std::uintptr_t>(ch));
    }
    if (!mapped_guest_wstring(str)) return str;
    for (std::uint16_t* p = str; *p != 0; ++p) {
        if (*p >= u'a' && *p <= u'z') {
            *p = static_cast<std::uint16_t>(*p - u'a' + u'A');
        }
    }
    return str;
}

TL_MSABI std::uint16_t* tl_CharLowerW(std::uint16_t* const str) noexcept {
    if (reinterpret_cast<std::uintptr_t>(str) <= 0xFFFFU) {
        auto ch = static_cast<char16_t>(reinterpret_cast<std::uintptr_t>(str));
        if (ch >= u'A' && ch <= u'Z') {
            ch = static_cast<char16_t>(ch - u'A' + u'a');
        }
        return reinterpret_cast<std::uint16_t*>(static_cast<std::uintptr_t>(ch));
    }
    if (!mapped_guest_wstring(str)) return str;
    for (std::uint16_t* p = str; *p != 0; ++p) {
        if (*p >= u'A' && *p <= u'Z') {
            *p = static_cast<std::uint16_t>(*p - u'A' + u'a');
        }
    }
    return str;
}

TL_MSABI const char* tl_CharPrevExA(const std::uint32_t code_page, const char* const start,
                                     const char* const current, const std::uint32_t flags) noexcept {
    (void)code_page;
    (void)flags;
    if (start == nullptr || current == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return start;
    }
    if (!mapped_guest_cstring(start)) {
        set_last_error(abi::kErrorInvalidParameter);
        return start;
    }
    if (current <= start) {
        set_last_error(abi::kErrorSuccess);
        return start;
    }
    const std::uintptr_t start_addr = reinterpret_cast<std::uintptr_t>(start);
    const std::uintptr_t cur_addr = reinterpret_cast<std::uintptr_t>(current);
    if (cur_addr - start_addr > 1U << 20U) {
        set_last_error(abi::kErrorInvalidParameter);
        return start;
    }
    if (!mapped_guest_range(current - 1, 1, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return start;
    }
    // DBCS não suportado (CP 932/936 etc. sempre retorna FALSE em IsDBCSLeadByteEx),
    // então o char anterior é sempre current-1 para o 7z.dll.
    set_last_error(abi::kErrorSuccess);
    return current - 1;
}

TL_MSABI int tl_DrawTextA(const void* const dc, const char* const text, const int count,
                          void* const rect, const std::uint32_t format) noexcept {
    if (text == nullptr || rect == nullptr || !mapped_guest_range(rect, sizeof(abi::GuestRect), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::size_t len = (count < 0) ? std::strlen(text) : static_cast<std::size_t>(count);
    auto* const r = static_cast<abi::GuestRect*>(rect);
    constexpr int kLineHeight = 16;
    constexpr int kCharWidth = 8;
    if ((format & kDtCalcRect) != 0) {
        r->right = r->left + static_cast<std::int32_t>(len * kCharWidth);
        r->bottom = r->top + kLineHeight;
        return kLineHeight;
    }
    if (dc != nullptr) {
        (void)tl_TextOut(dc, r->left, r->top, text, static_cast<int>(len));
    }
    return kLineHeight;
}

TL_MSABI int tl_DrawTextW(const void* const dc, const std::uint16_t* const text, const int count,
                          void* const rect, const std::uint32_t format) noexcept {
    if (text == nullptr || rect == nullptr || !mapped_guest_range(rect, sizeof(abi::GuestRect), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::size_t len = 0;
    if (count < 0) {
        while (text[len] != 0) ++len;
    } else {
        len = static_cast<std::size_t>(count);
    }
    auto* const r = static_cast<abi::GuestRect*>(rect);
    constexpr int kLineHeight = 16;
    constexpr int kCharWidth = 8;
    if ((format & kDtCalcRect) != 0) {
        r->right = r->left + static_cast<std::int32_t>(len * kCharWidth);
        r->bottom = r->top + kLineHeight;
        return kLineHeight;
    }
    if (dc != nullptr) {
        const std::string utf8 = util::wide_to_utf8(text, len);
        (void)tl_TextOut(dc, r->left, r->top, utf8.c_str(), static_cast<int>(utf8.size()));
    }
    return kLineHeight;
}

TL_MSABI int tl_SetUserObjectInformationW(void* const obj, const int index, void* const info,
                                          const std::uint32_t length) noexcept {
    (void)obj;
    (void)index;
    (void)info;
    (void)length;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_WaitForInputIdle(void* const process, const std::uint32_t milliseconds) noexcept {
    (void)process;
    (void)milliseconds;
    set_last_error(abi::kErrorSuccess);
    return 0; // WAIT_OBJECT_0
}

TL_MSABI void* tl_FindWindowExW(void* const hwnd_parent, void* const hwnd_child_after,
                                const std::uint16_t* const class_name,
                                const std::uint16_t* const window_name) noexcept {
    (void)hwnd_parent;
    (void)hwnd_child_after;
    (void)class_name;
    (void)window_name;
    set_last_error(abi::kErrorSuccess);
    return nullptr;
}

TL_MSABI int tl_SetProcessDefaultLayout(const std::uint32_t default_layout) noexcept {
    (void)default_layout;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_OpenClipboard(void* const hwnd_new_owner) noexcept {
    (void)hwnd_new_owner;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_CloseClipboard(void) noexcept {
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_SetClipboardData(const std::uint32_t format, void* const mem) noexcept {
    (void)format;
    set_last_error(abi::kErrorSuccess);
    return mem;
}

TL_MSABI int tl_EmptyClipboard(void) noexcept {
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_MessageBoxExW(void* const hwnd, const std::uint16_t* const text,
                              const std::uint16_t* const caption, const std::uint32_t type,
                              const std::uint16_t language_id) noexcept {
    (void)language_id;
    return tl_MessageBoxW(hwnd, text, caption, type);
}

TL_MSABI int tl_DrawIconEx(void* const hdc, const int x_left, const int y_top, void* const hicon,
                           const int cx_width, const int cy_width, const std::uint32_t step_if_ani_cur,
                           void* const hbr_flicker_free_draw, const std::uint32_t flags) noexcept {
    (void)hdc;
    (void)x_left;
    (void)y_top;
    (void)hicon;
    (void)cx_width;
    (void)cy_width;
    (void)step_if_ani_cur;
    (void)hbr_flicker_free_draw;
    (void)flags;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_LoadImageW(void* const hinst, const std::uint16_t* const name, const std::uint32_t type,
                             const int cx, const int cy, const std::uint32_t fu_load) noexcept {
    (void)hinst;
    (void)name;
    (void)type;
    (void)cx;
    (void)cy;
    (void)fu_load;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x1000);
}

TL_MSABI int tl_ClientToScreen(void* const hwnd, void* const point) noexcept {
    (void)hwnd;
    (void)point;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_GetMenu(void* const hwnd) noexcept {
    (void)hwnd;
    // Ainda não há um objeto HMENU nativo associado à janela. Retornar um
    // marcador inteiro fazia o convidado tratá-lo como ponteiro e causava
    // SIGSEGV durante a montagem do menu do 7-Zip.
    return nullptr;
}

TL_MSABI int tl_SetMenu(void* const hwnd, void* const menu) noexcept {
    (void)hwnd;
    (void)menu;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_GetSubMenu(void* const menu, const int pos) noexcept {
    (void)menu;
    (void)pos;
    return nullptr;
}

TL_MSABI int tl_GetMenuItemCount(void* const menu) noexcept {
    (void)menu;
    return 0;
}

TL_MSABI int tl_GetMenuItemInfoW(void* const menu, const std::uint32_t item, const int f_by_position,
                                void* const mii) noexcept {
    (void)menu;
    (void)f_by_position;
    // MENUITEMINFOW em Win64: os ponteiros ficam alinhados em 8 bytes após
    // os cinco campos UINT iniciais.
    struct GuestMenuItemInfoW {
        std::uint32_t cb_size;
        std::uint32_t f_mask;
        std::uint32_t f_type;
        std::uint32_t f_state;
        std::uint32_t item_id;
        void* sub_menu;
        void* checked_bitmap;
        void* unchecked_bitmap;
        std::uintptr_t item_data;
        std::uint16_t* type_data;
        std::uint32_t char_count;
    };
    if (mii != nullptr && !mapped_guest_range(mii, sizeof(GuestMenuItemInfoW), true)) {
        const std::array<diagnostics::TraceField, 4> fields{
            diagnostics::TraceField{"symbol", "GetMenuItemInfoW"},
            diagnostics::TraceField{"status", "invalid-output"},
            diagnostics::TraceField{"item", std::to_string(item)},
            diagnostics::TraceField{"mii", std::to_string(reinterpret_cast<std::uintptr_t>(mii))}};
        runtime_trace("GetMenuItemInfoW", fields, 4);
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (mii != nullptr) {
        auto* const info = static_cast<GuestMenuItemInfoW*>(mii);
        const std::uint32_t requested_mask = info->f_mask;
        info->cb_size = sizeof(GuestMenuItemInfoW);
        info->f_type = 0;
        info->f_state = 0;
        info->item_id = 100U + item;
        info->sub_menu = nullptr;
        info->checked_bitmap = nullptr;
        info->unchecked_bitmap = nullptr;
        info->item_data = 0;
        // O buffer de saída pode ter sido preparado por uma versão diferente
        // de MENUITEMINFO. Não devolva ao convidado ponteiros inventados nem
        // reutilize type_data sem validar o layout e a capacidade completos.
        if ((requested_mask & 0x00000040U) != 0U) {
            info->type_data = nullptr;
            info->char_count = 0;
        }
        // Não anuncie um item sintético como válido: o 7-Zip usa o retorno
        // para decidir se deve continuar enumerando o menu. Retornar sucesso
        // sem um catálogo real provoca recursão durante o WM_CREATE.
        set_last_error(abi::kErrorNotSupported);
        return 0;
    }
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "GetMenuItemInfoW"},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"item", std::to_string(item)},
        diagnostics::TraceField{"mii", std::to_string(reinterpret_cast<std::uintptr_t>(mii))}};
    runtime_trace("GetMenuItemInfoW", fields, 4);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetMenuItemInfoW(void* const menu, const std::uint32_t item, const int f_by_position,
                                const void* const mii) noexcept {
    (void)menu;
    (void)item;
    (void)f_by_position;
    (void)mii;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_InsertMenuItemW(void* const menu, const std::uint32_t item, const int f_by_position,
                               const void* const mii) noexcept {
    (void)menu;
    (void)item;
    (void)f_by_position;
    (void)mii;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_RemoveMenu(void* const menu, const std::uint32_t position, const std::uint32_t flags) noexcept {
    (void)menu;
    (void)position;
    (void)flags;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_EnableMenuItem(void* const menu, const std::uint32_t item, const std::uint32_t enable) noexcept {
    (void)menu;
    (void)item;
    (void)enable;
    return 0;
}

TL_MSABI std::uint32_t tl_CheckMenuItem(void* const menu, const std::uint32_t item, const std::uint32_t check) noexcept {
    (void)menu;
    (void)item;
    (void)check;
    return 0;
}

TL_MSABI int tl_CheckMenuRadioItem(void* const menu, const std::uint32_t first, const std::uint32_t last,
                                  const std::uint32_t check, const std::uint32_t flags) noexcept {
    (void)menu;
    (void)first;
    (void)last;
    (void)check;
    (void)flags;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_DrawMenuBar(void* const hwnd) noexcept {
    (void)hwnd;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_TrackPopupMenuEx(void* const menu, const std::uint32_t flags, const int x, const int y,
                                void* const hwnd, void* const params) noexcept {
    (void)menu;
    (void)flags;
    (void)x;
    (void)y;
    (void)hwnd;
    (void)params;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_LoadMenuW(void* const instance, const std::uint16_t* const menu_name) noexcept {
    (void)instance;
    (void)menu_name;
    set_last_error(abi::kErrorResourceNotFound);
    return nullptr;
}

TL_MSABI int tl_CheckDlgButton(void* const hdlg, const int id_button, const std::uint32_t check) noexcept {
    (void)hdlg;
    (void)id_button;
    (void)check;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_IsDlgButtonChecked(void* const hdlg, const int id_button) noexcept {
    (void)hdlg;
    (void)id_button;
    return 0;
}

TL_MSABI int tl_CheckRadioButton(void* const hdlg, const int first_button, const int last_button,
                                 const int check_button) noexcept {
    (void)hdlg;
    (void)first_button;
    (void)last_button;
    (void)check_button;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_MapDialogRect(void* const hdlg, void* const rect) noexcept {
    (void)hdlg;
    (void)rect;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetDialogBaseUnits() noexcept {
    return 0x00080004; // 8 high, 4 low
}

TL_MSABI void* tl_WindowFromPoint(const std::int64_t point_coord) noexcept {
    (void)point_coord;
    return reinterpret_cast<void*>(0x1000);
}

TL_MSABI void* tl_ChildWindowFromPointEx(void* const hwnd, const std::int64_t point_coord,
                                         const std::uint32_t flags) noexcept {
    (void)hwnd;
    (void)point_coord;
    (void)flags;
    return reinterpret_cast<void*>(0x1000);
}

TL_MSABI int tl_GetWindowPlacement(void* const hwnd, void* const placement) noexcept {
    (void)hwnd;
    (void)placement;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetWindowPlacement(void* const hwnd, const void* const placement) noexcept {
    (void)hwnd;
    (void)placement;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_IsWindowEnabled(void* const hwnd) noexcept {
    (void)hwnd;
    return 1;
}

TL_MSABI int tl_IsZoomed(void* const hwnd) noexcept {
    (void)hwnd;
    return 0;
}

TL_MSABI int tl_GetClassInfoW(void* const instance, const std::uint16_t* const class_name,
                              void* const wnd_class) noexcept {
    (void)instance;
    (void)class_name;
    (void)wnd_class;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetMonitorInfoA(void* const monitor, void* const mi) noexcept {
    (void)monitor;
    (void)mi;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SystemParametersInfoW(const std::uint32_t action, const std::uint32_t param1,
                                      void* const param2, const std::uint32_t win_ini) noexcept {
    (void)action;
    (void)param1;
    (void)param2;
    (void)win_ini;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_LoadAcceleratorsW(void* const instance, const std::uint16_t* const table_name) noexcept {
    (void)instance;
    (void)table_name;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x41434345ULL); // 'ACCE'
}

TL_MSABI int tl_TranslateAcceleratorW(void* const hwnd, void* const accel_table, void* const msg) noexcept {
    (void)hwnd;
    (void)accel_table;
    (void)msg;
    return 0;
}

TL_MSABI void* tl_LoadBitmapW(void* const instance, const std::uint16_t* const bitmap_name) noexcept {
    (void)instance;
    (void)bitmap_name;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x424D50ULL); // 'BMP'
}

TL_MSABI std::uint32_t tl_MapVirtualKeyW(const std::uint32_t code, const std::uint32_t map_type) noexcept {
    (void)map_type;
    return code;
}

TL_MSABI std::uint32_t tl_RegisterClipboardFormatW(const std::uint16_t* const format_name) noexcept {
    (void)format_name;
    set_last_error(abi::kErrorSuccess);
    return 0xC001; // Custom format ID
}

TL_MSABI int tl_ScreenToClient(void* const hwnd, void* const point) noexcept {
    (void)hwnd;
    (void)point;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_CreateCaret(void* const hwnd, void* const bitmap, const int width, const int height) noexcept {
    (void)hwnd;
    (void)bitmap;
    (void)width;
    (void)height;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_DestroyCaret() noexcept {
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetCaretPos(const int x, const int y) noexcept {
    (void)x;
    (void)y;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_ShowCaret(void* const hwnd) noexcept {
    (void)hwnd;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_HideCaret(void* const hwnd) noexcept {
    (void)hwnd;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetCaretPos(void* const point) noexcept {
    if (point != nullptr && mapped_guest_range(point, 8, true)) {
        std::memset(point, 0, 8);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetScrollInfo(void* const hwnd, const int bar, const void* const scroll_info, const int redraw) noexcept {
    (void)hwnd;
    (void)bar;
    (void)scroll_info;
    (void)redraw;
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI int tl_GetScrollInfo(void* const hwnd, const int bar, void* const scroll_info) noexcept {
    (void)hwnd;
    (void)bar;
    (void)scroll_info;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_ShowScrollBar(void* const hwnd, const int bar, const int show) noexcept {
    (void)hwnd;
    (void)bar;
    (void)show;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_EnableScrollBar(void* const hwnd, const std::uint32_t flags, const std::uint32_t arrows) noexcept {
    (void)hwnd;
    (void)flags;
    (void)arrows;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetScrollPos(void* const hwnd, const int bar, const int pos, const int redraw) noexcept {
    (void)hwnd;
    (void)bar;
    (void)pos;
    (void)redraw;
    return pos;
}

TL_MSABI int tl_GetScrollPos(void* const hwnd, const int bar) noexcept {
    (void)hwnd;
    (void)bar;
    return 0;
}

TL_MSABI int tl_SetScrollRange(void* const hwnd, const int bar, const int min_pos, const int max_pos, const int redraw) noexcept {
    (void)hwnd;
    (void)bar;
    (void)min_pos;
    (void)max_pos;
    (void)redraw;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetScrollRange(void* const hwnd, const int bar, int* const min_pos, int* const max_pos) noexcept {
    (void)hwnd;
    (void)bar;
    if (min_pos != nullptr && mapped_guest_range(min_pos, sizeof(int), true)) {
        *min_pos = 0;
    }
    if (max_pos != nullptr && mapped_guest_range(max_pos, sizeof(int), true)) {
        *max_pos = 100;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_FlashWindow(void* const hwnd, const int invert) noexcept {
    (void)hwnd;
    (void)invert;
    return 0;
}

TL_MSABI int tl_FlashWindowEx(void* const flash_info) noexcept {
    (void)flash_info;
    return 1;
}

TL_MSABI int tl_SetSysColors(const int count, const int* const elements, const std::uint32_t* const colors) noexcept {
    (void)count;
    (void)elements;
    (void)colors;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_MessageBeep(const std::uint32_t type) noexcept {
    (void)type;
    return 1;
}

TL_MSABI void* tl_GetClipboardData(const std::uint32_t format) noexcept {
    (void)format;
    return nullptr;
}

TL_MSABI int tl_IsClipboardFormatAvailable(const std::uint32_t format) noexcept {
    (void)format;
    return 0;
}

TL_MSABI std::uint32_t tl_RegisterClipboardFormatA(const char* const format_name) noexcept {
    (void)format_name;
    set_last_error(abi::kErrorSuccess);
    return 0xC002;
}

TL_MSABI int tl_CountClipboardFormats() noexcept {
    return 0;
}

TL_MSABI std::uint32_t tl_EnumClipboardFormats(const std::uint32_t format) noexcept {
    (void)format;
    return 0;
}

TL_MSABI std::uint32_t tl_GetDpiForWindow(void* const hwnd) noexcept {
    (void)hwnd;
    return 96; // Standard 96 DPI (100% scaling)
}

TL_MSABI std::uint32_t tl_GetDpiForSystem() noexcept {
    return 96;
}

TL_MSABI int tl_SetProcessDpiAwarenessContext(void* const dpi_context) noexcept {
    (void)dpi_context;
    return 1;
}

TL_MSABI int tl_SetProcessDPIAware() noexcept {
    return 1;
}

TL_MSABI int tl_GetSystemMetricsForDpi(const int index, const std::uint32_t dpi) noexcept {
    (void)dpi;
    return tl_GetSystemMetrics(index);
}

TL_MSABI int tl_AdjustWindowRectExForDpi(void* const rect, const std::uint32_t style, const int menu,
                                        const std::uint32_t ex_style, const std::uint32_t dpi) noexcept {
    (void)style;
    (void)menu;
    (void)ex_style;
    (void)dpi;
    if (rect != nullptr && mapped_guest_range(rect, 16, true)) {
        // Adjust borders if needed
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreateIconIndirect(const void* const icon_info) noexcept {
    (void)icon_info;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x49434F4EULL); // 'ICON'
}

TL_MSABI int tl_GetIconInfo(void* const icon, void* const icon_info) noexcept {
    (void)icon;
    if (icon_info != nullptr && mapped_guest_range(icon_info, 32, true)) {
        std::memset(icon_info, 0, 32);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetIconInfoExW(void* const icon, void* const icon_info_ex) noexcept {
    (void)icon;
    if (icon_info_ex != nullptr && mapped_guest_range(icon_info_ex, 40, true)) {
        std::memset(icon_info_ex, 0, 40);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_DrawIcon(void* const hdc, const int x, const int y, void* const icon) noexcept {
    (void)hdc;
    (void)x;
    (void)y;
    (void)icon;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CopyIcon(void* const icon) noexcept {
    return icon;
}

TL_MSABI int tl_SetWindowRgn(void* const hwnd, void* const rgn, const int redraw) noexcept {
    (void)hwnd;
    (void)rgn;
    (void)redraw;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetWindowRgn(void* const hwnd, void* const rgn) noexcept {
    (void)hwnd;
    (void)rgn;
    set_last_error(abi::kErrorSuccess);
    return 2; // SIMPLEREGION
}

TL_MSABI int tl_GetWindowRgnBox(void* const hwnd, void* const rect) noexcept {
    (void)hwnd;
    if (rect != nullptr && mapped_guest_range(rect, 16, true)) {
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(rect) + 0) = 0;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(rect) + 4) = 0;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(rect) + 8) = 1024;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(rect) + 12) = 768;
    }
    set_last_error(abi::kErrorSuccess);
    return 2; // SIMPLEREGION
}

TL_MSABI int tl_DrawEdge(void* const hdc, void* const rect, const std::uint32_t edge, const std::uint32_t flags) noexcept {
    (void)hdc;
    (void)rect;
    (void)edge;
    (void)flags;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_DrawFrameControl(void* const hdc, void* const rect, const std::uint32_t type, const std::uint32_t state) noexcept {
    (void)hdc;
    (void)rect;
    (void)type;
    (void)state;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_DrawFocusRect(void* const hdc, const void* const rect) noexcept {
    (void)hdc;
    (void)rect;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_FrameRect(void* const hdc, const void* const rect, void* const brush) noexcept {
    (void)hdc;
    (void)rect;
    (void)brush;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_InvertRect(void* const hdc, const void* const rect) noexcept {
    (void)hdc;
    (void)rect;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetUpdateRect(void* const hwnd, void* const rect, const int erase) noexcept {
    (void)hwnd;
    (void)erase;
    if (rect != nullptr && mapped_guest_range(rect, 16, true)) {
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(rect) + 0) = 0;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(rect) + 4) = 0;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(rect) + 8) = 1024;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(rect) + 12) = 768;
    }
    return 1;
}

TL_MSABI int tl_GetUpdateRgn(void* const hwnd, void* const rgn, const int erase) noexcept {
    (void)hwnd;
    (void)rgn;
    (void)erase;
    return 2; // SIMPLEREGION
}

TL_MSABI int tl_InvalidateRgn(void* const hwnd, void* const rgn, const int erase) noexcept {
    (void)hwnd;
    (void)rgn;
    (void)erase;
    return 1;
}

TL_MSABI int tl_ValidateRgn(void* const hwnd, void* const rgn) noexcept {
    (void)hwnd;
    (void)rgn;
    return 1;
}

TL_MSABI int tl_ScrollWindow(void* const hwnd, const int x_amount, const int y_amount,
                             const void* const rect, const void* const clip_rect) noexcept {
    (void)hwnd;
    (void)x_amount;
    (void)y_amount;
    (void)rect;
    (void)clip_rect;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_ScrollWindowEx(void* const hwnd, const int dx, const int dy, const void* const scroll_rect,
                               const void* const clip_rect, void* const update_rgn, void* const update_rect,
                               const std::uint32_t flags) noexcept {
    (void)hwnd;
    (void)dx;
    (void)dy;
    (void)scroll_rect;
    (void)clip_rect;
    (void)update_rgn;
    (void)flags;
    if (update_rect != nullptr && mapped_guest_range(update_rect, 16, true)) {
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(update_rect) + 0) = 0;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(update_rect) + 4) = 0;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(update_rect) + 8) = 1024;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(update_rect) + 12) = 768;
    }
    set_last_error(abi::kErrorSuccess);
    return 2; // SIMPLEREGION
}

TL_MSABI int tl_RegisterHotKey(void* const hwnd, const int id, const std::uint32_t modifiers, const std::uint32_t vk) noexcept {
    (void)hwnd;
    (void)id;
    (void)modifiers;
    (void)vk;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_UnregisterHotKey(void* const hwnd, const int id) noexcept {
    (void)hwnd;
    (void)id;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_GetProcessWindowStation() noexcept {
    return reinterpret_cast<void*>(0x57535441ULL); // 'WSTA'
}

TL_MSABI int tl_GetUserObjectInformationW(void* const handle, const int index,
                                          void* const info, const std::uint32_t length,
                                          std::uint32_t* const length_needed) noexcept {
    (void)handle;
    (void)index;
    if (length_needed != nullptr && mapped_guest_range(length_needed, sizeof(std::uint32_t), true)) {
        *length_needed = sizeof(std::uint32_t);
    }
    if (info != nullptr && length >= sizeof(std::uint32_t) && mapped_guest_range(info, sizeof(std::uint32_t), true)) {
        *reinterpret_cast<std::uint32_t*>(info) = 1; // WSF_VISIBLE
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_GetShellWindow() noexcept {
    return reinterpret_cast<void*>(0x53484C4CULL); // 'SHLL'
}

TL_MSABI int tl_EnumDisplayDevicesA(const char* const device, const std::uint32_t dev_num,
                                    void* const display_device, const std::uint32_t flags) noexcept {
    (void)device;
    (void)flags;
    if (dev_num > 0 || display_device == nullptr || !mapped_guest_range(display_device, 40, true)) {
        set_last_error(abi::kErrorSuccess);
        return 0;
    }
    // DISPLAY_DEVICEA: cb(4), DeviceName[32], DeviceString[128], StateFlags(4), DeviceID[128], DeviceKey[128]
    struct DummyDisplayDeviceA {
        std::uint32_t cb;
        char DeviceName[32];
        char DeviceString[128];
        std::uint32_t StateFlags;
        char DeviceID[128];
        char DeviceKey[128];
    }* dd = reinterpret_cast<DummyDisplayDeviceA*>(display_device);
    const std::uint32_t cb = dd->cb;
    std::memset(display_device, 0, std::min<std::size_t>(cb, sizeof(DummyDisplayDeviceA)));
    dd->cb = cb;
    std::strncpy(dd->DeviceName, "\\\\.\\DISPLAY1", sizeof(dd->DeviceName) - 1);
    std::strncpy(dd->DeviceString, "Generic PnP Monitor", sizeof(dd->DeviceString) - 1);
    dd->StateFlags = 1 | 4; // DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreateDialogParamA(void* const instance, const char* const template_name, void* const wnd_parent, void* const dialog_func, const std::intptr_t init_param) noexcept {
    (void)instance;
    (void)template_name;
    (void)wnd_parent;
    (void)dialog_func;
    (void)init_param;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x444C4731ULL); // 'DLG1'
}

TL_MSABI void* tl_CreateMenu() noexcept {
    set_last_error(abi::kErrorNotSupported);
    return nullptr;
}

TL_MSABI std::intptr_t tl_DefDlgProcA(void* const hwnd, const std::uint32_t msg, const std::uintptr_t wparam, const std::intptr_t lparam) noexcept {
    return tl_DefWindowProcA(hwnd, msg, wparam, lparam);
}

TL_MSABI int tl_DeleteMenu(void* const menu, const std::uint32_t position, const std::uint32_t flags) noexcept {
    (void)menu;
    (void)position;
    (void)flags;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::intptr_t tl_DialogBoxParamA(void* const instance, const char* const template_name, void* const wnd_parent, void* const dialog_func, const std::intptr_t init_param) noexcept {
    (void)instance;
    (void)template_name;
    (void)wnd_parent;
    (void)dialog_func;
    (void)init_param;
    set_last_error(abi::kErrorSuccess);
    return 1; // IDOK
}

TL_MSABI std::uint32_t tl_GetCaretBlinkTime() noexcept {
    return 530; // standard 530 ms
}

TL_MSABI void* tl_GetClipboardOwner() noexcept {
    return nullptr;
}

TL_MSABI std::uint32_t tl_GetDoubleClickTime() noexcept {
    return 500; // standard 500 ms
}

TL_MSABI void* tl_GetForegroundWindow() noexcept {
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x57494E31ULL);
}

TL_MSABI void* tl_GetKeyboardLayout(const std::uint32_t thread_id) noexcept {
    (void)thread_id;
    return reinterpret_cast<void*>(0x04090409ULL); // US English
}

TL_MSABI int tl_GetKeyboardState(std::uint8_t* const key_states) noexcept {
    if (key_states != nullptr && mapped_guest_range(key_states, 256, true)) {
        std::memset(key_states, 0, 256);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetMessageTime() noexcept {
    return static_cast<std::uint32_t>(tl_GetTickCount());
}

TL_MSABI std::uint32_t tl_GetQueueStatus(const std::uint32_t flags) noexcept {
    (void)flags;
    return 0;
}

TL_MSABI void* tl_GetSysColorBrush(const int index) noexcept {
    (void)index;
    return tl_GetStockObject(0); // WHITE_BRUSH
}

TL_MSABI void* tl_GetSystemMenu(void* const hwnd, const int b_revert) noexcept {
    (void)hwnd;
    (void)b_revert;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x5359534DULL); // 'SYSM'
}

TL_MSABI int tl_InsertMenuA(void* const menu, const std::uint32_t position, const std::uint32_t flags, const std::uintptr_t id_new_item, const char* const new_item) noexcept {
    (void)menu;
    (void)position;
    (void)flags;
    (void)id_new_item;
    (void)new_item;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_IsDialogMessageA(void* const hwnd, void* const msg) noexcept {
    return tl_IsDialogMessageW(hwnd, msg);
}

TL_MSABI int tl_IsIconic(void* const hwnd) noexcept {
    (void)hwnd;
    return 0;
}

TL_MSABI void* tl_LoadImageA(void* const instance, const char* const name, const std::uint32_t type, const int cx, const int cy, const std::uint32_t load) noexcept {
    (void)instance;
    (void)name;
    (void)type;
    (void)cx;
    (void)cy;
    (void)load;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x494D4147ULL); // 'IMAG'
}

TL_MSABI int tl_MessageBoxIndirectW(const void* const msg_box_params) noexcept {
    (void)msg_box_params;
    set_last_error(abi::kErrorSuccess);
    return 1; // IDOK
}

TL_MSABI int tl_OffsetRect(void* const rect, const int dx, const int dy) noexcept {
    if (rect != nullptr && mapped_guest_range(rect, 16, true)) {
        auto* const r = reinterpret_cast<std::int32_t*>(rect);
        r[0] += dx; // left
        r[1] += dy; // top
        r[2] += dx; // right
        r[3] += dy; // bottom
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    set_last_error(abi::kErrorInvalidParameter);
    return 0;
}

TL_MSABI std::uint32_t tl_RegisterWindowMessageA(const char* const string) noexcept {
    (void)string;
    set_last_error(abi::kErrorSuccess);
    return 0xC001U;
}

TL_MSABI std::intptr_t tl_SendDlgItemMessageA(void* const hwnd, const int id_dlg_item, const std::uint32_t msg, const std::uintptr_t wparam, const std::intptr_t lparam) noexcept {
    (void)hwnd;
    (void)id_dlg_item;
    (void)msg;
    (void)wparam;
    (void)lparam;
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI void* tl_SetActiveWindow(void* const hwnd) noexcept {
    set_last_error(abi::kErrorSuccess);
    return hwnd;
}

TL_MSABI int tl_SetDlgItemTextA(void* const hwnd, const int id_dlg_item, const char* const text) noexcept {
    (void)hwnd;
    (void)id_dlg_item;
    (void)text;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetKeyboardState(const std::uint8_t* const key_states) noexcept {
    (void)key_states;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SystemParametersInfoA(const std::uint32_t action, const std::uint32_t param1, void* const param2, const std::uint32_t winini) noexcept {
    return tl_SystemParametersInfoW(action, param1, param2, winini);
}

TL_MSABI int tl_ToAsciiEx(const std::uint32_t vk, const std::uint32_t scan_code, const std::uint8_t* const key_state, std::uint16_t* const char_out, const std::uint32_t flags, void* const dwhkl) noexcept {
    (void)scan_code;
    (void)key_state;
    (void)flags;
    (void)dwhkl;
    if (char_out != nullptr && mapped_guest_range(char_out, sizeof(std::uint16_t), true)) {
        *char_out = static_cast<std::uint16_t>(vk & 0xFF);
        return 1;
    }
    return 0;
}

TL_MSABI std::uint32_t tl_RegisterWindowMessageW(const wchar_t* const lpString) noexcept {
    (void)lpString;
    return 0xC001;
}

TL_MSABI void* tl_RemovePropW(void* const hWnd, const wchar_t* const lpString) noexcept {
    (void)hWnd;
    (void)lpString;
    return nullptr;
}

TL_MSABI void* tl_GetPropW(void* const hWnd, const wchar_t* const lpString) noexcept {
    (void)hWnd;
    (void)lpString;
    return nullptr;
}

TL_MSABI int tl_SetPropW(void* const hWnd, const wchar_t* const lpString, void* const hData) noexcept {
    (void)hWnd;
    (void)lpString;
    (void)hData;
    return 1;
}

TL_MSABI int tl_ValidateRect(void* const hWnd, const void* const lpRect) noexcept {
    (void)hWnd;
    (void)lpRect;
    return 1;
}

TL_MSABI int tl_DestroyCursor(void* const hCursor) noexcept {
    (void)hCursor;
    return 1;
}

TL_MSABI void tl_NotifyWinEvent(const std::uint32_t event, void* const hwnd, const std::int32_t idObject, const std::int32_t idChild) noexcept {
    (void)event;
    (void)hwnd;
    (void)idObject;
    (void)idChild;
}

TL_MSABI void* tl_MonitorFromPoint(const int x, const int y, const std::uint32_t dwFlags) noexcept {
    (void)x;
    (void)y;
    (void)dwFlags;
    return reinterpret_cast<void*>(0x10001);
}

TL_MSABI void* tl_MonitorFromRect(const void* const lprc, const std::uint32_t dwFlags) noexcept {
    (void)lprc;
    (void)dwFlags;
    return reinterpret_cast<void*>(0x10001);
}

TL_MSABI int tl_GetMonitorInfoW(void* const hMonitor, void* const lpmi) noexcept {
    (void)hMonitor;
    if (lpmi != nullptr && mapped_guest_range(lpmi, 40, true)) {
        std::memset(lpmi, 0, 40);
        *reinterpret_cast<std::uint32_t*>(lpmi) = 40;
        auto* rects = reinterpret_cast<std::int32_t*>(static_cast<char*>(lpmi) + 4);
        rects[0] = 0; rects[1] = 0; rects[2] = 1920; rects[3] = 1080;
        rects[4] = 0; rects[5] = 0; rects[6] = 1920; rects[7] = 1080;
        rects[8] = 1;
    }
    return 1;
}

TL_MSABI int tl_AdjustWindowRectEx(void* const lpRect, const std::uint32_t dwStyle, const int bMenu, const std::uint32_t dwExStyle) noexcept {
    (void)lpRect;
    (void)dwStyle;
    (void)bMenu;
    (void)dwExStyle;
    return 1;
}

TL_MSABI std::uint32_t tl_GetDlgItemTextA(void* const hDlg, const int nIDDlgItem, char* const lpString, const int cchMax) noexcept {
    (void)hDlg;
    (void)nIDDlgItem;
    if (lpString != nullptr && cchMax > 0 && mapped_guest_range(lpString, static_cast<std::size_t>(cchMax), true)) {
        lpString[0] = 0;
    }
    return 0;
}

TL_MSABI std::uint32_t tl_GetDlgItemTextW(void* const hDlg, const int nIDDlgItem, wchar_t* const lpString, const int cchMax) noexcept {
    (void)hDlg;
    (void)nIDDlgItem;
    if (lpString != nullptr && cchMax > 0 && mapped_guest_range(lpString, static_cast<std::size_t>(cchMax) * sizeof(wchar_t), true)) {
        lpString[0] = 0;
    }
    return 0;
}

TL_MSABI void* tl_BeginDeferWindowPos(const int nNumWindows) noexcept {
    (void)nNumWindows;
    return reinterpret_cast<void*>(0x44454650ULL);
}

TL_MSABI void* tl_DeferWindowPos(void* const hWinPosInfo, void* const hWnd, void* const hWndInsertAfter, const int x, const int y, const int cx, const int cy, const std::uint32_t uFlags) noexcept {
    (void)hWnd;
    (void)hWndInsertAfter;
    (void)x;
    (void)y;
    (void)cx;
    (void)cy;
    (void)uFlags;
    return hWinPosInfo;
}

TL_MSABI int tl_EndDeferWindowPos(void* const hWinPosInfo) noexcept {
    (void)hWinPosInfo;
    return 1;
}

TL_MSABI int tl_UnregisterClassW(const wchar_t* const lpClassName, void* const hInstance) noexcept {
    (void)lpClassName;
    (void)hInstance;
    return 1;
}

TL_MSABI void* tl_GetActiveWindow() noexcept {
    return tl_GetForegroundWindow();
}

TL_MSABI std::intptr_t tl_CallNextHookEx(void* const hhk, const int nCode, const std::uintptr_t wParam, const std::intptr_t lParam) noexcept {
    (void)hhk;
    (void)nCode;
    (void)wParam;
    (void)lParam;
    return 0;
}

TL_MSABI int tl_UnhookWindowsHookEx(void* const hhk) noexcept {
    (void)hhk;
    return 1;
}

TL_MSABI void* tl_SetWindowsHookExW(const int idHook, void* const lpfn, void* const hmod, const std::uint32_t dwThreadId) noexcept {
    (void)idHook;
    (void)lpfn;
    (void)hmod;
    (void)dwThreadId;
    return reinterpret_cast<void*>(0x484F4F4BULL);
}

TL_MSABI std::uint32_t tl_GetMenuState(void* const hMenu, const std::uint32_t uId, const std::uint32_t uFlags) noexcept {
    (void)hMenu;
    (void)uId;
    (void)uFlags;
    return 0;
}

TL_MSABI int tl_InsertMenuW(void* const hMenu, const std::uint32_t uPosition, const std::uint32_t uFlags, const std::uintptr_t uIDNewItem, const wchar_t* const lpNewItem) noexcept {
    (void)hMenu;
    (void)uPosition;
    (void)uFlags;
    (void)uIDNewItem;
    (void)lpNewItem;
    return 1;
}

TL_MSABI std::uint32_t tl_GetDlgItemInt(void* const hDlg, const int nIDDlgItem, int* const lpTranslated, const int bSigned) noexcept {
    (void)hDlg;
    (void)nIDDlgItem;
    (void)bSigned;
    if (lpTranslated != nullptr && mapped_guest_range(lpTranslated, sizeof(int), true)) {
        *lpTranslated = 1;
    }
    return 0;
}

TL_MSABI int tl_SetDlgItemInt(void* const hDlg, const int nIDDlgItem, const std::uint32_t uValue, const int bSigned) noexcept {
    (void)hDlg;
    (void)nIDDlgItem;
    (void)uValue;
    (void)bSigned;
    return 1;
}

TL_MSABI void* tl_CreateDialogParamW(void* const hInstance, const wchar_t* const lpTemplateName, void* const hWndParent, void* const lpDialogFunc, const std::intptr_t dwInitParam) noexcept {
    (void)hInstance;
    (void)lpTemplateName;
    (void)hWndParent;
    (void)lpDialogFunc;
    (void)dwInitParam;
    return reinterpret_cast<void*>(0x444C4757ULL);
}

TL_MSABI void* tl_CreateDialogIndirectParamW(void* const hInstance, const void* const lpTemplate, void* const hWndParent, void* const lpDialogFunc, const std::intptr_t dwInitParam) noexcept {
    (void)hInstance;
    (void)lpTemplate;
    (void)hWndParent;
    (void)lpDialogFunc;
    (void)dwInitParam;
    return reinterpret_cast<void*>(0x444C4749ULL);
}

TL_MSABI std::intptr_t tl_DialogBoxIndirectParamW(void* const hInstance, const void* const hDialogTemplate, void* const hWndParent, void* const lpDialogFunc, const std::intptr_t dwInitParam) noexcept {
    (void)hInstance;
    (void)hDialogTemplate;
    (void)hWndParent;
    (void)lpDialogFunc;
    (void)dwInitParam;
    return 1;
}

TL_MSABI void* tl_SetClipboardViewer(void* const hWndNewViewer) noexcept {
    (void)hWndNewViewer;
    return nullptr;
}

TL_MSABI int tl_ChangeClipboardChain(void* const hWndRemove, void* const hWndNewNext) noexcept {
    (void)hWndRemove;
    (void)hWndNewNext;
    return 1;
}

TL_MSABI int tl_DrawTextExW(void* const hdc, wchar_t* const lpchText, const int cchText, void* const lprc, const std::uint32_t format, void* const lpdtp) noexcept {
    (void)format;
    (void)lpdtp;
    return tl_DrawTextW(hdc, reinterpret_cast<const std::uint16_t*>(lpchText), cchText, lprc, format);
}

TL_MSABI int tl_ToAscii(const std::uint32_t uVirtKey, const std::uint32_t uScanCode, const std::uint8_t* const lpKeyState, std::uint16_t* const lpChar, const std::uint32_t uFlags) noexcept {
    return tl_ToAsciiEx(uVirtKey, uScanCode, lpKeyState, lpChar, uFlags, nullptr);
}

TL_MSABI void* tl_CreateAcceleratorTableW(void* const paccel, const int cAccel) noexcept {
    (void)paccel;
    (void)cAccel;
    return reinterpret_cast<void*>(0x4143434CULL);
}

TL_MSABI int tl_DestroyAcceleratorTable(void* const hAccel) noexcept {
    (void)hAccel;
    return 1;
}

TL_MSABI int tl_IsCharLowerW(const wchar_t ch) noexcept {
    return std::iswlower(static_cast<wint_t>(ch)) != 0 ? 1 : 0;
}

TL_MSABI int tl_IsCharAlphaNumericW(const wchar_t ch) noexcept {
    return std::iswalnum(static_cast<wint_t>(ch)) != 0 ? 1 : 0;
}

TL_MSABI int tl_IsCharAlphaW(const wchar_t ch) noexcept {
    return std::iswalpha(static_cast<wint_t>(ch)) != 0 ? 1 : 0;
}

TL_MSABI int tl_ModifyMenuW(void* const hMnu, const std::uint32_t uPosition, const std::uint32_t uFlags, const std::uintptr_t uIDNewItem, const wchar_t* const lpNewItem) noexcept {
    (void)hMnu;
    (void)uPosition;
    (void)uFlags;
    (void)uIDNewItem;
    (void)lpNewItem;
    return 1;
}

TL_MSABI int tl_InflateRect(void* const lprc, const int dx, const int dy) noexcept {
    if (lprc == nullptr || !mapped_guest_range(lprc, 16, true)) return 0;
    auto* const r = reinterpret_cast<std::int32_t*>(lprc);
    r[0] -= dx;
    r[1] -= dy;
    r[2] += dx;
    r[3] += dy;
    return 1;
}

TL_MSABI int tl_IntersectRect(void* const lprcDst, const void* const lprcSrc1, const void* const lprcSrc2) noexcept {
    if (lprcDst == nullptr || lprcSrc1 == nullptr || lprcSrc2 == nullptr ||
        !mapped_guest_range(lprcDst, 16, true) ||
        !mapped_guest_range(lprcSrc1, 16, false) ||
        !mapped_guest_range(lprcSrc2, 16, false)) return 0;
    const auto* const s1 = reinterpret_cast<const std::int32_t*>(lprcSrc1);
    const auto* const s2 = reinterpret_cast<const std::int32_t*>(lprcSrc2);
    auto* const d = reinterpret_cast<std::int32_t*>(lprcDst);
    d[0] = std::max(s1[0], s2[0]);
    d[1] = std::max(s1[1], s2[1]);
    d[2] = std::min(s1[2], s2[2]);
    d[3] = std::min(s1[3], s2[3]);
    if (d[0] >= d[2] || d[1] >= d[3]) {
        std::memset(lprcDst, 0, 16);
        return 0;
    }
    return 1;
}

TL_MSABI int tl_SetRectEmpty(void* const lprc) noexcept {
    if (lprc == nullptr || !mapped_guest_range(lprc, 16, true)) return 0;
    std::memset(lprc, 0, 16);
    return 1;
}

TL_MSABI int tl_EnumChildWindows(void* const hWndParent, void* const lpEnumFunc, const std::intptr_t lParam) noexcept {
    (void)hWndParent;
    (void)lpEnumFunc;
    (void)lParam;
    return 1;
}

TL_MSABI int tl_EnumThreadWindows(const std::uint32_t dwThreadId, void* const lpfn, const std::intptr_t lParam) noexcept {
    (void)dwThreadId;
    (void)lpfn;
    (void)lParam;
    return 1;
}

TL_MSABI int tl_GetMenuBarInfo(void* const hwnd, const std::int32_t idObject, const std::int32_t idItem, void* const pmbi) noexcept {
    (void)hwnd;
    (void)idObject;
    (void)idItem;
    if (pmbi != nullptr && mapped_guest_range(pmbi, 32, true)) {
        std::memset(pmbi, 0, 32);
        *reinterpret_cast<std::uint32_t*>(pmbi) = 32;
    }
    return 1;
}

TL_MSABI int tl_TrackMouseEvent(void* const lpEventTrack) noexcept {
    (void)lpEventTrack;
    return 1;
}

TL_MSABI int tl_GetComboBoxInfo(void* const hwndCombo, void* const pcbi) noexcept {
    (void)hwndCombo;
    if (pcbi != nullptr && mapped_guest_range(pcbi, 64, true)) {
        std::memset(pcbi, 0, 64);
        *reinterpret_cast<std::uint32_t*>(pcbi) = 64;
    }
    return 1;
}

TL_MSABI void* tl_ChildWindowFromPoint(void* const hWndParent, const int x, const int y) noexcept {
    (void)x;
    (void)y;
    return hWndParent;
}

TL_MSABI int tl_GetDlgCtrlID(void* const hWnd) noexcept {
    (void)hWnd;
    return 0;
}

TL_MSABI int tl_wsprintfW(wchar_t* const lpOut, const wchar_t* const lpFmt, ...) noexcept {
    if (lpOut == nullptr || lpFmt == nullptr) return 0;
    std::size_t i = 0;
    while (lpFmt[i] != 0) {
        lpOut[i] = lpFmt[i];
        ++i;
    }
    lpOut[i] = 0;
    return static_cast<int>(i);
}

TL_MSABI void* tl_GetAncestor(void* const hwnd, const std::uint32_t gaFlags) noexcept {
    (void)gaFlags;
    return hwnd;
}

TL_MSABI std::uint32_t tl_GetMenuItemID(void* const hMenu, const int nPos) noexcept {
    (void)hMenu;
    (void)nPos;
    return 0;
}

TL_MSABI int tl_SetLayeredWindowAttributes(void* const hwnd, const std::uint32_t crKey, const std::uint8_t bAlpha, const std::uint32_t dwFlags) noexcept {
    (void)hwnd;
    (void)crKey;
    (void)bAlpha;
    (void)dwFlags;
    return 1;
}

TL_MSABI void* tl_GetLastActivePopup(void* const hWnd) noexcept {
    return hWnd;
}

TL_MSABI int tl_GetMenuStringW(void* const hMenu, const std::uint32_t uIDItem, wchar_t* const lpString, const int cchMax, const std::uint32_t flags) noexcept {
    (void)hMenu;
    (void)uIDItem;
    (void)flags;
    if (lpString != nullptr && cchMax > 0 && mapped_guest_range(lpString, static_cast<std::size_t>(cchMax) * sizeof(wchar_t), true)) {
        lpString[0] = 0;
    }
    return 0;
}

TL_MSABI int tl_LockWindowUpdate(void* const hWndLock) noexcept {
    (void)hWndLock;
    return 1;
}

TL_MSABI void tl_mouse_event(const std::uint32_t dwFlags, const std::uint32_t dx, const std::uint32_t dy, const std::uint32_t dwData, const std::uintptr_t dwExtraInfo) noexcept {
    (void)dwFlags;
    (void)dx;
    (void)dy;
    (void)dwData;
    (void)dwExtraInfo;
}

TL_MSABI int tl_SetMenuItemBitmaps(void* const hMenu, const std::uint32_t uPosition, const std::uint32_t uFlags, void* const hBitmapUnchecked, void* const hBitmapChecked) noexcept {
    (void)hMenu;
    (void)uPosition;
    (void)uFlags;
    (void)hBitmapUnchecked;
    (void)hBitmapChecked;
    return 1;
}

TL_MSABI void* tl_GetDCEx(void* const hWnd, void* const hrgnClip, const std::uint32_t flags) noexcept {
    (void)hrgnClip;
    (void)flags;
    return tl_GetDC(hWnd);
}

TL_MSABI int tl_IsChild(void* const hWndParent, void* const hWnd) noexcept {
    (void)hWndParent;
    (void)hWnd;
    return 0;
}

TL_MSABI void* tl_SetWindowsHookExA(const int id_hook, void* const lpfn, void* const hmod, const std::uint32_t thread_id) noexcept {
    (void)id_hook;
    (void)lpfn;
    (void)hmod;
    (void)thread_id;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x484F4F4BULL); // 'HOOK'
}

TL_MSABI std::intptr_t tl_SendMessageTimeoutA(void* const hwnd, const std::uint32_t msg, const std::uintptr_t w_param, const std::intptr_t l_param, const std::uint32_t flags, const std::uint32_t timeout, std::uintptr_t* const result) noexcept {
    (void)hwnd;
    (void)msg;
    (void)w_param;
    (void)l_param;
    (void)flags;
    (void)timeout;
    if (result != nullptr && mapped_guest_range(result, sizeof(*result), true)) {
        *result = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI void* tl_WindowFromDC(void* const hdc) noexcept {
    (void)hdc;
    if (hdc != nullptr) {
        // HDC == HWND token in our model
        return hdc;
    }
    return reinterpret_cast<void*>(0x57494E44ULL); // 'WIND'
}

TL_MSABI void* tl_FindWindowExA(void* const parent, void* const after, const char* const class_name, const char* const window_name) noexcept {
    (void)parent;
    (void)after;
    (void)class_name;
    (void)window_name;
    set_last_error(abi::kErrorSuccess);
    return nullptr;
}

TL_MSABI int tl_EnumDisplaySettingsA(const char* const device, const std::uint32_t mode, void* const dev_mode) noexcept {
    (void)device;
    if (mode != 0) {
        return 0;
    }
    if (dev_mode != nullptr && mapped_guest_range(dev_mode, 124, true)) {
        std::memset(dev_mode, 0, 124);
        *reinterpret_cast<std::uint32_t*>(dev_mode) = 124;
        // dmPelsWidth/Height at offset 104/108
        *reinterpret_cast<std::uint32_t*>(static_cast<char*>(dev_mode) + 104) = 1920;
        *reinterpret_cast<std::uint32_t*>(static_cast<char*>(dev_mode) + 108) = 1080;
        *reinterpret_cast<std::uint32_t*>(static_cast<char*>(dev_mode) + 112) = 32;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_IsRectEmpty(const void* const rect) noexcept {
    if (rect == nullptr || !mapped_guest_range(rect, 16, false)) {
        return 1;
    }
    const auto* r = static_cast<const std::int32_t*>(rect);
    return (r[2] <= r[0] || r[3] <= r[1]) ? 1 : 0;
}

TL_MSABI int tl_SubtractRect(void* const dest, const void* const src1, const void* const src2) noexcept {
    if (dest == nullptr || src1 == nullptr || src2 == nullptr ||
        !mapped_guest_range(dest, 16, true) || !mapped_guest_range(src1, 16, false) ||
        !mapped_guest_range(src2, 16, false)) {
        return 0;
    }
    const auto* s1 = static_cast<const std::int32_t*>(src1);
    const auto* s2 = static_cast<const std::int32_t*>(src2);
    auto* d = static_cast<std::int32_t*>(dest);
    // Simplificado: dest = src1 - intersecção
    d[0] = s1[0];
    d[1] = s1[1];
    d[2] = s1[2];
    d[3] = s1[3];
    // Se há intersecção, retorna 1
    const int left = std::max(s1[0], s2[0]);
    const int top = std::max(s1[1], s2[1]);
    const int right = std::min(s1[2], s2[2]);
    const int bottom = std::min(s1[3], s2[3]);
    return (left < right && top < bottom) ? 1 : 0;
}

}  // extern "C"

}  // namespace tradutorlinux
