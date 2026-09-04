#include "user32_internal.hpp"
namespace tradutorlinux {

namespace {

thread_local bool g_create_window_w_bridge = false;

}  // namespace

void paint_registered_children(WindowSlot& parent) noexcept {
    for (WindowSlot& child : g_windows) {
        if (!child.used || !child.is_control || child.parent != &parent || child.wndproc == 0 ||
            !child.visible) {
            continue;
        }
        const std::array<diagnostics::TraceField, 4> begin_fields{
            diagnostics::TraceField{"symbol", "CreateWindowExA"},
            diagnostics::TraceField{"stage", "WM_PAINT-child-begin"},
            diagnostics::TraceField{"class", child.class_name},
            diagnostics::TraceField{"status", "dispatch"},
        };
        runtime_trace("CreateWindowExA", begin_fields, 4);
        (void)call_wndproc(child.wndproc, &child, abi::kWmPaint, 0, 0);
        const std::array<diagnostics::TraceField, 4> end_fields{
            diagnostics::TraceField{"symbol", "CreateWindowExA"},
            diagnostics::TraceField{"stage", "WM_PAINT-child-end"},
            diagnostics::TraceField{"class", child.class_name},
            diagnostics::TraceField{"status", "success"},
        };
        runtime_trace("CreateWindowExA", end_fields, 4);
    }
}

extern "C" {

TL_MSABI abi::Atom tl_RegisterClassExA(const void* const wnd_class) noexcept {
    if (!user32_gui_thread_allowed("RegisterClassExA")) {
        return 0;
    }
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
    slot.menu_name_raw = reinterpret_cast<std::uintptr_t>(wc->menu_name) <= 0xFFFFU
                             ? reinterpret_cast<std::uintptr_t>(wc->menu_name)
                             : 0;
    if (wc->menu_name != nullptr && slot.menu_name_raw == 0) {
        if (!mapped_guest_cstring(wc->menu_name)) {
            slot = {};
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        slot.menu_name_text = util::utf8_to_wide(wc->menu_name);
    }
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
    if (!user32_gui_thread_allowed("RegisterClassExW")) {
        return 0;
    }
    if (wnd_class == nullptr ||
        !mapped_guest_range(wnd_class, sizeof(abi::GuestWndClassExW), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("RegisterClassExW", "wnd-class", "estrutura WNDCLASSEXW inválida");
        return 0;
    }
    const auto* const wc = static_cast<const abi::GuestWndClassExW*>(wnd_class);
    if (wc->cb_size < sizeof(abi::GuestWndClassExW) || wc->window_proc == 0 ||
        wc->class_name == nullptr || !mapped_guest_wstring(wc->class_name) ||
        (wc->menu_name != nullptr && !guest_resource_or_wstring_valid(wc->menu_name)) ||
        !mapped_guest_range(std::bit_cast<const void*>(wc->window_proc), 1, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("RegisterClassExW", "wnd-class", "cbSize, window_proc ou class_name inválido");
        return 0;
    }
    std::string utf8_class = util::wide_to_utf8(wc->class_name);
    std::string utf8_menu;
    const char* menu_cstr = nullptr;
    if (wc->menu_name != nullptr && reinterpret_cast<std::uintptr_t>(wc->menu_name) > 0xFFFFU) {
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
    const abi::Atom atom = tl_RegisterClassExA(&exA);
    if (atom != 0) {
        if (ClassSlot* const slot = find_class_slot(utf8_class.c_str()); slot != nullptr) {
            const std::uintptr_t raw = reinterpret_cast<std::uintptr_t>(wc->menu_name);
            slot->menu_name_raw = raw <= 0xFFFFU ? raw : 0;
            if (raw > 0xFFFFU) {
                const auto* const menu_text = reinterpret_cast<const char16_t*>(wc->menu_name);
                slot->menu_name_text.assign(menu_text, std::char_traits<char16_t>::length(menu_text));
            } else {
                slot->menu_name_text.clear();
            }
        }
    }
    return atom;
}

TL_MSABI abi::Atom tl_RegisterClassW(const void* wnd_class) noexcept {
    if (!user32_gui_thread_allowed("RegisterClassW")) {
        return 0;
    }
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
    const std::uintptr_t menu_raw = reinterpret_cast<std::uintptr_t>(wc->menu_name);
    slot.menu_name_raw = menu_raw <= 0xFFFFU ? menu_raw : 0;
    if (menu_raw > 0xFFFFU) {
        const auto* const menu_text = reinterpret_cast<const char16_t*>(wc->menu_name);
        slot.menu_name_text.assign(menu_text, std::char_traits<char16_t>::length(menu_text));
    }
    slot.atom = static_cast<abi::Atom>(static_cast<std::size_t>(free_it - g_classes.begin()) + 1U);
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "RegisterClassW"},
        diagnostics::TraceField{"class", slot.name},
        diagnostics::TraceField{"menu-resource", std::to_string(slot.menu_name_raw)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("RegisterClassW", fields, 4);
    return slot.atom;
}

TL_MSABI abi::HWnd tl_CreateWindowExA(const std::uint32_t ex_style,
                                      const char* const class_name, const char* const window_name,
                                      const std::uint32_t style, const int x, const int y,
                                      const int width, const int height, const void* const parent,
                                      const void* const menu, const void* const instance,
                                      const void* const param) noexcept {
    if (!user32_gui_thread_allowed("CreateWindowExA")) {
        return nullptr;
    }
    (void)ex_style;
    (void)instance;
    (void)param;
    const auto class_val = reinterpret_cast<std::uintptr_t>(class_name);
    const bool normalize_geometry =
        guest_window_geometry_is_unreasonable(x, y, width, height);
    const int resolved_width = normalize_geometry ? 800 : guest_window_dimension(width, 800);
    const int resolved_height = normalize_geometry ? 600 : guest_window_dimension(height, 600);
    const int resolved_x = normalize_geometry ? 0 : x;
    const int resolved_y = normalize_geometry ? 0 : y;
    const bool strings_from_bridge = g_create_window_w_bridge;
    if (class_name == nullptr ||
        (class_val > 0xFFFFU && !strings_from_bridge && !mapped_guest_cstring(class_name)) ||
        (window_name != nullptr && !strings_from_bridge && !mapped_guest_cstring(window_name))) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateWindowExA", "strings", "nome de classe ou janela inválido");
        return nullptr;
    }
    ClassSlot* const cls = find_class_slot(class_name);
    const bool generic_child = cls == nullptr && class_val > 0xFFFFU && parent != nullptr;
    const bool registered_child = cls != nullptr && parent != nullptr;
    if (cls == nullptr && (class_val <= 0xFFFFU ||
                           (!runtime_gui::is_builtin_control(class_name) && !generic_child))) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateWindowExA", "class-lookup", "classe não registrada");
        return nullptr;
    }
    g_create_window_w_bridge = false;
    const auto free_it = std::find_if(g_windows.begin(), g_windows.end(),
                                      [](const WindowSlot& slot) { return !slot.used; });
    if (free_it == g_windows.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
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
    if (class_val > 0xFFFFU &&
        (runtime_gui::is_builtin_control(class_name) || generic_child || registered_child)) {
        WindowSlot& slot = *free_it;
        WindowSlot* parent_slot = find_window_slot(parent);
        if (parent_slot == nullptr || parent_slot->is_control) {
            set_last_error(abi::kErrorInvalidHandle);
            return nullptr;
        }
        slot = {};
        slot.used = true;
        slot.wndproc = cls != nullptr ? cls->wndproc : 0;
        slot.class_name = class_name;
        slot.window_title = window_name != nullptr ? window_name : "";
        slot.native = registered_child ? parent_slot->native : nullptr;
        slot.mapped = registered_child ? parent_slot->mapped : false;
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
        if (registered_child) {
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
            cs.lpszName = window_name;
            cs.lpszClass = class_name;
            cs.dwExStyle = ex_style;
            const abi::Lresult create_result = call_wndproc(
                slot.wndproc, &slot, abi::kWmCreate, 0,
                reinterpret_cast<abi::Lparam>(&cs));
            if (create_result == -1) {
                slot = {};
                set_last_error(abi::kErrorInvalidParameter);
                trace_guest_failure("CreateWindowExA", "wm-create",
                                    "WM_CREATE do filho rejeitou a criação");
                return nullptr;
            }
        }
        if (generic_child) {
            const std::array<diagnostics::TraceField, 4> fields{
                diagnostics::TraceField{"class", class_name},
                diagnostics::TraceField{"status", "generic-child"},
                diagnostics::TraceField{"position", std::to_string(slot.x) + "," +
                                             std::to_string(slot.y)},
                diagnostics::TraceField{"size", std::to_string(slot.width) + "x" +
                                             std::to_string(slot.height)}};
            runtime_trace("CreateWindowExA", fields, 4);
        } else if (registered_child) {
            const std::array<diagnostics::TraceField, 4> fields{
                diagnostics::TraceField{"class", class_name},
                diagnostics::TraceField{"status", "registered-child"},
                diagnostics::TraceField{"position", std::to_string(slot.x) + "," +
                                             std::to_string(slot.y)},
                diagnostics::TraceField{"size", std::to_string(slot.width) + "x" +
                                             std::to_string(slot.height)}};
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
    slot.x = resolved_x;
    slot.y = resolved_y;
    slot.width = resolved_width;
    slot.height = resolved_height;
    slot.style = style;
    slot.visible = (style & kWsVisible) != 0U;
    if (parent == nullptr) {
        slot.menu_handle = menu;
        if (slot.menu_handle == nullptr && cls != nullptr) {
            const auto* menu_name = reinterpret_cast<const std::uint16_t*>(cls->menu_name_raw);
            if (cls->menu_name_raw > 0xFFFFU) {
                menu_name = reinterpret_cast<const std::uint16_t*>(cls->menu_name_text.c_str());
            }
            if (menu_name != nullptr) {
                slot.menu_handle = tl_LoadMenuW(nullptr, menu_name);
                const std::array<diagnostics::TraceField, 4> menu_fields{
                    diagnostics::TraceField{"symbol", "CreateWindowExA"},
                    diagnostics::TraceField{"status", slot.menu_handle != nullptr ? "menu-loaded" : "menu-not-found"},
                    diagnostics::TraceField{"menu-resource", std::to_string(cls->menu_name_raw)},
                    diagnostics::TraceField{"menu-handle", std::to_string(
                        reinterpret_cast<std::uintptr_t>(slot.menu_handle))},
                };
                runtime_trace("CreateWindowExA", menu_fields, 4);
            }
        }
    }
    if (normalize_geometry) {
        const std::array<diagnostics::TraceField, 4> fields{
            diagnostics::TraceField{"symbol", "CreateWindowExA"},
            diagnostics::TraceField{"status", "geometry-normalized"},
            diagnostics::TraceField{"requested-position",
                                    std::to_string(x) + "," + std::to_string(y)},
            diagnostics::TraceField{"requested-size",
                                    std::to_string(width) + "x" + std::to_string(height)},
        };
        runtime_trace("CreateWindowExA", fields, 4);
    }
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
    GuestCreateStructA cs{};
    cs.lpCreateParams = param;
    cs.hInstance = instance;
    cs.hMenu = menu;
    cs.hwndParent = parent;
    cs.cy = slot.height;
    cs.cx = slot.width;
    cs.y = resolved_y;
    cs.x = resolved_x;
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
        const bool previous_bridge = g_create_window_w_bridge;
        g_create_window_w_bridge = true;
        abi::HWnd result = tl_CreateWindowExA(ex_style, reinterpret_cast<const char*>(class_val), win_cstr,
                                              style, x, y, width, height, parent, menu, instance, param);
        g_create_window_w_bridge = previous_bridge;
        return result;
    }
    const std::string utf8_class = util::wide_to_utf8(class_name);
    const bool previous_bridge = g_create_window_w_bridge;
    g_create_window_w_bridge = true;
    abi::HWnd result = tl_CreateWindowExA(ex_style, utf8_class.c_str(), win_cstr, style, x, y, width,
                                          height, parent, menu, instance, param);
    g_create_window_w_bridge = previous_bridge;
    return result;
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
    if (!user32_gui_thread_allowed("DestroyWindow")) {
        return 0;
    }
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
    if (slot->native != nullptr && !slot->is_control) {
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
    if (!user32_gui_thread_allowed("SetFocus")) {
        return nullptr;
    }
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

TL_MSABI const void* tl_FindWindowA(const char* class_name, const char* window_name) noexcept {
    if (!user32_gui_thread_allowed("FindWindowA")) {
        return nullptr;
    }
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
    if (!user32_gui_thread_allowed("FindWindowW")) {
        return nullptr;
    }
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

TL_MSABI void* tl_GetWindowDC(const void* window) noexcept {
    return tl_GetDC(window);
}

TL_MSABI void* tl_GetDesktopWindow() noexcept {
    return kDesktopHwndToken;
}

TL_MSABI void* tl_GetFocus() noexcept {
    if (!user32_gui_thread_allowed("GetFocus")) {
        return nullptr;
    }
    return g_focused_control != nullptr ? g_focused_control : nullptr;
}

TL_MSABI void* tl_SetCapture(const void* const window) noexcept {
    if (!user32_gui_thread_allowed("SetCapture")) {
        return nullptr;
    }
    void* const prev = g_captured_window;
    if (window == nullptr) {
        g_captured_window = nullptr;
    } else if (WindowSlot* const slot = find_window_slot(window); slot != nullptr) {
        g_captured_window = slot;
    }
    return prev;
}

TL_MSABI int tl_ReleaseCapture() noexcept {
    if (!user32_gui_thread_allowed("ReleaseCapture")) {
        return 0;
    }
    g_captured_window = nullptr;
    return 1;
}

TL_MSABI void* tl_GetCapture() noexcept {
    if (!user32_gui_thread_allowed("GetCapture")) {
        return nullptr;
    }
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
    if (!user32_gui_thread_allowed("GetWindow")) {
        return nullptr;
    }
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
    if (!user32_gui_thread_allowed("GetClassInfoW") || class_name == nullptr ||
        !mapped_guest_wstring(class_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string utf8_class = util::wide_to_utf8(class_name);
    ClassSlot* const cls = find_class_slot(utf8_class.c_str());
    if (cls == nullptr) {
        set_last_error(abi::kErrorClassDoesNotExist);
        const std::array<diagnostics::TraceField, 4> fields{
            diagnostics::TraceField{"symbol", "GetClassInfoW"},
            diagnostics::TraceField{"class", utf8_class},
            diagnostics::TraceField{"status", "not-found"},
            diagnostics::TraceField{"error", std::to_string(abi::kErrorClassDoesNotExist)},
        };
        runtime_trace("GetClassInfoW", fields, 4);
        return 0;
    }
    if (wnd_class == nullptr || !mapped_guest_range(wnd_class, sizeof(abi::GuestWndClassW), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("GetClassInfoW", "wnd-class", "estrutura WNDCLASSW inválida");
        return 0;
    }
    auto* const output = static_cast<abi::GuestWndClassW*>(wnd_class);
    *output = {};
    output->window_proc = cls->wndproc;
    output->class_name = class_name;
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "GetClassInfoW"},
        diagnostics::TraceField{"class", cls->name},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"mechanism", "class-table"},
    };
    runtime_trace("GetClassInfoW", fields, 4);
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

TL_MSABI std::uint32_t tl_GetDpiForWindow(void* const hwnd) noexcept {
    (void)hwnd;
    return 96; // Standard 96 DPI (100% scaling)
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

TL_MSABI int tl_DrawFocusRect(void* const hdc, const void* const rect) noexcept {
    (void)hdc;
    (void)rect;
    set_last_error(abi::kErrorSuccess);
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

TL_MSABI void* tl_GetProcessWindowStation() noexcept {
    return reinterpret_cast<void*>(0x57535441ULL); // 'WSTA'
}

TL_MSABI void* tl_GetShellWindow() noexcept {
    return reinterpret_cast<void*>(0x53484C4CULL); // 'SHLL'
}

TL_MSABI void* tl_GetForegroundWindow() noexcept {
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x57494E31ULL);
}

TL_MSABI int tl_IsIconic(void* const hwnd) noexcept {
    (void)hwnd;
    return 0;
}

TL_MSABI void* tl_SetActiveWindow(void* const hwnd) noexcept {
    set_last_error(abi::kErrorSuccess);
    return hwnd;
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

TL_MSABI int tl_AdjustWindowRectEx(void* const lpRect, const std::uint32_t dwStyle, const int bMenu, const std::uint32_t dwExStyle) noexcept {
    (void)lpRect;
    (void)dwStyle;
    (void)bMenu;
    (void)dwExStyle;
    return 1;
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

TL_MSABI void* tl_ChildWindowFromPoint(void* const hWndParent, const int x, const int y) noexcept {
    (void)x;
    (void)y;
    return hWndParent;
}

TL_MSABI void* tl_GetAncestor(void* const hwnd, const std::uint32_t gaFlags) noexcept {
    (void)gaFlags;
    return hwnd;
}

TL_MSABI int tl_SetLayeredWindowAttributes(void* const hwnd, const std::uint32_t crKey, const std::uint8_t bAlpha, const std::uint32_t dwFlags) noexcept {
    (void)hwnd;
    (void)crKey;
    (void)bAlpha;
    (void)dwFlags;
    return 1;
}

TL_MSABI int tl_LockWindowUpdate(void* const hWndLock) noexcept {
    (void)hWndLock;
    return 1;
}

TL_MSABI int tl_IsChild(void* const hWndParent, void* const hWnd) noexcept {
    (void)hWndParent;
    (void)hWnd;
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

}  // extern "C"
}  // namespace tradutorlinux
