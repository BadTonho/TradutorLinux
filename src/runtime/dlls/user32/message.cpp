#include "user32_internal.hpp"
#include "tradutorlinux/runtime/ws2_32.hpp"

#include <cstddef>
#include <cstring>
#include <limits>

namespace tradutorlinux {

namespace {

struct PendingNativeMessage {
    abi::GuestMsg message{};
    bool has_message{false};
    bool is_timer{false};
    bool is_paint{false};
};

std::array<PendingNativeMessage, 32> g_pending_native{};

[[nodiscard]] PendingNativeMessage& pending_native(WindowSlot& slot) noexcept {
    return g_pending_native[static_cast<std::size_t>(&slot - g_windows.data())];
}

[[nodiscard]] abi::Lparam mouse_lparam(const int x, const int y) noexcept {
    return (static_cast<std::intptr_t>(y & 0xFFFF) << 16) |
           static_cast<std::intptr_t>(x & 0xFFFF);
}

[[nodiscard]] WindowSlot* event_control(WindowSlot& parent, const gui::WindowEvent& event) noexcept {
    // Um submenu aberto captura os cliques sobre a superfície lógica. Sem
    // esta precedência, a faixa do popup que cobre a toolbar seria entregue
    // também ao controle Win32 por baixo do menu.
    if (parent.open_menu_index >= 0) {
        return nullptr;
    }
    return runtime_gui::find_control_at(parent, std::span<WindowSlot>{g_windows}, event.x, event.y);
}

[[nodiscard]] bool logical_child_mouse_message(WindowSlot* const control) noexcept {
    return control != nullptr && control->wndproc != 0 && control->is_control &&
           control->parent != nullptr;
}

[[nodiscard]] std::string toolbar_command_ids(const WindowSlot& toolbar) {
    std::string result;
    for (std::size_t index = 0; index < toolbar.toolbar_buttons.size(); ++index) {
        if (index != 0) {
            result += ",";
        }
        result += std::to_string(toolbar.toolbar_buttons[index].command_id);
    }
    return result;
}

[[nodiscard]] const char* toolbar_message_name(const std::uint32_t message) noexcept {
    switch (message) {
        case abi::kTbButtonStructSize: return "TB_BUTTONSTRUCTSIZE";
        case abi::kTbAddButtons: return "TB_ADDBUTTONS";
        case abi::kTbAddButtonsW: return "TB_ADDBUTTONSW";
        case abi::kTbButtonCount: return "TB_BUTTONCOUNT";
        case abi::kTbDeleteButton: return "TB_DELETEBUTTON";
        case abi::kTbSetButtonSize: return "TB_SETBUTTONSIZE";
        case abi::kTbSetBitmapSize: return "TB_SETBITMAPSIZE";
        case abi::kTbAutoSize: return "TB_AUTOSIZE";
        case abi::kTbSetImageList: return "TB_SETIMAGELIST";
        case abi::kTbEnableButton: return "TB_ENABLEBUTTON";
        default: return "unknown";
    }
}

constexpr std::uint32_t kTreeFirstMessage = 0x1100U;
constexpr std::uint32_t kTreeInsertItemA = kTreeFirstMessage + 0U;
constexpr std::uint32_t kTreeDeleteItem = kTreeFirstMessage + 1U;
constexpr std::uint32_t kTreeExpand = kTreeFirstMessage + 2U;
constexpr std::uint32_t kTreeGetCount = kTreeFirstMessage + 5U;
constexpr std::uint32_t kTreeGetNextItem = kTreeFirstMessage + 10U;
constexpr std::uint32_t kTreeSelectItem = kTreeFirstMessage + 11U;
constexpr std::uint32_t kTreeGetItemA = kTreeFirstMessage + 12U;
constexpr std::uint32_t kTreeSetItemA = kTreeFirstMessage + 13U;
constexpr std::uint32_t kTreeInsertItemW = kTreeFirstMessage + 50U;
constexpr std::uint32_t kTreeGetItemW = kTreeFirstMessage + 62U;
constexpr std::uint32_t kTreeSetItemW = kTreeFirstMessage + 63U;

constexpr std::uint32_t kTreeGetRoot = 0U;
constexpr std::uint32_t kTreeGetNext = 1U;
constexpr std::uint32_t kTreeGetPrevious = 2U;
constexpr std::uint32_t kTreeGetParent = 3U;
constexpr std::uint32_t kTreeGetChild = 4U;
constexpr std::uint32_t kTreeGetNextVisible = 6U;
constexpr std::uint32_t kTreeGetPreviousVisible = 7U;
constexpr std::uint32_t kTreeGetCaret = 9U;

constexpr std::uint32_t kTreeCollapse = 1U;
constexpr std::uint32_t kTreeExpandAction = 2U;
constexpr std::uint32_t kTreeToggle = 3U;
constexpr std::uint32_t kTreeCollapseReset = 0x8000U;

constexpr std::uint32_t kTreeItemText = 0x0001U;
constexpr std::uint32_t kTreeItemParam = 0x0004U;
constexpr std::uint32_t kTreeNotifySelectionChanged = 1U;
constexpr std::int32_t kTreeNotifySelectedChangedCode = -402;
constexpr std::size_t kTreeItemBufferSize = 56U;
constexpr std::size_t kTreeInsertBufferSize = 72U;
constexpr std::size_t kTreeNotificationSize = 152U;
constexpr std::size_t kTreeItemLimit = 4096U;

constexpr std::uintptr_t kTreeRootHandle =
    std::numeric_limits<std::uintptr_t>::max() - static_cast<std::uintptr_t>(0xFFFFU);
constexpr std::uintptr_t kTreeFirstHandle =
    std::numeric_limits<std::uintptr_t>::max() - static_cast<std::uintptr_t>(0xFFFEU);
constexpr std::uintptr_t kTreeLastHandle =
    std::numeric_limits<std::uintptr_t>::max() - static_cast<std::uintptr_t>(0xFFFDU);
constexpr std::uintptr_t kTreeSortHandle =
    std::numeric_limits<std::uintptr_t>::max() - static_cast<std::uintptr_t>(0xFFFCU);

struct GuestTreeItemNotification {
    std::uint32_t mask{};
    std::uint32_t padding{};
    void* h_item{};
    std::uint32_t state{};
    std::uint32_t state_mask{};
    char* text{};
    std::int32_t text_capacity{};
    std::int32_t image{};
    std::int32_t selected_image{};
    std::int32_t children{};
    std::intptr_t item_data{};
};
static_assert(sizeof(GuestTreeItemNotification) == kTreeItemBufferSize);
static_assert(offsetof(GuestTreeItemNotification, h_item) == 8U);
static_assert(offsetof(GuestTreeItemNotification, text) == 24U);
static_assert(offsetof(GuestTreeItemNotification, item_data) == 48U);

struct GuestTreeNotificationHeader {
    void* hwnd_from{};
    std::uintptr_t id_from{};
    std::int32_t code{};
    std::int32_t padding{};
};
static_assert(sizeof(GuestTreeNotificationHeader) == 24U);

struct GuestTreeNotification {
    GuestTreeNotificationHeader header{};
    std::uint32_t action{};
    std::uint32_t padding{};
    GuestTreeItemNotification old_item{};
    GuestTreeItemNotification new_item{};
    std::int32_t point_x{};
    std::int32_t point_y{};
};
static_assert(sizeof(GuestTreeNotification) == kTreeNotificationSize);

[[nodiscard]] std::uint32_t tree_u32(const std::byte* const bytes,
                                     const std::size_t offset) noexcept {
    std::uint32_t value{};
    std::memcpy(&value, bytes + offset, sizeof(value));
    return value;
}

[[nodiscard]] std::int32_t tree_i32(const std::byte* const bytes,
                                    const std::size_t offset) noexcept {
    std::int32_t value{};
    std::memcpy(&value, bytes + offset, sizeof(value));
    return value;
}

[[nodiscard]] std::uintptr_t tree_pointer(const std::byte* const bytes,
                                           const std::size_t offset) noexcept {
    std::uintptr_t value{};
    std::memcpy(&value, bytes + offset, sizeof(value));
    return value;
}

[[nodiscard]] bool tree_is_special_handle(const std::uintptr_t handle) noexcept {
    return handle >= kTreeRootHandle && handle <= kTreeSortHandle;
}

[[nodiscard]] TreeItem* tree_find_item(WindowSlot& tree,
                                       const std::uintptr_t handle) noexcept {
    const auto found = std::find_if(tree.tree_items.begin(), tree.tree_items.end(),
                                    [handle](const TreeItem& item) {
                                        return item.handle == handle;
                                    });
    return found == tree.tree_items.end() ? nullptr : &*found;
}

[[nodiscard]] const TreeItem* tree_find_item(const WindowSlot& tree,
                                             const std::uintptr_t handle) noexcept {
    const auto found = std::find_if(tree.tree_items.begin(), tree.tree_items.end(),
                                    [handle](const TreeItem& item) {
                                        return item.handle == handle;
                                    });
    return found == tree.tree_items.end() ? nullptr : &*found;
}

[[nodiscard]] std::size_t tree_item_index(const WindowSlot& tree,
                                          const std::uintptr_t handle) noexcept {
    for (std::size_t index = 0; index < tree.tree_items.size(); ++index) {
        if (tree.tree_items[index].handle == handle) {
            return index;
        }
    }
    return tree.tree_items.size();
}

[[nodiscard]] bool read_tree_text(const std::uintptr_t pointer, const std::int32_t cch,
                                  const bool wide, std::string& output) {
    output.clear();
    if (pointer == 0U) {
        return true;
    }
    constexpr std::size_t kTreeTextLimit = 1U << 20U;
    if (cch > static_cast<std::int32_t>(kTreeTextLimit)) {
        return false;
    }

    const std::size_t capacity = cch > 0 ? static_cast<std::size_t>(cch)
                                         : kTreeTextLimit + 1U;
    if (!wide) {
        return runtime::copy_guest_cstring(reinterpret_cast<const char*>(pointer), capacity,
                                           output);
    }
    std::u16string text;
    if (!runtime::copy_guest_wstring(reinterpret_cast<const std::uint16_t*>(pointer), capacity,
                                     text)) {
        return false;
    }
    output = util::wide_to_utf8(reinterpret_cast<const std::uint16_t*>(text.data()), text.size());
    return true;
}

[[nodiscard]] std::uintptr_t tree_parent_handle(const std::uintptr_t raw) noexcept {
    return raw == 0U || tree_is_special_handle(raw) ? 0U : raw;
}

[[nodiscard]] std::size_t tree_insert_position(const WindowSlot& tree,
                                               const std::uintptr_t parent,
                                               const std::uintptr_t insert_after) noexcept {
    std::size_t first_sibling = tree.tree_items.size();
    std::size_t last_sibling = tree.tree_items.size();
    std::size_t parent_index = tree.tree_items.size();
    std::size_t after_index = tree.tree_items.size();
    for (std::size_t index = 0; index < tree.tree_items.size(); ++index) {
        const TreeItem& item = tree.tree_items[index];
        if (item.handle == parent) {
            parent_index = index;
        }
        if (item.parent != parent) {
            continue;
        }
        if (first_sibling == tree.tree_items.size()) {
            first_sibling = index;
        }
        last_sibling = index;
        if (item.handle == insert_after) {
            after_index = index;
        }
    }

    if (insert_after == kTreeFirstHandle) {
        if (first_sibling != tree.tree_items.size()) {
            return first_sibling;
        }
    } else if (insert_after != 0U && insert_after != kTreeLastHandle &&
               insert_after != kTreeSortHandle) {
        if (after_index != tree.tree_items.size()) {
            return after_index + 1U;
        }
        return tree.tree_items.size();
    }
    if (last_sibling != tree.tree_items.size()) {
        return last_sibling + 1U;
    }
    if (parent != 0U && parent_index != tree.tree_items.size()) {
        return parent_index + 1U;
    }
    return tree.tree_items.size();
}

[[nodiscard]] int tree_insert_item(WindowSlot& tree, const abi::Lparam lparam,
                                   const bool wide) {
    std::array<std::byte, kTreeInsertBufferSize> bytes{};
    if (lparam == 0 || tree.tree_items.size() >= kTreeItemLimit ||
        runtime::read_guest_memory(reinterpret_cast<const void*>(lparam), bytes.data(),
                                   bytes.size()).status != runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uint32_t mask = tree_u32(bytes.data(), 16U);
    const std::uintptr_t raw_parent = tree_pointer(bytes.data(), 0U);
    const std::uintptr_t parent = tree_parent_handle(raw_parent);
    const std::uintptr_t insert_after = tree_pointer(bytes.data(), 8U);
    if (parent != 0U && tree_find_item(tree, parent) == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (insert_after != 0U && insert_after != kTreeFirstHandle &&
        insert_after != kTreeLastHandle && insert_after != kTreeSortHandle) {
        const TreeItem* const after = tree_find_item(tree, insert_after);
        if (after == nullptr || after->parent != parent) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }

    std::string text;
    if ((mask & kTreeItemText) != 0U &&
        !read_tree_text(tree_pointer(bytes.data(), 40U), tree_i32(bytes.data(), 48U), wide, text)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uintptr_t item_data = (mask & kTreeItemParam) != 0U
                                         ? tree_pointer(bytes.data(), 64U)
                                         : 0U;
    if (tree.tree_next_handle == 0U ||
        tree.tree_next_handle > static_cast<std::uintptr_t>(std::numeric_limits<int>::max())) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    const std::uintptr_t handle = tree.tree_next_handle++;
    const std::size_t position = tree_insert_position(tree, parent, insert_after);
    tree.tree_items.insert(tree.tree_items.begin() +
                               static_cast<std::vector<TreeItem>::difference_type>(position),
                           TreeItem{handle, parent, item_data, std::move(text), false});
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(handle);
}

void fill_tree_notification_item(GuestTreeItemNotification& output,
                                  const TreeItem* const item) noexcept {
    output = {};
    if (item == nullptr) {
        return;
    }
    output.mask = kTreeItemText | kTreeItemParam;
    output.h_item = reinterpret_cast<void*>(item->handle);
    output.text = const_cast<char*>(item->text.c_str());
    output.text_capacity = static_cast<std::int32_t>(
        std::min<std::size_t>(item->text.size() + 1U,
                              static_cast<std::size_t>(std::numeric_limits<int>::max())));
    output.item_data = static_cast<std::intptr_t>(item->item_data);
}

void notify_tree_selection(WindowSlot& tree, const std::uintptr_t old_handle,
                           const std::uintptr_t new_handle) noexcept {
    if (tree.parent == nullptr || tree.parent->wndproc == 0U) {
        return;
    }
    static thread_local GuestTreeNotification notification{};
    notification = {};
    notification.header.hwnd_from = &tree;
    notification.header.id_from = tree.control_id;
    notification.header.code = kTreeNotifySelectedChangedCode;
    notification.action = kTreeNotifySelectionChanged;
    fill_tree_notification_item(notification.old_item, tree_find_item(tree, old_handle));
    fill_tree_notification_item(notification.new_item, tree_find_item(tree, new_handle));
    static_cast<void>(call_wndproc(tree.parent->wndproc, tree.parent, abi::kWmNotify, 0,
                                   reinterpret_cast<abi::Lparam>(&notification)));
}

[[nodiscard]] std::vector<std::uintptr_t> tree_visible_items(const WindowSlot& tree) {
    std::vector<std::uintptr_t> result;
    result.reserve(tree.tree_items.size());
    std::vector<std::size_t> pending;
    pending.reserve(tree.tree_items.size());
    for (std::size_t index = tree.tree_items.size(); index > 0U; --index) {
        if (tree.tree_items[index - 1U].parent == 0U) {
            pending.push_back(index - 1U);
        }
    }
    while (!pending.empty()) {
        const std::size_t index = pending.back();
        pending.pop_back();
        const TreeItem& item = tree.tree_items[index];
        result.push_back(item.handle);
        if (!item.expanded) {
            continue;
        }
        for (std::size_t child = tree.tree_items.size(); child > 0U; --child) {
            if (tree.tree_items[child - 1U].parent == item.handle) {
                pending.push_back(child - 1U);
            }
        }
    }
    return result;
}

[[nodiscard]] int tree_next_item(WindowSlot& tree, const abi::Wparam relation,
                                 const abi::Lparam lparam) {
    const std::uintptr_t handle = static_cast<std::uintptr_t>(lparam);
    if (relation == kTreeGetCaret) {
        set_last_error(abi::kErrorSuccess);
        return static_cast<int>(tree.tree_selected);
    }
    if (relation == kTreeGetRoot) {
        if (handle == 0U) {
            for (const TreeItem& item : tree.tree_items) {
                if (item.parent == 0U) {
                    set_last_error(abi::kErrorSuccess);
                    return static_cast<int>(item.handle);
                }
            }
            return 0;
        }
        const TreeItem* item = tree_find_item(tree, handle);
        if (item == nullptr) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        while (item->parent != 0U) {
            item = tree_find_item(tree, item->parent);
            if (item == nullptr) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
        }
        set_last_error(abi::kErrorSuccess);
        return static_cast<int>(item->handle);
    }
    if (relation == kTreeGetChild) {
        const std::uintptr_t parent = handle;
        if (parent != 0U && tree_find_item(tree, parent) == nullptr) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        for (const TreeItem& item : tree.tree_items) {
            if (item.parent == parent) {
                set_last_error(abi::kErrorSuccess);
                return static_cast<int>(item.handle);
            }
        }
        return 0;
    }
    if (relation == kTreeGetParent) {
        const TreeItem* const item = tree_find_item(tree, handle);
        if (item == nullptr) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        set_last_error(abi::kErrorSuccess);
        return static_cast<int>(item->parent);
    }
    if (relation == kTreeGetNextVisible || relation == kTreeGetPreviousVisible) {
        const std::vector<std::uintptr_t> visible = tree_visible_items(tree);
        const auto found = std::find(visible.begin(), visible.end(), handle);
        if (found == visible.end()) {
            set_last_error(handle == 0U ? abi::kErrorSuccess : abi::kErrorInvalidParameter);
            return 0;
        }
        if (relation == kTreeGetNextVisible) {
            if (found + 1 == visible.end()) return 0;
            set_last_error(abi::kErrorSuccess);
            return static_cast<int>(*(found + 1));
        }
        if (found == visible.begin()) return 0;
        set_last_error(abi::kErrorSuccess);
        return static_cast<int>(*(found - 1));
    }
    const TreeItem* const item = tree_find_item(tree, handle);
    if (item == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (relation != kTreeGetNext && relation != kTreeGetPrevious) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::size_t index = tree_item_index(tree, handle);
    if (relation == kTreeGetNext) {
        for (std::size_t next = index + 1U; next < tree.tree_items.size(); ++next) {
            if (tree.tree_items[next].parent == item->parent) {
                set_last_error(abi::kErrorSuccess);
                return static_cast<int>(tree.tree_items[next].handle);
            }
        }
    } else {
        for (std::size_t previous = index; previous > 0U; --previous) {
            if (tree.tree_items[previous - 1U].parent == item->parent) {
                set_last_error(abi::kErrorSuccess);
                return static_cast<int>(tree.tree_items[previous - 1U].handle);
            }
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 0;
}

[[nodiscard]] int tree_select_item(WindowSlot& tree, const abi::Lparam lparam) noexcept {
    const std::uintptr_t selected = static_cast<std::uintptr_t>(lparam);
    if (selected != 0U && tree_find_item(tree, selected) == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uintptr_t previous = tree.tree_selected;
    if (previous != selected) {
        tree.tree_selected = selected;
        notify_tree_selection(tree, previous, selected);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

[[nodiscard]] int tree_expand_item(WindowSlot& tree, const abi::Wparam action,
                                   const abi::Lparam lparam) noexcept {
    TreeItem* const item = tree_find_item(tree, static_cast<std::uintptr_t>(lparam));
    if (item == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uint32_t operation = static_cast<std::uint32_t>(action) & 0xFFFFU;
    if (operation == kTreeCollapse || operation == kTreeCollapseReset) {
        item->expanded = false;
    } else if (operation == kTreeExpandAction) {
        item->expanded = true;
    } else if (operation == kTreeToggle) {
        item->expanded = !item->expanded;
    } else {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

[[nodiscard]] bool read_tree_item_request(const abi::Lparam lparam,
                                          std::array<std::byte, kTreeItemBufferSize>& bytes) noexcept {
    if (lparam == 0 ||
        runtime::read_guest_memory(reinterpret_cast<const void*>(lparam), bytes.data(),
                                   bytes.size()).status != runtime::GuestMemoryAccessStatus::Success) {
        return false;
    }
    return true;
}

[[nodiscard]] int tree_get_item(WindowSlot& tree, const abi::Lparam lparam, const bool wide) {
    std::array<std::byte, kTreeItemBufferSize> bytes{};
    if (!read_tree_item_request(lparam, bytes)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uint32_t mask = tree_u32(bytes.data(), 0U);
    const TreeItem* const item = tree_find_item(tree, tree_pointer(bytes.data(), 8U));
    if (item == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }

    std::u16string wide_text;
    const std::uintptr_t output_pointer = tree_pointer(bytes.data(), 24U);
    const std::int32_t output_capacity = tree_i32(bytes.data(), 32U);
    if ((mask & kTreeItemText) != 0U) {
        if (output_pointer == 0U || output_capacity <= 0 ||
            output_capacity > static_cast<std::int32_t>(1U << 20U)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        if (wide) {
            wide_text = util::utf8_to_wide(item->text);
        }
    }
    const std::uintptr_t item_data = item->item_data;
    if ((mask & kTreeItemText) != 0U) {
        if (!wide) {
            const std::size_t count = std::min<std::size_t>(
                item->text.size(), static_cast<std::size_t>(output_capacity - 1));
            std::string output = item->text.substr(0, count);
            output.push_back('\0');
            if (runtime::write_guest_memory(reinterpret_cast<void*>(output_pointer), output.data(),
                                             output.size()).status !=
                runtime::GuestMemoryAccessStatus::Success) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
        } else {
            const std::size_t count = std::min<std::size_t>(
                wide_text.size(), static_cast<std::size_t>(output_capacity - 1));
            std::u16string output = wide_text.substr(0, count);
            output.push_back(0);
            if (runtime::write_guest_memory(reinterpret_cast<void*>(output_pointer), output.data(),
                                             output.size() * sizeof(std::uint16_t)).status !=
                runtime::GuestMemoryAccessStatus::Success) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
        }
    }
    if ((mask & kTreeItemParam) != 0U) {
        const std::intptr_t parameter = static_cast<std::intptr_t>(item_data);
        std::memcpy(bytes.data() + 48U, &parameter, sizeof(parameter));
        if (runtime::write_guest_memory(reinterpret_cast<void*>(lparam), bytes.data(), bytes.size()).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

[[nodiscard]] int tree_set_item(WindowSlot& tree, const abi::Lparam lparam, const bool wide) {
    std::array<std::byte, kTreeItemBufferSize> bytes{};
    if (!read_tree_item_request(lparam, bytes)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    TreeItem* const item = tree_find_item(tree, tree_pointer(bytes.data(), 8U));
    if (item == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uint32_t mask = tree_u32(bytes.data(), 0U);
    std::string text;
    if ((mask & kTreeItemText) != 0U &&
        !read_tree_text(tree_pointer(bytes.data(), 24U), tree_i32(bytes.data(), 32U), wide,
                        text)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if ((mask & kTreeItemText) != 0U) {
        item->text = std::move(text);
    }
    if ((mask & kTreeItemParam) != 0U) {
        item->item_data = tree_pointer(bytes.data(), 48U);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

[[nodiscard]] int handle_tree_message(WindowSlot& tree, const std::uint32_t message,
                                      const abi::Wparam wparam, const abi::Lparam lparam,
                                      const bool wide) noexcept {
    try {
        switch (message) {
            case kTreeInsertItemA:
                return wide ? 0 : tree_insert_item(tree, lparam, false);
            case kTreeInsertItemW:
                return wide ? tree_insert_item(tree, lparam, true) : 0;
            case kTreeDeleteItem: {
                if (lparam == 0) {
                    tree.tree_items.clear();
                    tree.tree_selected = 0;
                    set_last_error(abi::kErrorSuccess);
                    return 1;
                }
                const std::uintptr_t deleted_handle = static_cast<std::uintptr_t>(lparam);
                if (tree_find_item(tree, deleted_handle) == nullptr) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                std::vector<std::uintptr_t> deleted_handles;
                deleted_handles.reserve(tree.tree_items.size());
                for (const TreeItem& candidate : tree.tree_items) {
                    if (candidate.handle == deleted_handle) {
                        deleted_handles.push_back(candidate.handle);
                        continue;
                    }
                    std::uintptr_t parent = candidate.parent;
                    for (std::size_t depth = 0; depth < tree.tree_items.size() && parent != 0U;
                         ++depth) {
                        if (parent == deleted_handle) {
                            deleted_handles.push_back(candidate.handle);
                            break;
                        }
                        const TreeItem* const ancestor = tree_find_item(tree, parent);
                        if (ancestor == nullptr) break;
                        parent = ancestor->parent;
                    }
                }
                tree.tree_items.erase(
                    std::remove_if(tree.tree_items.begin(), tree.tree_items.end(),
                                   [&deleted_handles](const TreeItem& candidate) {
                                       return std::find(deleted_handles.begin(),
                                                        deleted_handles.end(), candidate.handle) !=
                                              deleted_handles.end();
                                   }),
                    tree.tree_items.end());
                if (std::find(deleted_handles.begin(), deleted_handles.end(), tree.tree_selected) !=
                    deleted_handles.end()) {
                    tree.tree_selected = 0;
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            case kTreeExpand:
                return tree_expand_item(tree, wparam, lparam);
            case kTreeGetCount:
                set_last_error(abi::kErrorSuccess);
                return static_cast<int>(tree.tree_items.size());
            case kTreeGetNextItem:
                return tree_next_item(tree, wparam, lparam);
            case kTreeSelectItem:
                return tree_select_item(tree, lparam);
            case kTreeGetItemA:
                return wide ? 0 : tree_get_item(tree, lparam, false);
            case kTreeSetItemA:
                return wide ? 0 : tree_set_item(tree, lparam, false);
            case kTreeGetItemW:
                return wide ? tree_get_item(tree, lparam, true) : 0;
            case kTreeSetItemW:
                return wide ? tree_set_item(tree, lparam, true) : 0;
            default:
                set_last_error(abi::kErrorSuccess);
                return 0;
        }
    } catch (...) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
}

[[nodiscard]] bool translate_native_window_event(WindowSlot& slot,
                                                 const gui::WindowEvent& event,
                                                 abi::GuestMsg& message) noexcept {
    const auto set_message = [&message](const abi::HWnd hwnd, const std::uint32_t id,
                                         const abi::Wparam wparam,
                                         const abi::Lparam lparam) noexcept {
        message = {};
        message.hwnd = hwnd;
        message.message = id;
        message.wparam = wparam;
        message.lparam = lparam;
    };

    if (event.type == gui::WindowEventType::Redraw) {
        render_controls(slot);
        set_message(&slot, abi::kWmPaint, 0, 0);
        return true;
    }
    if (event.type == gui::WindowEventType::Press) {
        WindowSlot* const control = event_control(slot, event);
        handle_control_mouse(slot, event);
        slot.left_button_down = true;
        const WindowDrawingTarget target = logical_child_mouse_message(control)
                                               ? window_drawing_target(control)
                                               : WindowDrawingTarget{};
        if (logical_child_mouse_message(control) && target.native != nullptr) {
            set_message(control, abi::kWmLButtonDown, abi::kMkLButton,
                        mouse_lparam(event.x - target.offset_x, event.y - target.offset_y));
        } else {
            set_message(&slot, abi::kWmLButtonDown, abi::kMkLButton,
                        mouse_lparam(event.x, event.y));
        }
        return true;
    }
    if (event.type == gui::WindowEventType::Release) {
        WindowSlot* const control = event_control(slot, event);
        handle_control_mouse(slot, event);
        slot.left_button_down = false;
        const WindowDrawingTarget target = logical_child_mouse_message(control)
                                               ? window_drawing_target(control)
                                               : WindowDrawingTarget{};
        if (logical_child_mouse_message(control) && target.native != nullptr) {
            set_message(control, abi::kWmLButtonUp, 0,
                        mouse_lparam(event.x - target.offset_x, event.y - target.offset_y));
        } else {
            set_message(&slot, abi::kWmLButtonUp, 0, mouse_lparam(event.x, event.y));
        }
        return true;
    }
    if (event.type == gui::WindowEventType::MouseMove) {
        WindowSlot* const control = event_control(slot, event);
        handle_control_mouse(slot, event);
        const WindowDrawingTarget target = logical_child_mouse_message(control)
                                               ? window_drawing_target(control)
                                               : WindowDrawingTarget{};
        const bool deliver_to_child = logical_child_mouse_message(control) &&
                                       target.native != nullptr;
        const int local_x = deliver_to_child ? event.x - target.offset_x : event.x;
        const int local_y = deliver_to_child ? event.y - target.offset_y : event.y;
        const abi::Wparam wparam = slot.left_button_down ? abi::kMkLButton : 0;
        set_message(deliver_to_child ? control : &slot, abi::kWmMouseMove, wparam,
                    mouse_lparam(local_x, local_y));
        return true;
    }
    if (event.type == gui::WindowEventType::KeyDown) {
        slot.last_key = event.character;
        handle_control_key(slot, event);
        set_message(&slot, abi::kWmKeyDown,
                    keydown_vkey(event.keysym, event.character), 0);
        return true;
    }
    if (event.type == gui::WindowEventType::RightPress) {
        if (slot.tray_registered) {
            set_message(&slot, slot.tray_callback_message != 0
                                  ? slot.tray_callback_message
                                  : abi::kWmTrayIcon,
                        slot.tray_icon_id, abi::kWmRButtonUp);
        } else {
            set_message(&slot, abi::kWmRButtonDown, abi::kMkRButton,
                        mouse_lparam(event.x, event.y));
        }
        return true;
    }
    if (event.type == gui::WindowEventType::RightRelease) {
        if (slot.tray_registered) {
            return false;
        }
        set_message(&slot, abi::kWmRButtonUp, 0, mouse_lparam(event.x, event.y));
        return true;
    }
    if (event.type == gui::WindowEventType::KeyUp) {
        set_message(&slot, abi::kWmKeyUp,
                    keydown_vkey(event.keysym, event.character), 0);
        return true;
    }
    if (event.type == gui::WindowEventType::CloseRequested) {
        set_message(&slot, abi::kWmClose, 0, 0);
        return true;
    }
    return false;
}

void reset_pending_native(WindowSlot& slot) noexcept {
    PendingNativeMessage& pending = pending_native(slot);
    if (pending.is_timer) {
        const auto found = std::find_if(
            slot.timers.begin(), slot.timers.end(),
            [&pending](const GuestTimer& timer) {
                return timer.id == pending.message.wparam;
            });
        if (found != slot.timers.end()) {
            found->deadline = std::chrono::steady_clock::now() + found->interval;
        }
    }
    if (pending.is_paint) {
        slot.render_pending = false;
    }
    pending = {};
}

bool deliver_pending_native(void* const msg, WindowSlot& slot) noexcept {
    const PendingNativeMessage& pending = pending_native(slot);
    if (!write_guest_msg(msg, pending.message.hwnd, pending.message.message,
                         pending.message.wparam, pending.message.lparam)) {
        return false;
    }
    reset_pending_native(slot);
    return true;
}

void trace_peek_message(const abi::GuestMsg& message, const std::uint32_t remove_msg) noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "PeekMessageA"},
        diagnostics::TraceField{"message", std::to_string(message.message)},
        diagnostics::TraceField{"remove", std::to_string(remove_msg & kPmRemove)},
        diagnostics::TraceField{"status", "available"},
    };
    runtime_trace("PeekMessageA", fields, 4);
}

void trace_message_loop_idle(const void* const window) noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "GetMessageA"},
        diagnostics::TraceField{"status", "idle"},
        diagnostics::TraceField{"mechanism", "native-poll"},
        diagnostics::TraceField{"window", window == nullptr ? "all" : "filtered"},
    };
    runtime_trace("GetMessageA", fields, 4);
}

}  // namespace

void clear_pending_native(WindowSlot& slot) noexcept {
    pending_native(slot) = {};
}

extern "C" {

TL_MSABI int tl_GetMessageA(void* const msg, const void* const window,
                            const std::uint32_t filter_min,
                            const std::uint32_t filter_max) noexcept {
    if (!user32_gui_thread_allowed("GetMessageA")) {
        return -1;
    }
    if (msg == nullptr || !write_guest_msg(msg, nullptr, 0, 0, 0)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("GetMessageA", "output-message", "ponteiro sem permissão de escrita");
        return -1;
    }
    (void)filter_min;
    (void)filter_max;
    if (g_quit_requested) {
        g_quit_requested = false;
        if (!write_guest_msg(msg, nullptr, abi::kWmQuit, g_quit_code, 0)) {
            set_last_error(abi::kErrorInvalidParameter);
            return -1;
        }
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
    tl_WSAPumpAsyncSelect();
    for (WindowSlot& slot : g_windows) {
        if (!slot.used || (window != nullptr && window != &slot)) {
            continue;
        }
        if (pending_native(slot).has_message) {
            if (!deliver_pending_native(msg, slot)) {
                set_last_error(abi::kErrorInvalidParameter);
                return -1;
            }
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
        if (slot.has_pending) {
            if (!write_guest_msg(msg, &slot, slot.pending.message, slot.pending.wparam,
                                 slot.pending.lparam)) {
                set_last_error(abi::kErrorInvalidParameter);
                return -1;
            }
            slot.has_pending = false;
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
        if (!slot.queued_messages.empty()) {
            const abi::GuestMsg queued = slot.queued_messages.front();
            slot.queued_messages.pop_front();
            if (!write_guest_msg(msg, queued.hwnd, queued.message, queued.wparam, queued.lparam)) {
                set_last_error(abi::kErrorInvalidParameter);
                return -1;
            }
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
    }
    bool idle_traced = false;
    for (;;) {
        tl_WSAPumpAsyncSelect();
        CrossThreadWindowMessage cross_thread_message{};
        while (take_cross_thread_window_message(window, cross_thread_message)) {
            void* const target = reinterpret_cast<void*>(cross_thread_message.window);
            if (find_window_slot(target) == nullptr) {
                continue;
            }
            if (!write_guest_msg(msg, target, cross_thread_message.message,
                                 cross_thread_message.wparam, cross_thread_message.lparam)) {
                set_last_error(abi::kErrorInvalidParameter);
                return -1;
            }
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
        for (WindowSlot& slot : g_windows) {
            if (!slot.used || slot.native == nullptr || (window != nullptr && window != &slot)) {
                continue;
            }
            const gui::WindowEvent event = gui::platform::next_window_event(slot.native);
            if (event.type == gui::WindowEventType::Redraw) {
                render_controls(slot);
                if (!write_guest_msg(msg, &slot, abi::kWmPaint, 0, 0)) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return -1;
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::Press) {
                WindowSlot* const control = event_control(slot, event);
                handle_control_mouse(slot, event);
                slot.left_button_down = true;
                const WindowDrawingTarget target = logical_child_mouse_message(control)
                                                       ? window_drawing_target(control)
                                                       : WindowDrawingTarget{};
                if (logical_child_mouse_message(control) && target.native != nullptr) {
                    if (!write_guest_msg(msg, control, abi::kWmLButtonDown, abi::kMkLButton,
                                         mouse_lparam(event.x - target.offset_x,
                                                      event.y - target.offset_y))) {
                        set_last_error(abi::kErrorInvalidParameter);
                        return -1;
                    }
                } else {
                    if (!write_guest_msg(msg, &slot, abi::kWmLButtonDown, abi::kMkLButton,
                                         mouse_lparam(event.x, event.y))) {
                        set_last_error(abi::kErrorInvalidParameter);
                        return -1;
                    }
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::Release) {
                WindowSlot* const control = event_control(slot, event);
                handle_control_mouse(slot, event);
                slot.left_button_down = false;
                const WindowDrawingTarget target = logical_child_mouse_message(control)
                                                       ? window_drawing_target(control)
                                                       : WindowDrawingTarget{};
                if (logical_child_mouse_message(control) && target.native != nullptr) {
                    if (!write_guest_msg(msg, control, abi::kWmLButtonUp, 0,
                                         mouse_lparam(event.x - target.offset_x,
                                                      event.y - target.offset_y))) {
                        set_last_error(abi::kErrorInvalidParameter);
                        return -1;
                    }
                } else {
                    if (!write_guest_msg(msg, &slot, abi::kWmLButtonUp, 0,
                                         mouse_lparam(event.x, event.y))) {
                        set_last_error(abi::kErrorInvalidParameter);
                        return -1;
                    }
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::MouseMove) {
                WindowSlot* const control = event_control(slot, event);
                handle_control_mouse(slot, event);
                const WindowDrawingTarget target = logical_child_mouse_message(control)
                                                       ? window_drawing_target(control)
                                                       : WindowDrawingTarget{};
                const bool deliver_to_child = logical_child_mouse_message(control) &&
                                              target.native != nullptr;
                const int local_x = deliver_to_child
                                        ? event.x - target.offset_x
                                        : event.x;
                const int local_y = deliver_to_child
                                        ? event.y - target.offset_y
                                        : event.y;
                const abi::Wparam wparam = slot.left_button_down ? abi::kMkLButton : 0;
                if (!write_guest_msg(msg, deliver_to_child ? control : &slot,
                                     abi::kWmMouseMove, wparam, mouse_lparam(local_x, local_y))) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return -1;
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::KeyDown) {
                slot.last_key = event.character;
                handle_control_key(slot, event);
                if (!write_guest_msg(msg, &slot, abi::kWmKeyDown,
                                     keydown_vkey(event.keysym, event.character), 0)) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return -1;
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::RightPress) {
                if (slot.tray_registered) {
                    if (!write_guest_msg(msg, &slot,
                                         slot.tray_callback_message != 0
                                             ? slot.tray_callback_message
                                             : abi::kWmTrayIcon,
                                         slot.tray_icon_id, abi::kWmRButtonUp)) {
                        set_last_error(abi::kErrorInvalidParameter);
                        return -1;
                    }
                } else {
                    if (!write_guest_msg(msg, &slot, abi::kWmRButtonDown, abi::kMkRButton,
                                         mouse_lparam(event.x, event.y))) {
                        set_last_error(abi::kErrorInvalidParameter);
                        return -1;
                    }
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::RightRelease) {
                if (slot.tray_registered) {
                    continue;
                }
                if (!write_guest_msg(msg, &slot, abi::kWmRButtonUp, 0,
                                     mouse_lparam(event.x, event.y))) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return -1;
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::KeyUp) {
                if (!write_guest_msg(msg, &slot, abi::kWmKeyUp,
                                     keydown_vkey(event.keysym, event.character), 0)) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return -1;
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::CloseRequested) {
                if (!write_guest_msg(msg, &slot, abi::kWmClose, 0, 0)) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return -1;
                }
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
                    if (!write_guest_msg(msg, &slot, abi::kWmTimer, timer.id, 0)) {
                        set_last_error(abi::kErrorInvalidParameter);
                        return -1;
                    }
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
        for (WindowSlot& slot : g_windows) {
            if (!slot.used || !slot.render_pending ||
                (window != nullptr && window != &slot)) {
                continue;
            }
            slot.render_pending = false;
            if (!write_guest_msg(msg, &slot, abi::kWmPaint, 0, 0)) {
                set_last_error(abi::kErrorInvalidParameter);
                return -1;
            }
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
        if (!idle_traced) {
            trace_message_loop_idle(window);
            idle_traced = true;
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
    abi::GuestMsg message{};
    if (msg == nullptr || !read_guest_value(msg, message)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (message.message == abi::kWmKeyDown) {
        WindowSlot* const slot = find_window_slot(message.hwnd);
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
    abi::GuestMsg message{};
    if (msg == nullptr || !read_guest_value(msg, message)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    WindowSlot* const slot = find_window_slot(message.hwnd);
    if (slot == nullptr || slot->wndproc == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string message_id = std::to_string(message.message);
    const std::array<diagnostics::TraceField, 4> begin_fields{
        diagnostics::TraceField{"symbol", "DispatchMessageA"},
        diagnostics::TraceField{"message", message_id},
        diagnostics::TraceField{"phase", "call"},
        diagnostics::TraceField{"status", "begin"},
    };
    runtime_trace("DispatchMessageA", begin_fields, 4);
    set_last_error(abi::kErrorSuccess);
    const abi::Lresult result = call_wndproc(slot->wndproc, message.hwnd, message.message,
                                             message.wparam, message.lparam);
    if (message.message == abi::kWmPaint) {
        flush_dialog_render();
    }
    const std::array<diagnostics::TraceField, 4> end_fields{
        diagnostics::TraceField{"symbol", "DispatchMessageA"},
        diagnostics::TraceField{"message", message_id},
        diagnostics::TraceField{"result", std::to_string(static_cast<long long>(result))},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("DispatchMessageA", end_fields, 4);
    return result;
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
        if (util::ascii_iequals(slot->class_name, "SysTreeView32")) {
            return handle_tree_message(*slot, message, wparam, lparam, false);
        }
        if (message == abi::kWmSetFont) {
            return 0;
        }
        if (slot->control_kind == ControlKind::Toolbar) {
            constexpr std::uint32_t kMaxToolbarButtons = 128U;
            constexpr std::uint32_t kMaxToolbarStructSize = 64U;

            const std::array<diagnostics::TraceField, 4> message_fields{
                diagnostics::TraceField{"symbol", toolbar_message_name(message)},
                diagnostics::TraceField{"message", std::to_string(message)},
                diagnostics::TraceField{"wparam", std::to_string(wparam)},
                diagnostics::TraceField{"lparam", std::to_string(lparam)}};
            runtime_trace("ToolbarMessage", message_fields, 4);

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
                    request_dialog_render(*slot->parent);
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (message == abi::kTbAddButtons || message == abi::kTbAddButtonsW) {
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
                    count > std::numeric_limits<std::size_t>::max() / struct_size) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                const std::size_t byte_count = count * struct_size;
                std::array<std::byte, kMaxToolbarButtons * kMaxToolbarStructSize> bytes{};
                if (runtime::read_guest_memory(reinterpret_cast<const void*>(lparam), bytes.data(),
                                               byte_count).status !=
                    runtime::GuestMemoryAccessStatus::Success) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                slot->toolbar_button_struct_size = static_cast<std::uint32_t>(struct_size);
                for (std::size_t index = 0; index < count; ++index) {
                    std::int32_t command_id = 0;
                    std::memcpy(&command_id, bytes.data() + index * struct_size + sizeof(std::int32_t),
                                sizeof(command_id));
                    slot->toolbar_buttons.push_back(ToolbarButton{command_id});
                }
                const std::array<diagnostics::TraceField, 4> fields{
                    diagnostics::TraceField{"symbol", "TB_ADDBUTTONS"},
                    diagnostics::TraceField{"buttons", std::to_string(count)},
                    diagnostics::TraceField{"command-ids", toolbar_command_ids(*slot)},
                    diagnostics::TraceField{"geometry", std::to_string(slot->x) + "," +
                                                     std::to_string(slot->y) + "," +
                                                     std::to_string(slot->width) + "x" +
                                                     std::to_string(slot->height)}};
                runtime_trace("ToolbarModel", fields, 4);
                if (slot->parent != nullptr) {
                    request_dialog_render(*slot->parent);
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
                    request_dialog_render(*slot->parent);
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (message == abi::kTbAutoSize) {
                if (slot->parent != nullptr) {
                    slot->x = 0;
                    slot->y = 0;
                    slot->width = std::max(slot->parent->width, 1);
                    slot->height = std::max(slot->height, 24);
                    request_dialog_render(*slot->parent);
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (message == abi::kTbSetImageList || message == abi::kTbEnableButton) {
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        if (slot->control_kind == ControlKind::StatusBar) {
            if (message == abi::kSbSetTextA || message == abi::kSbSetTextW) {
                if (lparam == 0) {
                    slot->text.clear();
                } else {
                    std::string text_copy;
                    if (!runtime::copy_guest_cstring(reinterpret_cast<const char*>(lparam), 65535U,
                                                     text_copy)) {
                        set_last_error(abi::kErrorInvalidParameter);
                        return 0;
                    }
                    slot->text = std::move(text_copy);
                }
                if (slot->parent != nullptr) {
                    request_dialog_render(*slot->parent);
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (message == abi::kSbSetParts) {
                const std::size_t count = static_cast<std::size_t>(wparam);
                if (count > 128U || (count > 0U && lparam == 0)) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                if (count > 0U) {
                    std::array<std::int32_t, 128> parts{};
                    if (runtime::read_guest_memory(reinterpret_cast<const void*>(lparam), parts.data(),
                                                   count * sizeof(std::int32_t)).status !=
                        runtime::GuestMemoryAccessStatus::Success) {
                        set_last_error(abi::kErrorInvalidParameter);
                        return 0;
                    }
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
                    request_dialog_render(*slot->parent);
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
            if (message == 0x000C && lparam != 0) {
                std::string text_copy;
                if (!runtime::copy_guest_cstring(reinterpret_cast<const char*>(lparam), 65535U,
                                                 text_copy)) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                slot->text = std::move(text_copy);
                if (slot->parent != nullptr) {
                    request_dialog_render(*slot->parent);
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        if (slot->control_kind == ControlKind::ComboBox) {
            if (message == abi::kCbAddString && lparam != 0) {
                std::string text_copy;
                if (!runtime::copy_guest_cstring(reinterpret_cast<const char*>(lparam), 65535U,
                                                 text_copy)) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                slot->combo_items.emplace_back(std::move(text_copy));
                set_last_error(abi::kErrorSuccess);
                return static_cast<int>(slot->combo_items.size() - 1U);
            }
            if (message == abi::kCbSetCurSel) {
                slot->combo_selection = static_cast<int>(wparam);
                if (slot->parent != nullptr) {
                    request_dialog_render(*slot->parent);
                }
                return slot->combo_selection;
            }
            if (message == abi::kCbGetCurSel) {
                return slot->combo_selection;
            }
        }
        if (slot->control_kind == ControlKind::ListView ||
            util::ascii_iequals(slot->class_name, "SysListView32")) {
            return runtime_gui::handle_listview_message(*slot, message, wparam, lparam, false);
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
        if (util::ascii_iequals(slot->class_name, "SysTreeView32")) {
            return handle_tree_message(*slot, message, wparam, lparam, true);
        }
        if (slot->control_kind == ControlKind::ListView ||
            util::ascii_iequals(slot->class_name, "SysListView32")) {
            return runtime_gui::handle_listview_message(*slot, message, wparam, lparam, true);
        }
        if (slot->control_kind == ControlKind::StatusBar && message == abi::kSbSetTextW) {
            if (lparam == 0) {
                return tl_SendMessageA(window, abi::kSbSetTextA, wparam, 0);
            }
            std::u16string wide_text;
            if (!runtime::copy_guest_wstring(reinterpret_cast<const std::uint16_t*>(lparam), 65535U,
                                              wide_text)) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
            const std::string utf8 = util::wide_to_utf8(
                reinterpret_cast<const std::uint16_t*>(wide_text.data()), wide_text.size());
            return tl_SendMessageA(window, abi::kSbSetTextA, wparam,
                                   reinterpret_cast<abi::Lparam>(utf8.c_str()));
        }
        if (message == 0x000C && lparam != 0) {
            std::u16string wide_text;
            if (!runtime::copy_guest_wstring(reinterpret_cast<const std::uint16_t*>(lparam), 65535U,
                                              wide_text)) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
            const std::string utf8 = util::wide_to_utf8(
                reinterpret_cast<const std::uint16_t*>(wide_text.data()), wide_text.size());
            return tl_SendMessageA(window, message, wparam, reinterpret_cast<abi::Lparam>(utf8.c_str()));
        }
        if (message == abi::kCbAddString && lparam != 0) {
            std::u16string wide_text;
            if (!runtime::copy_guest_wstring(reinterpret_cast<const std::uint16_t*>(lparam), 65535U,
                                              wide_text)) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
            const std::string utf8 = util::wide_to_utf8(
                reinterpret_cast<const std::uint16_t*>(wide_text.data()), wide_text.size());
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
    if (g_current_thread_id != kMainThreadId) {
        if (!is_registered_window_handle(window)) {
            set_last_error(abi::kErrorInvalidHandle);
            return 0;
        }
        if (!post_cross_thread_window_message(window, message, wparam, lparam)) {
            set_last_error(abi::kErrorNotEnoughMemory);
            return 0;
        }
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
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
    if (count > 64 || count > std::numeric_limits<std::size_t>::max() / sizeof(void*) ||
        (count > 0 && handles == nullptr)) {
        set_last_error(abi::kErrorInvalidParameter);
        return abi::kWaitFailed;
    }
    std::vector<const void*> handle_copy;
    if (count > 0) {
        handle_copy.resize(count);
        if (runtime::read_guest_memory(handles, handle_copy.data(),
                                       handle_copy.size() * sizeof(handle_copy[0])).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            set_last_error(abi::kErrorInvalidParameter);
            return abi::kWaitFailed;
        }
    }
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        tl_WSAPumpAsyncSelect();
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t res = tl_WaitForSingleObject(handle_copy[i], 0);
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
    if (msg == nullptr || !write_guest_msg(msg, nullptr, 0, 0, 0)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    tl_WSAPumpAsyncSelect();
    CrossThreadWindowMessage cross_thread_message{};
    while (peek_cross_thread_window_message(window, cross_thread_message)) {
        void* const target = reinterpret_cast<void*>(cross_thread_message.window);
        if (find_window_slot(target) == nullptr) {
            CrossThreadWindowMessage discarded{};
            static_cast<void>(take_cross_thread_window_message(window, discarded));
            continue;
        }
        if ((remove_msg & kPmRemove) != 0U) {
            CrossThreadWindowMessage removed{};
            if (!take_cross_thread_window_message(window, removed)) {
                continue;
            }
            cross_thread_message = removed;
        }
        if (!write_guest_msg(msg, target, cross_thread_message.message,
                             cross_thread_message.wparam, cross_thread_message.lparam)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        return 1;
    }
    const auto matches_window = [window](const WindowSlot& candidate) noexcept {
        return candidate.used && (window == nullptr || window == &candidate);
    };
    const auto write_message = [msg, remove_msg](WindowSlot& slot,
                                                  const abi::GuestMsg& message) noexcept {
        if (!write_guest_msg(msg, message.hwnd, message.message, message.wparam,
                             message.lparam)) {
            return false;
        }
        trace_peek_message(message, remove_msg);
        if ((remove_msg & kPmRemove) != 0U && pending_native(slot).has_message) {
            reset_pending_native(slot);
        }
        return true;
    };

    for (WindowSlot& slot : g_windows) {
        if (!matches_window(slot)) {
            continue;
        }
        if (pending_native(slot).has_message) {
            if (!write_message(slot, pending_native(slot).message)) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
        if (slot.has_pending) {
            const abi::GuestMsg pending = slot.pending;
            if (!write_guest_msg(msg, pending.hwnd, pending.message, pending.wparam,
                                 pending.lparam)) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
            trace_peek_message(pending, remove_msg);
            if ((remove_msg & kPmRemove) != 0U) {
                slot.has_pending = false;
            }
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
        if (!slot.queued_messages.empty()) {
            const abi::GuestMsg queued = slot.queued_messages.front();
            if (!write_guest_msg(msg, queued.hwnd, queued.message, queued.wparam,
                                 queued.lparam)) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
            trace_peek_message(queued, remove_msg);
            if ((remove_msg & kPmRemove) != 0U) {
                slot.queued_messages.pop_front();
            }
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
    }

    for (WindowSlot& slot : g_windows) {
        if (!matches_window(slot) || slot.native == nullptr) {
            continue;
        }
        const gui::WindowEvent event = gui::platform::next_window_event(slot.native);
        if (event.type == gui::WindowEventType::Idle) {
            continue;
        }
        abi::GuestMsg translated{};
        if (!translate_native_window_event(slot, event, translated)) {
            continue;
        }
        PendingNativeMessage& pending = pending_native(slot);
        pending.message = translated;
        pending.has_message = true;
        pending.is_paint = translated.message == abi::kWmPaint;
        if (!write_message(slot, pending.message)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        set_last_error(abi::kErrorSuccess);
        return 1;
    }

    const auto now = std::chrono::steady_clock::now();
    for (WindowSlot& slot : g_windows) {
        if (!matches_window(slot)) {
            continue;
        }
        for (const GuestTimer& timer : slot.timers) {
            if (now < timer.deadline) {
                continue;
            }
            PendingNativeMessage& pending = pending_native(slot);
            pending = {};
            pending.message.hwnd = &slot;
            pending.message.message = abi::kWmTimer;
            pending.message.wparam = timer.id;
            pending.has_message = true;
            pending.is_timer = true;
            if (!write_message(slot, pending.message)) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
    }

    for (WindowSlot& slot : g_windows) {
        if (!matches_window(slot) || !slot.render_pending) {
            continue;
        }
        PendingNativeMessage& pending = pending_native(slot);
        pending = {};
        pending.message.hwnd = &slot;
        pending.message.message = abi::kWmPaint;
        pending.has_message = true;
        pending.is_paint = true;
        if (!write_message(slot, pending.message)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        set_last_error(abi::kErrorSuccess);
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
    if (key_states == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::array<std::uint8_t, 256> output{};
    if (runtime::write_guest_memory(key_states, output.data(), output.size()).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
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
    const std::uint16_t output = static_cast<std::uint16_t>(vk & 0xFF);
    if (char_out != nullptr &&
        runtime::write_guest_memory(char_out, &output, sizeof(output)).status ==
            runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    set_last_error(abi::kErrorInvalidParameter);
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
    if (result != nullptr) {
        const std::uintptr_t output = 0;
        if (runtime::write_guest_memory(result, &output, sizeof(output)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 0;
}

}  // extern "C"
}  // namespace tradutorlinux
