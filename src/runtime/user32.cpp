#include "tradutorlinux/runtime/winapi.hpp"
#include "runtime_context.hpp"

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
    const std::uint32_t result = gui::message_box(text, caption);
    set_last_error(result == 0 ? abi::kErrorAccessDenied : abi::kErrorSuccess);
    return result;
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

TL_MSABI abi::HWnd tl_CreateWindowExA(const std::uint32_t,
                                      const char* const class_name, const char* const window_name,
                                      const std::uint32_t, const int x, const int y,
                                      const int width, const int height, const void* const parent,
                                      const void* const menu, const void* const,
                                      const void* const) noexcept {
    if (!mapped_guest_cstring(class_name) || class_name == nullptr ||
        (window_name != nullptr && !mapped_guest_cstring(window_name))) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateWindowExA", "strings", "nome de classe ou janela inválido");
        return nullptr;
    }
    ClassSlot* const cls = find_class_slot(class_name);
    if (cls == nullptr && !runtime_gui::is_builtin_control(class_name)) {
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
    if (runtime_gui::is_builtin_control(class_name)) {
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
        slot.control_kind = runtime_gui::control_kind_for(class_name);
        slot.parent = parent_slot;
        slot.control_id = reinterpret_cast<std::uintptr_t>(menu);
        slot.x = x;
        slot.y = y;
        slot.width = width > 0 ? width : 1;
        slot.height = height > 0 ? height : 1;
        slot.text = window_name != nullptr ? window_name : "";
        slot.visible = true;
        slot.enabled = true;
        slot.combo_selection = -1;
        set_last_error(abi::kErrorSuccess);
        return &slot;
    }
    const char* const caption = window_name != nullptr ? window_name : cls->name.c_str();
    gui::NativeWindow native = gui::create_window(caption, width, height);
    if (native == nullptr) {
        set_last_error(abi::kErrorAccessDenied);
        trace_guest_failure("CreateWindowExA", "x11", "falha ao criar janela X11");
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
    slot.width = width;
    slot.height = height;
    const abi::Lresult create_result = call_wndproc(slot.wndproc, &slot, abi::kWmCreate, 0, 0);
    if (create_result == -1) {
        gui::destroy_window(slot.native);
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

TL_MSABI int tl_ShowWindow(const void* const window, const int cmd_show) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("ShowWindow", "handle-validation", "handle inválido");
        return 0;
    }
    const bool was_visible = slot->visible;
    if (cmd_show == 0) {
        slot->visible = false;
    } else if (slot->native != nullptr) {
        slot->mapped = gui::map_window(slot->native);
        slot->visible = true;
    } else {
        slot->visible = true;
    }
    if (slot->is_control && slot->parent != nullptr) {
        render_controls(*slot->parent);
    }
    set_last_error(abi::kErrorSuccess);
    return was_visible ? 1 : 0;
}

TL_MSABI int tl_UpdateWindow(const void* const window) noexcept {
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
            const gui::WindowEvent event = gui::next_window_event(slot.native);
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
                    set_last_error(abi::kErrorSuccess);
                    return 1;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
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

TL_MSABI int tl_DestroyWindow(const void* const window) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (slot->native != nullptr) {
        gui::destroy_window(slot->native);
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
    return 0;
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
        gui::flush_window(slot->native);
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

TL_MSABI std::uintptr_t tl_LoadCursorA(const void* instance, const char* name) noexcept {
    (void)instance;
    (void)name;
    return 1;
}

TL_MSABI std::uintptr_t tl_LoadIconA(const void* instance, const char* name) noexcept {
    (void)instance;
    (void)name;
    return 1;
}

TL_MSABI std::intptr_t tl_SetClassLongPtrA(const void* window, int index,
                                            std::intptr_t value) noexcept {
    (void)window;
    (void)index;
    (void)value;
    return 0;
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
    const std::uint32_t command = gui::track_popup_menu(it->items, x, y);
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
        case -16: return 0x10000000 | 0x00C00000; // GWL_STYLE (WS_VISIBLE | WS_CAPTION)
        case -20: return 0; // GWL_EXSTYLE
        case -21: return reinterpret_cast<std::intptr_t>(slot->user_data); // GWLP_USERDATA
        default: return 0;
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
        default:
            return 0;
    }
}

TL_MSABI std::intptr_t tl_SetWindowLongPtrW(const void* window, const int index, const std::intptr_t new_long) noexcept {
    return tl_SetWindowLongPtrA(window, index, new_long);
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
    const std::uint32_t result = gui::message_box(utf8_text.c_str(), utf8_cap.c_str());
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
    (void)instance;
    (void)id;
    if (buffer == nullptr || buffer_max <= 0 || !mapped_guest_range(buffer, static_cast<std::size_t>(buffer_max), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    buffer[0] = '\0';
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI int tl_LoadStringW(void* instance, const std::uint32_t id, std::uint16_t* buffer, const int buffer_max) noexcept {
    (void)instance;
    (void)id;
    if (buffer == nullptr || buffer_max <= 0 || !mapped_guest_range(buffer, static_cast<std::size_t>(buffer_max) * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    buffer[0] = 0;
    set_last_error(abi::kErrorSuccess);
    return 0;
}

}  // extern "C"

}  // namespace tradutorlinux
