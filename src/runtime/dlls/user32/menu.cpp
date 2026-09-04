#include "user32_internal.hpp"
namespace tradutorlinux {
extern "C" {

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

TL_MSABI void* tl_GetMenu(void* const hwnd) noexcept {
    (void)hwnd;
    return &g_dummy_menu;
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
    return &g_dummy_sub_menu;
}

TL_MSABI int tl_GetMenuItemCount(void* const menu) noexcept {
    (void)menu;
    return 5;
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
    set_last_error(abi::kErrorSuccess);
    return &g_dummy_menu;
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

TL_MSABI void* tl_CreateMenu() noexcept {
    set_last_error(abi::kErrorNotSupported);
    return nullptr;
}

TL_MSABI int tl_DeleteMenu(void* const menu, const std::uint32_t position, const std::uint32_t flags) noexcept {
    (void)menu;
    (void)position;
    (void)flags;
    set_last_error(abi::kErrorSuccess);
    return 1;
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

TL_MSABI void* tl_CreateAcceleratorTableW(void* const paccel, const int cAccel) noexcept {
    (void)paccel;
    (void)cAccel;
    return reinterpret_cast<void*>(0x4143434CULL);
}

TL_MSABI int tl_DestroyAcceleratorTable(void* const hAccel) noexcept {
    (void)hAccel;
    return 1;
}

TL_MSABI int tl_ModifyMenuW(void* const hMnu, const std::uint32_t uPosition, const std::uint32_t uFlags, const std::uintptr_t uIDNewItem, const wchar_t* const lpNewItem) noexcept {
    (void)hMnu;
    (void)uPosition;
    (void)uFlags;
    (void)uIDNewItem;
    (void)lpNewItem;
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

TL_MSABI std::uint32_t tl_GetMenuItemID(void* const hMenu, const int nPos) noexcept {
    (void)hMenu;
    (void)nPos;
    return 0;
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

TL_MSABI int tl_SetMenuItemBitmaps(void* const hMenu, const std::uint32_t uPosition, const std::uint32_t uFlags, void* const hBitmapUnchecked, void* const hBitmapChecked) noexcept {
    (void)hMenu;
    (void)uPosition;
    (void)uFlags;
    (void)hBitmapUnchecked;
    (void)hBitmapChecked;
    return 1;
}

}  // extern "C"
}  // namespace tradutorlinux
