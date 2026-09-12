#include "user32_internal.hpp"
namespace tradutorlinux {

namespace {

constexpr std::uint32_t kRtMenu = 4U;
constexpr std::uint16_t kMenuItemPopup = 0x0001U;
constexpr std::uint16_t kMenuItemLast = 0x0080U;
constexpr std::uint32_t kMenuTypeSeparator = 0x00000800U;
constexpr std::uint32_t kMenuItemInfoType = 0x00000001U;
constexpr std::uint32_t kMenuItemInfoState = 0x00000002U;
constexpr std::uint32_t kMenuItemInfoId = 0x00000004U;
constexpr std::uint32_t kMenuItemInfoSubmenu = 0x00000008U;
constexpr std::uint32_t kMenuItemInfoString = 0x00000040U;
constexpr std::uint32_t kMenuByPosition = 0x00000400U;
constexpr std::uint32_t kMenuStateEnabledMask = 0x00000003U;
constexpr std::uint32_t kMenuStateChecked = 0x00000008U;

[[nodiscard]] MenuSlot* mutable_menu_slot(const void* const menu) noexcept {
    if (menu == nullptr) {
        return nullptr;
    }
    const auto it = std::find_if(g_menus.begin(), g_menus.end(),
                                 [menu](const MenuSlot& entry) {
                                     return entry.used && &entry == menu;
                                 });
    return it == g_menus.end() ? nullptr : &*it;
}

[[nodiscard]] MenuSlot* allocate_menu_slot() noexcept {
    const auto it = std::find_if(g_menus.begin(), g_menus.end(),
                                 [](const MenuSlot& entry) { return !entry.used; });
    if (it == g_menus.end()) {
        return nullptr;
    }
    *it = {};
    it->used = true;
    return &*it;
}

void destroy_menu_tree(MenuSlot& menu) noexcept {
    std::array<MenuSlot*, 256> children{};
    std::size_t child_count = 0;
    for (const MenuItem& item : menu.logical_items) {
        if (item.submenu != nullptr && child_count < children.size()) {
            children[child_count++] = item.submenu;
        }
    }
    for (std::size_t index = 0; index < child_count; ++index) {
        if (children[index] != nullptr && children[index] != &menu && children[index]->used) {
            destroy_menu_tree(*children[index]);
        }
    }
    menu = {};
}

struct MenuTemplateParser {
    std::span<const std::byte> bytes;
    std::size_t parsed_items{0};

    [[nodiscard]] bool read_u16(const std::size_t offset, std::uint16_t& value) const noexcept {
        if (offset > bytes.size() || sizeof(value) > bytes.size() - offset) {
            return false;
        }
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return true;
    }

    [[nodiscard]] bool read_u32(const std::size_t offset, std::uint32_t& value) const noexcept {
        if (offset > bytes.size() || sizeof(value) > bytes.size() - offset) {
            return false;
        }
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return true;
    }

    [[nodiscard]] bool read_text(const std::size_t offset, std::string& text,
                                 std::size_t& next) const noexcept {
        std::array<std::uint16_t, 4097> units{};
        std::size_t count = 0;
        std::size_t cursor = offset;
        while (count < units.size()) {
            std::uint16_t unit = 0;
            if (!read_u16(cursor, unit)) {
                return false;
            }
            cursor += sizeof(unit);
            if (unit == 0) {
                text = util::wide_to_utf8(units.data(), count + 1);
                if (cursor > std::numeric_limits<std::size_t>::max() - 3U) {
                    return false;
                }
                next = (cursor + 3U) & ~static_cast<std::size_t>(3U);
                return next <= bytes.size();
            }
            units[count++] = unit;
        }
        return false;
    }

    [[nodiscard]] bool parse_extended_list(const std::size_t offset, MenuSlot& menu,
                                           const std::size_t depth,
                                           std::size_t& next) noexcept {
        if (depth > 16U) {
            return false;
        }
        std::size_t cursor = offset;
        for (;;) {
            if (++parsed_items > 1024U || cursor > bytes.size() || 14U > bytes.size() - cursor) {
                return false;
            }
            MenuItem item{};
            if (!read_u32(cursor, item.type) || !read_u32(cursor + 4U, item.state) ||
                !read_u32(cursor + 8U, item.command_id) || !read_u16(cursor + 12U, item.flags)) {
                return false;
            }
            std::size_t after_text = 0;
            if (!read_text(cursor + 14U, item.text, after_text)) {
                return false;
            }
            if ((item.type & kMenuTypeSeparator) != 0U) {
                item.text.clear();
            }
            MenuSlot* submenu = nullptr;
            std::size_t submenu_offset = after_text;
            if ((item.flags & kMenuItemPopup) != 0U) {
                submenu = allocate_menu_slot();
                if (submenu == nullptr) {
                    return false;
                }
                std::uint32_t help_id = 0;
                if (!read_u32(submenu_offset, help_id)) {
                    destroy_menu_tree(*submenu);
                    return false;
                }
                submenu_offset += sizeof(help_id);
            }
            try {
                menu.logical_items.push_back(std::move(item));
            } catch (...) {
                if (submenu != nullptr) {
                    destroy_menu_tree(*submenu);
                }
                return false;
            }
            MenuItem& stored = menu.logical_items.back();
            stored.submenu = submenu;
            if (stored.submenu != nullptr &&
                !parse_extended_list(submenu_offset, *stored.submenu, depth + 1U, after_text)) {
                MenuSlot* const failed_submenu = stored.submenu;
                menu.logical_items.pop_back();
                destroy_menu_tree(*failed_submenu);
                return false;
            }
            cursor = after_text;
            if ((stored.flags & kMenuItemLast) != 0U) {
                next = cursor;
                return true;
            }
        }
    }
};

[[nodiscard]] bool parse_menu_template(const void* const resource_data,
                                       const std::size_t resource_size,
                                       MenuSlot& menu) noexcept {
    if (resource_data == nullptr || resource_size < 8U) {
        return false;
    }
    MenuTemplateParser parser{
        .bytes = std::span<const std::byte>{static_cast<const std::byte*>(resource_data), resource_size}};
    std::uint16_t version = 0;
    std::uint16_t offset = 0;
    if (!parser.read_u16(0, version) || !parser.read_u16(2, offset) || version != 1U) {
        return false;
    }
    // In the extended format wOffset is relative to the end of its own
    // WORD, so the usual value 4 points at byte 8 (after dwHelpId).
    const std::size_t first_item = 4U + offset;
    if (first_item > resource_size) {
        return false;
    }
    std::size_t end = 0;
    return parser.parse_extended_list(first_item, menu, 0, end);
}

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
    void* item_bitmap;
};
static_assert(sizeof(GuestMenuItemInfoW) == 80);

[[nodiscard]] MenuItem* menu_item_for(MenuSlot& menu, const std::uint32_t item,
                                       const int by_position) noexcept {
    if (by_position != 0) {
        return item < menu.logical_items.size() ? &menu.logical_items[item] : nullptr;
    }
    const auto it = std::find_if(menu.logical_items.begin(), menu.logical_items.end(),
                                 [item](const MenuItem& entry) {
                                     return entry.command_id == item;
                                 });
    return it == menu.logical_items.end() ? nullptr : &*it;
}

[[nodiscard]] MenuItem* menu_item_for_flags(MenuSlot& menu, const std::uint32_t item,
                                             const std::uint32_t flags) noexcept {
    return menu_item_for(menu, item, (flags & kMenuByPosition) != 0U ? 1 : 0);
}

}  // namespace

extern "C" {

TL_MSABI void* tl_CreatePopupMenu() noexcept {
    if (!user32_gui_thread_allowed("CreatePopupMenu")) {
        return nullptr;
    }
    const auto free_it = std::find_if(g_menus.begin(), g_menus.end(),
                                      [](const MenuSlot& menu) { return !menu.used; });
    if (free_it == g_menus.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    free_it->used = true;
    free_it->items.clear();
    free_it->logical_items.clear();
    set_last_error(abi::kErrorSuccess);
    return &*free_it;
}

TL_MSABI int tl_AppendMenuA(const void* menu, std::uint32_t flags, std::uintptr_t command,
                            const char* text) noexcept {
    if (!user32_gui_thread_allowed("AppendMenuA")) {
        return 0;
    }
    const auto it = std::find_if(g_menus.begin(), g_menus.end(),
                                 [menu](const MenuSlot& entry) { return entry.used && &entry == menu; });
    if (it == g_menus.end()) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    it->items.push_back(gui::PopupMenuItem{.command = static_cast<std::uint32_t>(command),
                                           .text = text != nullptr ? text : "",
                                           .separator = (flags & 0x00000800U) != 0});
    it->logical_items.push_back(MenuItem{.type = flags,
                                         .state = 0,
                                         .command_id = static_cast<std::uint32_t>(command),
                                         .flags = 0,
                                         .text = text != nullptr ? text : "",
                                         .submenu = nullptr});
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
    if (!user32_gui_thread_allowed("DestroyMenu")) {
        return 0;
    }
    const auto it = std::find_if(g_menus.begin(), g_menus.end(),
                                 [menu](const MenuSlot& entry) { return entry.used && &entry == menu; });
    if (it == g_menus.end()) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    destroy_menu_tree(*it);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_TrackPopupMenu(const void* menu, std::uint32_t flags, int x, int y, int reserved,
                               const void* owner, const void* rect) noexcept {
    if (!user32_gui_thread_allowed("TrackPopupMenu")) {
        return 0;
    }
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
    if (WindowSlot* const window = find_window_slot(hwnd); window != nullptr &&
        window->menu_handle != nullptr) {
        return const_cast<void*>(window->menu_handle);
    }
    return &g_dummy_menu;
}

TL_MSABI int tl_SetMenu(void* const hwnd, void* const menu) noexcept {
    if (hwnd == nullptr) {
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    WindowSlot* const window = find_window_slot(hwnd);
    if (window == nullptr || (menu != nullptr && mutable_menu_slot(menu) == nullptr)) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    window->menu_handle = menu;
    if (window->native != nullptr && window->mapped && !window->is_control) {
        render_controls(*window);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_GetSubMenu(void* const menu, const int pos) noexcept {
    if (MenuSlot* const actual = mutable_menu_slot(menu); actual != nullptr && pos >= 0) {
        const auto index = static_cast<std::size_t>(pos);
        if (index < actual->logical_items.size()) {
            return actual->logical_items[index].submenu;
        }
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (menu == &g_dummy_menu && pos == 0) {
        return &g_dummy_sub_menu;
    }
    set_last_error(abi::kErrorInvalidHandle);
    return nullptr;
}

TL_MSABI int tl_GetMenuItemCount(void* const menu) noexcept {
    if (const MenuSlot* const actual = find_menu_slot(menu); actual != nullptr) {
        return static_cast<int>(actual->logical_items.size());
    }
    return menu == &g_dummy_menu ? 5 : -1;
}

TL_MSABI int tl_GetMenuItemInfoW(void* const menu, const std::uint32_t item, const int f_by_position,
                                void* const mii) noexcept {
    MenuSlot* const actual = mutable_menu_slot(menu);
    MenuItem* const entry = actual == nullptr ? nullptr : menu_item_for(*actual, item, f_by_position);
    if (mii == nullptr || !mapped_guest_range(mii, sizeof(GuestMenuItemInfoW), true) ||
        entry == nullptr) {
        const std::array<diagnostics::TraceField, 4> fields{
            diagnostics::TraceField{"symbol", "GetMenuItemInfoW"},
            diagnostics::TraceField{"status", "invalid-output"},
            diagnostics::TraceField{"item", std::to_string(item)},
            diagnostics::TraceField{"mii", std::to_string(reinterpret_cast<std::uintptr_t>(mii))}};
        runtime_trace("GetMenuItemInfoW", fields, 4);
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    auto* const output = static_cast<GuestMenuItemInfoW*>(mii);
    if (output->cb_size < sizeof(GuestMenuItemInfoW)) {
        set_last_error(abi::kErrorBadLength);
        return 0;
    }
    if ((output->f_mask & kMenuItemInfoType) != 0U) {
        output->f_type = entry->type;
    }
    if ((output->f_mask & kMenuItemInfoState) != 0U) {
        output->f_state = entry->state;
    }
    if ((output->f_mask & kMenuItemInfoId) != 0U) {
        output->item_id = entry->command_id;
    }
    if ((output->f_mask & kMenuItemInfoSubmenu) != 0U) {
        output->sub_menu = entry->submenu;
    }
    if ((output->f_mask & kMenuItemInfoString) != 0U) {
        if (output->type_data == nullptr || output->char_count == 0U) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        const std::u16string text = util::utf8_to_wide(entry->text);
        const std::size_t capacity = output->char_count;
        if (capacity > std::numeric_limits<std::size_t>::max() / sizeof(std::uint16_t)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        if (!mapped_guest_range(output->type_data, capacity * sizeof(std::uint16_t), true)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        const std::size_t copied = std::min(text.size(), capacity - 1U);
        std::memcpy(output->type_data, text.data(), copied * sizeof(std::uint16_t));
        output->type_data[copied] = 0;
        output->char_count = static_cast<std::uint32_t>(copied);
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
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_MSABI int tl_InsertMenuItemW(void* const menu, const std::uint32_t item, const int f_by_position,
                               const void* const mii) noexcept {
    (void)menu;
    (void)item;
    (void)f_by_position;
    (void)mii;
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_MSABI int tl_RemoveMenu(void* const menu, const std::uint32_t position, const std::uint32_t flags) noexcept {
    (void)menu;
    (void)position;
    (void)flags;
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_MSABI int tl_EnableMenuItem(void* const menu, const std::uint32_t item, const std::uint32_t enable) noexcept {
    if (!user32_gui_thread_allowed("EnableMenuItem")) {
        return -1;
    }
    MenuSlot* const actual = mutable_menu_slot(menu);
    MenuItem* const entry = actual == nullptr ? nullptr : menu_item_for_flags(*actual, item, enable);
    if (entry == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return -1;
    }
    const std::uint32_t previous = entry->state & kMenuStateEnabledMask;
    entry->state = (entry->state & ~kMenuStateEnabledMask) | (enable & kMenuStateEnabledMask);
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(previous);
}

TL_MSABI std::uint32_t tl_CheckMenuItem(void* const menu, const std::uint32_t item, const std::uint32_t check) noexcept {
    if (!user32_gui_thread_allowed("CheckMenuItem")) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    MenuSlot* const actual = mutable_menu_slot(menu);
    MenuItem* const entry = actual == nullptr ? nullptr : menu_item_for_flags(*actual, item, check);
    if (entry == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return std::numeric_limits<std::uint32_t>::max();
    }
    const std::uint32_t previous = entry->state & kMenuStateChecked;
    if ((check & kMenuStateChecked) != 0U) {
        entry->state |= kMenuStateChecked;
    } else {
        entry->state &= ~kMenuStateChecked;
    }
    set_last_error(abi::kErrorSuccess);
    return previous;
}

TL_MSABI int tl_CheckMenuRadioItem(void* const menu, const std::uint32_t first, const std::uint32_t last,
                                  const std::uint32_t check, const std::uint32_t flags) noexcept {
    if (!user32_gui_thread_allowed("CheckMenuRadioItem")) {
        return 0;
    }
    MenuSlot* const actual = mutable_menu_slot(menu);
    if (actual == nullptr || first > last) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    const bool by_position = (flags & kMenuByPosition) != 0U;
    bool range_found = false;
    bool check_found = false;
    for (std::size_t index = 0; index < actual->logical_items.size(); ++index) {
        MenuItem& entry = actual->logical_items[index];
        const std::uint32_t value = by_position ? static_cast<std::uint32_t>(index)
                                                : entry.command_id;
        if (value < first || value > last) {
            continue;
        }
        range_found = true;
        if (value == check) {
            entry.state |= kMenuStateChecked;
            check_found = true;
        } else {
            entry.state &= ~kMenuStateChecked;
        }
    }
    if (!range_found || !check_found) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
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
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_MSABI void* tl_LoadMenuW(void* const instance, const std::uint16_t* const menu_name) noexcept {
    const std::uint16_t* const type = reinterpret_cast<const std::uint16_t*>(kRtMenu);
    void* const resource = tl_FindResourceW(instance, menu_name, type);
    void* const loaded = resource == nullptr ? nullptr : tl_LoadResource(instance, resource);
    const std::uint32_t resource_size =
        loaded == nullptr ? 0U : tl_SizeofResource(instance, resource);
    const void* const resource_data = loaded == nullptr ? nullptr : tl_LockResource(loaded);
    if (resource_data == nullptr || resource_size == 0U) {
        return nullptr;
    }
    MenuSlot* const menu = allocate_menu_slot();
    if (menu == nullptr ||
        !parse_menu_template(resource_data, resource_size, *menu)) {
        if (menu != nullptr) {
            destroy_menu_tree(*menu);
        }
        set_last_error(abi::kErrorBadLength);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "LoadMenuW"},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"items", std::to_string(menu->logical_items.size())},
        diagnostics::TraceField{"resource-size", std::to_string(resource_size)}};
    runtime_trace("LoadMenuW", fields, 4);
    return menu;
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
