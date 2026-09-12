#include "user32_internal.hpp"
namespace tradutorlinux {

namespace {

[[nodiscard]] WindowSlot* create_modeless_dialog(const void* const instance,
                                                 const std::uint16_t* const template_name,
                                                 const void* const parent,
                                                 const std::uintptr_t dialog_proc,
                                                 const abi::Lparam init_param,
                                                 const char* const trace_symbol) noexcept {
    const auto trace_failure = [trace_symbol](const char* const stage,
                                              const char* const detail) noexcept {
        const std::array<diagnostics::TraceField, 4> fields{
            diagnostics::TraceField{"symbol", trace_symbol},
            diagnostics::TraceField{"stage", stage},
            diagnostics::TraceField{"detail", detail},
            diagnostics::TraceField{"status", "failure"},
        };
        runtime_trace(trace_symbol, fields, 4);
    };
    if (!user32_gui_thread_allowed(trace_symbol)) {
        trace_failure("thread-policy", "GUI thread não permitido");
        return nullptr;
    }
    if ((instance != nullptr && reinterpret_cast<std::uintptr_t>(instance) != 0x1000U &&
         reinterpret_cast<std::uintptr_t>(instance) !=
         reinterpret_cast<std::uintptr_t>(g_guest_image_base)) ||
        template_name == nullptr || !guest_callback_address_valid(dialog_proc)) {
        trace_failure("arguments", "instância, template ou callback inválido");
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    WindowSlot* parent_slot = nullptr;
    if (parent != nullptr) {
        parent_slot = find_window_slot(parent);
        if (parent_slot == nullptr || parent_slot->is_control) {
            trace_failure("parent", "janela pai inválida");
            set_last_error(abi::kErrorInvalidHandle);
            return nullptr;
        }
    }

    const auto* const resource_type = reinterpret_cast<const std::uint16_t*>(5U);
    void* const resource = tl_FindResourceW(nullptr, template_name, resource_type);
    void* const loaded = resource == nullptr ? nullptr : tl_LoadResource(nullptr, resource);
    const std::uint32_t resource_size = loaded == nullptr ? 0 : tl_SizeofResource(nullptr, resource);
    const void* const resource_data = loaded == nullptr ? nullptr : tl_LockResource(loaded);
    if (resource_data == nullptr || resource_size == 0) {
        trace_failure("resource", "recurso de diálogo ausente");
        set_last_error(abi::kErrorResourceNotFound);
        return nullptr;
    }
    runtime::DialogTemplate parsed{};
    const auto status = runtime::parse_dialog_template(
        std::span<const std::byte>{static_cast<const std::byte*>(resource_data), resource_size}, parsed);
    if (status != runtime::DialogTemplateStatus::Success) {
        const char* status_name = "unknown";
        switch (status) {
            case runtime::DialogTemplateStatus::Malformed: status_name = "malformed"; break;
            case runtime::DialogTemplateStatus::Unsupported: status_name = "unsupported"; break;
            case runtime::DialogTemplateStatus::DialogEx: status_name = "dialog-ex"; break;
            case runtime::DialogTemplateStatus::Success: status_name = "success"; break;
        }
        trace_failure("template", status_name);
        set_last_error(status == runtime::DialogTemplateStatus::DialogEx
                           ? abi::kErrorNotSupported
                           : abi::kErrorInvalidParameter);
        return nullptr;
    }

    const std::string title = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(parsed.title.c_str()));
    gui::NativeWindow native = gui::platform::create_window(title.c_str(), parsed.width, parsed.height);
    if (native == nullptr) {
        trace_failure("window", "janela nativa não criada");
        set_last_error(abi::kErrorAccessDenied);
        return nullptr;
    }
    const auto free_it = std::find_if(g_windows.begin(), g_windows.end(),
                                      [](const WindowSlot& slot) { return !slot.used; });
    if (free_it == g_windows.end()) {
        gui::platform::destroy_window(native);
        trace_failure("window-pool", "pool de janelas esgotado");
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
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
    static_cast<void>(register_window_handle(&dialog));
    for (const runtime::DialogControl& item : parsed.controls) {
        const auto child_it = std::find_if(g_windows.begin(), g_windows.end(),
                                           [](const WindowSlot& slot) { return !slot.used; });
        if (child_it == g_windows.end()) {
            tl_DestroyWindow(&dialog);
            trace_failure("control-pool", "pool de controles esgotado");
            set_last_error(abi::kErrorNotEnoughMemory);
            return nullptr;
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
        child.class_name = util::wide_to_utf8(
            reinterpret_cast<const std::uint16_t*>(item.class_name.c_str()));
        child.text = util::wide_to_utf8(reinterpret_cast<const std::uint16_t*>(item.title.c_str()));
        switch (item.control_class) {
            case runtime::DialogControlClass::Button:
                child.class_name = "BUTTON";
                child.control_kind = ControlKind::Button;
                break;
            case runtime::DialogControlClass::Edit:
                child.class_name = "EDIT";
                child.control_kind = ControlKind::Edit;
                break;
            case runtime::DialogControlClass::Static:
                child.class_name = "STATIC";
                child.control_kind = ControlKind::Static;
                break;
            case runtime::DialogControlClass::ComboBox:
                child.control_kind = ControlKind::ComboBox;
                break;
            case runtime::DialogControlClass::Generic:
                child.control_kind = runtime_gui::is_builtin_control(child.class_name.c_str())
                                         ? runtime_gui::control_kind_for(child.class_name.c_str())
                                         : ControlKind::Generic;
                break;
        }
        static_cast<void>(register_window_handle(&child));
        dialog.dialog_children.push_back(&child);
    }
    render_controls(dialog);
    if (next_dialog_tab_item(dialog, nullptr, false) != nullptr) {
        set_focus_control(next_dialog_tab_item(dialog, nullptr, false));
    }
    static_cast<void>(call_wndproc(dialog.wndproc, &dialog, 0x0110U, 0, init_param)); // WM_INITDIALOG
    flush_dialog_render();
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", trace_symbol},
        diagnostics::TraceField{"template", std::to_string(reinterpret_cast<std::uintptr_t>(template_name))},
        diagnostics::TraceField{"controls", std::to_string(parsed.controls.size())},
        diagnostics::TraceField{"status", "created"},
    };
    runtime_trace(trace_symbol, fields, 4);
    set_last_error(abi::kErrorSuccess);
    return &dialog;
}

}  // namespace

extern "C" {

TL_MSABI int tl_MessageBoxA(const void* const window, const char* const text,
                            const char* const caption, const std::uint32_t type) noexcept {
    if (!user32_gui_thread_allowed("MessageBoxA")) {
        return 0;
    }
    (void)window;
    if (type != 0 || !mapped_guest_cstring(text) || !mapped_guest_cstring(caption)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uint32_t result = gui::platform::message_box(text, caption);
    set_last_error(result == 0 ? abi::kErrorAccessDenied : abi::kErrorSuccess);
    return static_cast<int>(result);
}

TL_MSABI void* tl_GetDlgItem(const void* dialog, const int identifier) noexcept {
    if (!user32_gui_thread_allowed("GetDlgItem")) {
        return nullptr;
    }
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
    if (!user32_gui_thread_allowed("GetNextDlgTabItem")) {
        return nullptr;
    }
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
    if (!user32_gui_thread_allowed("IsDialogMessageW")) {
        return 0;
    }
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
    if (!user32_gui_thread_allowed("EndDialog")) {
        return 0;
    }
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
    if (!user32_gui_thread_allowed("DialogBoxParamW")) {
        return -1;
    }
    {
        std::lock_guard lock(g_modal_mutex);
        if (g_active_dialog != nullptr) {
            set_last_error(abi::kErrorNotSupported);
            return -1;
        }
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
    static_cast<void>(register_window_handle(&dialog));
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
        child.class_name = util::wide_to_utf8(
            reinterpret_cast<const std::uint16_t*>(item.class_name.c_str()));
        child.text = util::wide_to_utf8(reinterpret_cast<const std::uint16_t*>(item.title.c_str()));
        switch (item.control_class) {
            case runtime::DialogControlClass::Button:
                child.class_name = "BUTTON";
                child.control_kind = ControlKind::Button;
                break;
            case runtime::DialogControlClass::Edit:
                child.class_name = "EDIT";
                child.control_kind = ControlKind::Edit;
                break;
            case runtime::DialogControlClass::Static:
                child.class_name = "STATIC";
                child.control_kind = ControlKind::Static;
                break;
            case runtime::DialogControlClass::ComboBox:
                child.class_name = "COMBOBOX";
                child.control_kind = ControlKind::ComboBox;
                break;
            case runtime::DialogControlClass::Generic:
                child.control_kind = runtime_gui::is_builtin_control(child.class_name.c_str())
                                         ? runtime_gui::control_kind_for(child.class_name.c_str())
                                         : ControlKind::Generic;
                break;
        }
        static_cast<void>(register_window_handle(&child));
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
    flush_dialog_render();
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
        // A fila modal pertence ao thread, não somente ao HWND do diálogo:
        // janelas auxiliares do mesmo fluxo podem postar notificações para
        // que o diálogo as despache enquanto permanece modal.
        const int received = tl_GetMessageW(&message, nullptr, 0, 0);
        if (received <= 0) {
            static_cast<void>(tl_EndDialog(&dialog, kIdCancel));
            break;
        }
        if (tl_IsDialogMessageW(&dialog, &message) != 0) {
            continue;
        }
        const abi::Lresult handled = tl_DispatchMessageW(&message);
        {
            std::lock_guard lock(g_modal_mutex);
            if (g_modal_done) {
                continue;
            }
        }
        if (handled == 0 && message.message == abi::kWmClose) {
            static_cast<void>(tl_EndDialog(&dialog, kIdCancel));
        } else if (handled == 0 && message.message == abi::kWmCommand) {
            const auto notification = static_cast<std::uint32_t>(message.wparam >> 16U);
            const int identifier = static_cast<int>(message.wparam & 0xFFFFU);
            if (notification == 0U && (identifier == kIdOk || identifier == kIdCancel)) {
                static_cast<void>(tl_EndDialog(&dialog, identifier));
            }
        }
    }
    flush_dialog_render();
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

TL_MSABI int tl_MessageBoxW(const void* window, const std::uint16_t* text,
                            const std::uint16_t* caption, const std::uint32_t type) noexcept {
    if (!user32_gui_thread_allowed("MessageBoxW")) {
        return 0;
    }
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

TL_MSABI int tl_MessageBoxExW(void* const hwnd, const std::uint16_t* const text,
                              const std::uint16_t* const caption, const std::uint32_t type,
                              const std::uint16_t language_id) noexcept {
    (void)language_id;
    return tl_MessageBoxW(hwnd, text, caption, type);
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

TL_MSABI void* tl_CreateDialogParamA(void* const instance, const char* const template_name, void* const wnd_parent, void* const dialog_func, const std::intptr_t init_param) noexcept {
    const std::uintptr_t raw_template = reinterpret_cast<std::uintptr_t>(template_name);
    if (raw_template > 0xFFFFU || template_name == nullptr) {
        set_last_error(abi::kErrorNotSupported);
        return nullptr;
    }
    return create_modeless_dialog(instance, reinterpret_cast<const std::uint16_t*>(template_name),
                                  wnd_parent, reinterpret_cast<std::uintptr_t>(dialog_func),
                                  init_param, "CreateDialogParamA");
}

TL_MSABI std::intptr_t tl_DefDlgProcA(void* const hwnd, const std::uint32_t msg, const std::uintptr_t wparam, const std::intptr_t lparam) noexcept {
    return tl_DefWindowProcA(hwnd, msg, wparam, lparam);
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

TL_MSABI int tl_IsDialogMessageA(void* const hwnd, void* const msg) noexcept {
    return tl_IsDialogMessageW(hwnd, msg);
}

TL_MSABI int tl_MessageBoxIndirectW(const void* const msg_box_params) noexcept {
    (void)msg_box_params;
    set_last_error(abi::kErrorSuccess);
    return 1; // IDOK
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

TL_MSABI int tl_SetDlgItemTextA(void* const hwnd, const int id_dlg_item, const char* const text) noexcept {
    (void)hwnd;
    (void)id_dlg_item;
    (void)text;
    set_last_error(abi::kErrorSuccess);
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

TL_MSABI void* tl_CreateDialogParamW(void* const hInstance,
                                     const std::uint16_t* const lpTemplateName,
                                     void* const hWndParent,
                                     void* const lpDialogFunc,
                                     const std::intptr_t dwInitParam) noexcept {
    return create_modeless_dialog(
        hInstance, lpTemplateName, hWndParent,
        reinterpret_cast<std::uintptr_t>(lpDialogFunc), dwInitParam,
        "CreateDialogParamW");
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

TL_MSABI int tl_GetDlgCtrlID(void* const hWnd) noexcept {
    (void)hWnd;
    return 0;
}

TL_MSABI void* tl_GetLastActivePopup(void* const hWnd) noexcept {
    return hWnd;
}

}  // extern "C"
}  // namespace tradutorlinux
