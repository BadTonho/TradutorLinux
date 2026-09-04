#include "user32_internal.hpp"

#include <limits>

namespace tradutorlinux {
extern "C" {

TL_MSABI int tl_GetMessageA(void* const msg, const void* const window,
                            const std::uint32_t filter_min,
                            const std::uint32_t filter_max) noexcept {
    if (!user32_gui_thread_allowed("GetMessageA")) {
        return -1;
    }
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
    if (!user32_gui_thread_allowed("TranslateMessage")) {
        return 0;
    }
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
    if (!user32_gui_thread_allowed("DispatchMessageA")) {
        return 0;
    }
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

TL_MSABI void tl_PostQuitMessage(const int exit_code) noexcept {
    if (!user32_gui_thread_allowed("PostQuitMessage")) {
        return;
    }
    g_quit_code = static_cast<std::uint32_t>(exit_code);
    g_quit_requested = true;
}

TL_MSABI std::uintptr_t tl_SetTimer(const void* const window,
                                    const std::uintptr_t id,
                                    const std::uint32_t elapsed_ms,
                                    const void* const timer_proc) noexcept {
    if (!user32_gui_thread_allowed("SetTimer")) {
        return 0;
    }
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
    if (!user32_gui_thread_allowed("KillTimer")) {
        return 0;
    }
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

TL_MSABI int tl_SendMessageA(const void* window, const std::uint32_t message,
                             const abi::Wparam wparam, const abi::Lparam lparam) noexcept {
    if (!user32_gui_thread_allowed("SendMessageA")) {
        return 0;
    }
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (slot->is_control) {
        if (message == abi::kWmSetFont) {
            return 0;
        }
        if (slot->control_kind == ControlKind::Toolbar) {
            constexpr std::uint32_t kMaxToolbarButtons = 128U;
            constexpr std::uint32_t kMaxToolbarStructSize = 64U;

            if (message == abi::kTbButtonStructSize) {
                if (wparam < sizeof(std::int32_t) * 2U || wparam > kMaxToolbarStructSize) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                slot->toolbar_button_struct_size = static_cast<std::uint32_t>(wparam);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (message == abi::kTbSetButtonSize || message == abi::kTbSetBitmapSize) {
                const std::uint32_t packed = static_cast<std::uint32_t>(lparam);
                const int width = static_cast<int>(packed & 0xFFFFU);
                const int height = static_cast<int>((packed >> 16U) & 0xFFFFU);
                if (width == 0 || height == 0 || width > kMaxGuestWindowDimension ||
                    height > kMaxGuestWindowDimension) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                slot->toolbar_button_width = width;
                if (message == abi::kTbSetButtonSize) {
                    slot->height = height;
                }
                if (slot->parent != nullptr) {
                    render_controls(*slot->parent);
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (message == abi::kTbAddButtons) {
                const std::size_t count = static_cast<std::size_t>(wparam);
                const std::size_t struct_size = slot->toolbar_button_struct_size == 0U
                                                    ? 32U
                                                    : slot->toolbar_button_struct_size;
                if (count == 0U) {
                    set_last_error(abi::kErrorSuccess);
                    return 1;
                }
                if (count > kMaxToolbarButtons || count > kMaxToolbarButtons - slot->toolbar_buttons.size() ||
                    lparam == 0 || struct_size < sizeof(std::int32_t) * 2U ||
                    struct_size > kMaxToolbarStructSize ||
                    count > std::numeric_limits<std::size_t>::max() / struct_size ||
                    !mapped_guest_range(reinterpret_cast<const void*>(lparam), count * struct_size,
                                        false)) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                slot->toolbar_button_struct_size = static_cast<std::uint32_t>(struct_size);
                const auto* const bytes = reinterpret_cast<const std::byte*>(lparam);
                for (std::size_t index = 0; index < count; ++index) {
                    std::int32_t command_id = 0;
                    std::memcpy(&command_id, bytes + index * struct_size + sizeof(std::int32_t),
                                sizeof(command_id));
                    slot->toolbar_buttons.push_back(ToolbarButton{command_id});
                }
                if (slot->parent != nullptr) {
                    render_controls(*slot->parent);
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (message == abi::kTbButtonCount) {
                set_last_error(abi::kErrorSuccess);
                return static_cast<int>(slot->toolbar_buttons.size());
            }
            if (message == abi::kTbDeleteButton) {
                const std::size_t index = static_cast<std::size_t>(wparam);
                if (index >= slot->toolbar_buttons.size()) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                slot->toolbar_buttons.erase(
                    slot->toolbar_buttons.begin() +
                    static_cast<std::vector<ToolbarButton>::difference_type>(index));
                if (slot->parent != nullptr) {
                    render_controls(*slot->parent);
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (message == abi::kTbAutoSize || message == abi::kTbSetImageList ||
                message == abi::kTbEnableButton) {
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        if (slot->control_kind == ControlKind::StatusBar) {
            if (message == abi::kSbSetTextA || message == abi::kSbSetTextW) {
                if (lparam == 0) {
                    slot->text.clear();
                } else if (!mapped_guest_cstring(reinterpret_cast<const char*>(lparam))) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                } else {
                    slot->text = reinterpret_cast<const char*>(lparam);
                }
                if (slot->parent != nullptr) {
                    render_controls(*slot->parent);
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (message == abi::kSbSetParts) {
                const std::size_t count = static_cast<std::size_t>(wparam);
                if (count > 128U || (count > 0U &&
                                     (lparam == 0 || !mapped_guest_range(
                                                           reinterpret_cast<const void*>(lparam),
                                                           count * sizeof(std::int32_t), false)))) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (message == abi::kSbSetMinHeight) {
                if (wparam == 0U || wparam > kMaxGuestWindowDimension) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                slot->height = static_cast<int>(wparam);
                if (slot->parent != nullptr) {
                    render_controls(*slot->parent);
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (message == abi::kSbSimple) {
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
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
        if (slot->control_kind == ControlKind::StatusBar && message == abi::kSbSetTextW) {
            if (lparam == 0) {
                return tl_SendMessageA(window, abi::kSbSetTextA, wparam, 0);
            }
            if (!mapped_guest_wstring(reinterpret_cast<const std::uint16_t*>(lparam))) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
            const std::string utf8 =
                util::wide_to_utf8(reinterpret_cast<const std::uint16_t*>(lparam));
            return tl_SendMessageA(window, abi::kSbSetTextA, wparam,
                                   reinterpret_cast<abi::Lparam>(utf8.c_str()));
        }
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

TL_MSABI int tl_PostMessageA(const void* window, const std::uint32_t message,
                             const abi::Wparam wparam, const abi::Lparam lparam) noexcept {
    if (!user32_gui_thread_allowed("PostMessageA")) {
        return 0;
    }
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

TL_MSABI std::uint32_t tl_MsgWaitForMultipleObjectsEx(const std::uint32_t count,
                                                      const void* const* const handles,
                                                      const std::uint32_t milliseconds,
                                                      const std::uint32_t wake_mask,
                                                      const std::uint32_t flags) noexcept {
    if (!user32_gui_thread_allowed("MsgWaitForMultipleObjectsEx")) {
        return abi::kWaitFailed;
    }
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

TL_MSABI std::int16_t tl_GetKeyState(const int) noexcept {
    return 0;
}

TL_MSABI std::int16_t tl_GetAsyncKeyState(const int) noexcept {
    return 0;
}

TL_MSABI int tl_PeekMessageA(void* const msg, const void* const window,
                             const std::uint32_t filter_min, const std::uint32_t filter_max,
                             const std::uint32_t remove_msg) noexcept {
    if (!user32_gui_thread_allowed("PeekMessageA")) {
        return 0;
    }
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

TL_MSABI std::uint32_t tl_WaitForInputIdle(void* const process, const std::uint32_t milliseconds) noexcept {
    (void)process;
    (void)milliseconds;
    set_last_error(abi::kErrorSuccess);
    return 0; // WAIT_OBJECT_0
}

TL_MSABI int tl_MessageBeep(const std::uint32_t type) noexcept {
    (void)type;
    return 1;
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

TL_MSABI std::uint32_t tl_RegisterWindowMessageA(const char* const string) noexcept {
    (void)string;
    set_last_error(abi::kErrorSuccess);
    return 0xC001U;
}

TL_MSABI int tl_SetKeyboardState(const std::uint8_t* const key_states) noexcept {
    (void)key_states;
    set_last_error(abi::kErrorSuccess);
    return 1;
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

TL_MSABI void tl_NotifyWinEvent(const std::uint32_t event, void* const hwnd, const std::int32_t idObject, const std::int32_t idChild) noexcept {
    (void)event;
    (void)hwnd;
    (void)idObject;
    (void)idChild;
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

TL_MSABI int tl_ToAscii(const std::uint32_t uVirtKey, const std::uint32_t uScanCode, const std::uint8_t* const lpKeyState, std::uint16_t* const lpChar, const std::uint32_t uFlags) noexcept {
    return tl_ToAsciiEx(uVirtKey, uScanCode, lpKeyState, lpChar, uFlags, nullptr);
}

TL_MSABI int tl_TrackMouseEvent(void* const lpEventTrack) noexcept {
    (void)lpEventTrack;
    return 1;
}

TL_MSABI void tl_mouse_event(const std::uint32_t dwFlags, const std::uint32_t dx, const std::uint32_t dy, const std::uint32_t dwData, const std::uintptr_t dwExtraInfo) noexcept {
    (void)dwFlags;
    (void)dx;
    (void)dy;
    (void)dwData;
    (void)dwExtraInfo;
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

}  // extern "C"
}  // namespace tradutorlinux
