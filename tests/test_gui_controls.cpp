#include "../src/runtime/gui_controls.hpp"
#include "../src/runtime/core/runtime_context.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>

namespace tradutorlinux::runtime_gui {
namespace {

struct TestTreeItemA {
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
static_assert(sizeof(TestTreeItemA) == 56U);

struct TestTreeInsertA {
    void* parent{};
    void* insert_after{};
    TestTreeItemA item{};
};
static_assert(sizeof(TestTreeInsertA) == 72U);

constexpr std::uint32_t kTestTreeInsertItemA = 0x1100U;
constexpr std::uint32_t kTestTreeGetCount = 0x1105U;
constexpr std::uint32_t kTestTreeGetNextItem = 0x110AU;
constexpr std::uint32_t kTestTreeSelectItem = 0x110BU;
constexpr std::uint32_t kTestTreeGetItemA = 0x110CU;
constexpr std::uint32_t kTestTreeGetNext = 1U;
constexpr std::uint32_t kTestTreeGetCaret = 9U;
constexpr std::uint32_t kTestTreeItemText = 0x0001U;
constexpr std::uint32_t kTestTreeItemParam = 0x0004U;


TEST(WindowDrawingTarget, ProjectsNestedLogicalChildIntoTopLevelSurface) {
    g_windows = {};
    WindowSlot& parent = g_windows[0];
    parent.used = true;
    parent.native = reinterpret_cast<gui::NativeWindow>(0x1234U);

    WindowSlot& child = g_windows[1];
    child.used = true;
    child.is_control = true;
    child.parent = &parent;
    child.x = 12;
    child.y = 34;

    WindowSlot& nested = g_windows[2];
    nested.used = true;
    nested.is_control = true;
    nested.parent = &child;
    nested.x = 5;
    nested.y = 7;

    const WindowDrawingTarget target = window_drawing_target(&nested);

    EXPECT_EQ(target.native, parent.native);
    EXPECT_EQ(target.offset_x, 17);
    EXPECT_EQ(target.offset_y, 41);
    g_windows = {};
}

TEST(WindowDrawingTarget, RejectsUnknownOrDetachedWindow) {
    g_windows = {};
    WindowSlot& detached = g_windows[0];
    detached.used = true;
    detached.is_control = true;
    detached.x = 1;
    detached.y = 2;

    EXPECT_EQ(window_drawing_target(&detached).native, nullptr);
    EXPECT_EQ(window_drawing_target(reinterpret_cast<void*>(0x4321U)).native, nullptr);
    g_windows = {};
}

TEST(LogicalControls, HitTestReturnsTopmostVisibleChild) {
    g_windows = {};
    WindowSlot& parent = g_windows[0];
    parent.used = true;

    WindowSlot& first = g_windows[1];
    first.used = true;
    first.is_control = true;
    first.parent = &parent;
    first.x = 10;
    first.y = 20;
    first.width = 100;
    first.height = 40;

    WindowSlot& second = g_windows[2];
    second.used = true;
    second.is_control = true;
    second.parent = &parent;
    second.x = 40;
    second.y = 30;
    second.width = 100;
    second.height = 40;

    EXPECT_EQ(find_control_at(parent, std::span<WindowSlot>{g_windows}, 50, 40), &second);
    second.visible = false;
    EXPECT_EQ(find_control_at(parent, std::span<WindowSlot>{g_windows}, 50, 40), &first);
    EXPECT_EQ(find_control_at(parent, std::span<WindowSlot>{g_windows}, 500, 400), nullptr);
    g_windows = {};
}

TEST(CommonControls, CreateStatusAndToolbarAsLogicalChildren) {
    g_windows = {};
    WindowSlot& parent = g_windows[0];
    parent.used = true;
    parent.native = reinterpret_cast<gui::NativeWindow>(0x1234U);
    parent.width = 640;
    parent.height = 480;

    constexpr std::uint16_t status_text[] = {'R', 'e', 'a', 'd', 'y', 0};
    void* const status = tl_CreateStatusWindowW(0, status_text, &parent, 900);
    ASSERT_NE(status, nullptr);
    WindowSlot* const status_slot = find_window_slot(status);
    ASSERT_NE(status_slot, nullptr);
    EXPECT_EQ(status_slot->control_kind, ControlKind::StatusBar);
    EXPECT_EQ(status_slot->parent, &parent);
    EXPECT_EQ(status_slot->y, 456);
    EXPECT_EQ(status_slot->width, 640);
    EXPECT_EQ(status_slot->text, "Ready");
    constexpr std::uint16_t updated_status[] = {'O', 'K', 0};
    EXPECT_EQ(tl_SendMessageW(status, abi::kSbSetTextW, 0,
                              reinterpret_cast<abi::Lparam>(updated_status)),
              1);
    EXPECT_EQ(status_slot->text, "OK");

    struct TestToolbarButton {
        std::int32_t bitmap;
        std::int32_t command_id;
    } buttons[]{{0, 101}, {1, 202}};
    void* const toolbar = tl_CreateToolbarEx(&parent, 0, 901, 0, nullptr, 0, buttons, 2, 80, 32,
                                             16, 16, sizeof(TestToolbarButton));
    ASSERT_NE(toolbar, nullptr);
    WindowSlot* const toolbar_slot = find_window_slot(toolbar);
    ASSERT_NE(toolbar_slot, nullptr);
    EXPECT_EQ(toolbar_slot->control_kind, ControlKind::Toolbar);
    ASSERT_EQ(toolbar_slot->toolbar_buttons.size(), 2U);
    EXPECT_EQ(toolbar_slot->toolbar_buttons[0].command_id, 101);
    EXPECT_EQ(toolbar_slot->toolbar_buttons[1].command_id, 202);
    EXPECT_EQ(toolbar_slot->toolbar_button_width, 80);
    EXPECT_EQ(toolbar_slot->height, 32);
    EXPECT_EQ(toolbar_slot->width, 640);
    EXPECT_EQ(tl_SendMessageA(toolbar, abi::kTbButtonCount, 0, 0), 2);
    g_windows = {};
}

TEST(CommonControls, RejectInvalidToolbarBufferAndParent) {
    g_windows = {};
    EXPECT_EQ(tl_CreateToolbarEx(nullptr, 0, 1, 0, nullptr, 0, nullptr, 1, 16, 16, 16, 16, 8),
              nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    WindowSlot& parent = g_windows[0];
    parent.used = true;
    parent.width = 640;
    parent.height = 480;
    EXPECT_EQ(tl_CreateToolbarEx(&parent, 0, 1, 0, nullptr, 0, nullptr, 1, 16, 16, 16, 16, 8),
              nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    g_windows = {};
}

TEST(CommonControls, ToolbarClickQueuesCommandWithButtonId) {
    g_windows = {};
    WindowSlot& parent = g_windows[0];
    parent.used = true;
    parent.width = 100;
    parent.height = 50;

    WindowSlot& toolbar = g_windows[1];
    toolbar.used = true;
    toolbar.is_control = true;
    toolbar.control_kind = ControlKind::Toolbar;
    toolbar.parent = &parent;
    toolbar.width = 100;
    toolbar.height = 24;
    toolbar.toolbar_button_width = 40;
    toolbar.toolbar_buttons = {{101}, {202}};

    WindowSlot* focused = nullptr;
    handle_control_mouse(parent, std::span<WindowSlot>{g_windows}, focused,
                         gui::WindowEvent{gui::WindowEventType::Press, 60, 10});
    handle_control_mouse(parent, std::span<WindowSlot>{g_windows}, focused,
                         gui::WindowEvent{gui::WindowEventType::Release, 60, 10});

    ASSERT_EQ(parent.queued_messages.size(), 1U);
    const abi::GuestMsg& message = parent.queued_messages.front();
    EXPECT_EQ(message.message, abi::kWmCommand);
    EXPECT_EQ(message.wparam & 0xFFFFU, 202U);
    EXPECT_EQ(message.wparam >> 16U, 0U);
    EXPECT_EQ(message.lparam, reinterpret_cast<abi::Lparam>(&toolbar));
    g_windows = {};
}

TEST(CommonControls, UnicodeToolbarAddButtonsBuildsLogicalModel) {
    g_windows = {};
    WindowSlot& parent = g_windows[0];
    parent.used = true;
    parent.width = 320;
    parent.height = 120;

    WindowSlot& toolbar = g_windows[1];
    toolbar.used = true;
    toolbar.is_control = true;
    toolbar.control_kind = ControlKind::Toolbar;
    toolbar.parent = &parent;
    toolbar.width = 320;
    toolbar.height = 1;

    struct TestToolbarButton {
        std::int32_t bitmap;
        std::int32_t command_id;
    } buttons[]{{0, 540}, {1, 546}};

    ASSERT_EQ(tl_SendMessageA(&toolbar, abi::kTbButtonStructSize, sizeof(TestToolbarButton), 0),
              1);
    ASSERT_EQ(tl_SendMessageW(&toolbar, abi::kTbAddButtonsW, 2,
                              reinterpret_cast<abi::Lparam>(buttons)),
              1);
    ASSERT_EQ(toolbar.toolbar_buttons.size(), 2U);
    EXPECT_EQ(toolbar.toolbar_buttons[0].command_id, 540);
    EXPECT_EQ(toolbar.toolbar_buttons[1].command_id, 546);
    ASSERT_EQ(tl_SendMessageW(&toolbar, abi::kTbAutoSize, 0, 0), 1);
    EXPECT_EQ(toolbar.width, 320);
    EXPECT_EQ(toolbar.height, 24);
    g_windows = {};
}

TEST(CommonControls, EnterActivatesDefaultButtonForRegularWindow) {
    g_windows = {};
    g_focused_control = nullptr;

    WindowSlot& parent = g_windows[0];
    parent.used = true;

    WindowSlot& edit = g_windows[1];
    edit.used = true;
    edit.is_control = true;
    edit.parent = &parent;
    edit.control_kind = ControlKind::Edit;
    edit.visible = true;
    edit.enabled = true;

    WindowSlot& secondary = g_windows[2];
    secondary.used = true;
    secondary.is_control = true;
    secondary.parent = &parent;
    secondary.control_kind = ControlKind::Button;
    secondary.control_id = 200;
    secondary.visible = true;
    secondary.enabled = true;
    secondary.style = 0x00010000U;

    WindowSlot& primary = g_windows[3];
    primary.used = true;
    primary.is_control = true;
    primary.parent = &parent;
    primary.control_kind = ControlKind::Button;
    primary.control_id = 100;
    primary.visible = true;
    primary.enabled = true;
    primary.style = 0x00000001U;

    WindowSlot* focused = &edit;
    handle_control_key(parent, std::span<WindowSlot>{g_windows}, focused,
                       gui::WindowEvent{gui::WindowEventType::KeyDown, 0, 0, '\0', 0xFF0DUL});

    ASSERT_EQ(parent.queued_messages.size(), 1U);
    EXPECT_EQ(parent.queued_messages.front().message, abi::kWmCommand);
    EXPECT_EQ(parent.queued_messages.front().wparam & 0xFFFFU, 100U);
    EXPECT_EQ(parent.queued_messages.front().wparam >> 16U, 0U);
    EXPECT_EQ(parent.queued_messages.front().lparam,
              reinterpret_cast<abi::Lparam>(&primary));

    parent.queued_messages.clear();
    focused = &secondary;
    handle_control_key(parent, std::span<WindowSlot>{g_windows}, focused,
                       gui::WindowEvent{gui::WindowEventType::KeyDown, 0, 0, '\0', 0xFF0DUL});
    ASSERT_EQ(parent.queued_messages.size(), 1U);
    EXPECT_EQ(parent.queued_messages.front().wparam & 0xFFFFU, 200U);
    EXPECT_EQ(parent.queued_messages.front().lparam,
              reinterpret_cast<abi::Lparam>(&secondary));

    g_windows = {};
    g_focused_control = nullptr;
}

TEST(CommonControls, TabCyclesFocusableRegularControls) {
    g_windows = {};
    g_focused_control = nullptr;

    WindowSlot& parent = g_windows[0];
    parent.used = true;

    WindowSlot& edit = g_windows[1];
    edit.used = true;
    edit.is_control = true;
    edit.parent = &parent;
    edit.control_kind = ControlKind::Edit;
    edit.visible = true;
    edit.enabled = true;
    edit.focused = true;

    WindowSlot& combo = g_windows[2];
    combo.used = true;
    combo.is_control = true;
    combo.parent = &parent;
    combo.control_kind = ControlKind::ComboBox;
    combo.visible = true;
    combo.enabled = true;

    WindowSlot& static_control = g_windows[3];
    static_control.used = true;
    static_control.is_control = true;
    static_control.parent = &parent;
    static_control.control_kind = ControlKind::Static;
    static_control.visible = true;
    static_control.enabled = true;

    WindowSlot& button = g_windows[4];
    button.used = true;
    button.is_control = true;
    button.parent = &parent;
    button.control_kind = ControlKind::Button;
    button.visible = true;
    button.enabled = true;
    button.style = 0x00010000U;

    WindowSlot* focused = &edit;
    const gui::WindowEvent tab{gui::WindowEventType::KeyDown, 0, 0, '\0', 0xFF09UL};
    handle_control_key(parent, std::span<WindowSlot>{g_windows}, focused, tab);
    EXPECT_EQ(focused, &combo);
    EXPECT_FALSE(edit.focused);
    EXPECT_TRUE(combo.focused);

    handle_control_key(parent, std::span<WindowSlot>{g_windows}, focused, tab);
    EXPECT_EQ(focused, &button);
    EXPECT_FALSE(combo.focused);
    EXPECT_TRUE(button.focused);

    handle_control_key(parent, std::span<WindowSlot>{g_windows}, focused, tab);
    EXPECT_EQ(focused, &edit);
    EXPECT_FALSE(button.focused);
    EXPECT_TRUE(edit.focused);
    EXPECT_EQ(parent.queued_messages.size(), 6U);

    g_windows = {};
    g_focused_control = nullptr;
}

TEST(CommonControls, TreeViewMaintainsItemsAndSelection) {
    g_windows = {};
    g_focused_control = nullptr;

    WindowSlot& parent = g_windows[0];
    parent.used = true;
    WindowSlot& tree = g_windows[1];
    tree.used = true;
    tree.is_control = true;
    tree.class_name = "SysTreeView32";
    tree.control_kind = ControlKind::Generic;
    tree.parent = &parent;
    tree.control_id = 1008;

    char session_text[] = "Session";
    TestTreeInsertA session{};
    session.item.mask = kTestTreeItemText | kTestTreeItemParam;
    session.item.text = session_text;
    session.item.item_data = 0x1111;
    const int session_handle = tl_SendMessageA(
        &tree, kTestTreeInsertItemA, 0,
        reinterpret_cast<abi::Lparam>(&session));
    ASSERT_GT(session_handle, 0);

    char ssh_text[] = "SSH";
    TestTreeInsertA ssh{};
    ssh.parent = nullptr;
    ssh.insert_after = reinterpret_cast<void*>(static_cast<std::uintptr_t>(session_handle));
    ssh.item.mask = kTestTreeItemText | kTestTreeItemParam;
    ssh.item.text = ssh_text;
    ssh.item.item_data = 0x2222;
    const int ssh_handle = tl_SendMessageA(
        &tree, kTestTreeInsertItemA, 0,
        reinterpret_cast<abi::Lparam>(&ssh));
    ASSERT_GT(ssh_handle, session_handle);

    ASSERT_EQ(tl_SendMessageA(&tree, kTestTreeGetCount, 0, 0), 2);
    EXPECT_EQ(tl_SendMessageA(&tree, kTestTreeGetNextItem, 0, 0), session_handle);
    EXPECT_EQ(tl_SendMessageA(&tree, kTestTreeGetNextItem, kTestTreeGetNext,
                              static_cast<abi::Lparam>(session_handle)),
              ssh_handle);
    EXPECT_EQ(tl_SendMessageA(&tree, kTestTreeSelectItem, 0,
                              static_cast<abi::Lparam>(session_handle)),
              1);
    EXPECT_EQ(tree.tree_selected, static_cast<std::uintptr_t>(session_handle));
    EXPECT_EQ(tl_SendMessageA(&tree, kTestTreeSelectItem, 0,
                              static_cast<abi::Lparam>(ssh_handle)),
              1);
    EXPECT_EQ(tl_SendMessageA(&tree, kTestTreeGetNextItem, kTestTreeGetCaret, 0),
              ssh_handle);

    ASSERT_EQ(tree.tree_items.size(), 2U);
    EXPECT_EQ(tree.tree_items[0].text, "Session");
    EXPECT_EQ(tree.tree_items[0].item_data, 0x1111U);
    EXPECT_EQ(tree.tree_items[1].text, "SSH");
    EXPECT_EQ(tree.tree_items[1].item_data, 0x2222U);

    char output[16]{};
    TestTreeItemA item_query{};
    item_query.mask = kTestTreeItemText | kTestTreeItemParam;
    item_query.h_item = reinterpret_cast<void*>(static_cast<std::uintptr_t>(ssh_handle));
    item_query.text = output;
    item_query.text_capacity = static_cast<std::int32_t>(sizeof(output));
    ASSERT_EQ(tl_SendMessageA(&tree, kTestTreeGetItemA, 0,
                              reinterpret_cast<abi::Lparam>(&item_query)),
              1);
    EXPECT_STREQ(output, "SSH");
    EXPECT_EQ(item_query.item_data, 0x2222);

    g_windows = {};
    g_focused_control = nullptr;
}

TEST(CommonControls, TreeViewRejectsUnmappedInputAndOutputBuffers) {
    g_windows = {};
    WindowSlot& tree = g_windows[0];
    tree.used = true;
    tree.is_control = true;
    tree.class_name = "SysTreeView32";
    tree.control_kind = ControlKind::Generic;

    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    EXPECT_EQ(tl_SendMessageA(&tree, kTestTreeInsertItemA, 0,
                              reinterpret_cast<abi::Lparam>(invalid)), 0);

    TestTreeInsertA insert{};
    insert.item.mask = kTestTreeItemText;
    insert.item.text = static_cast<char*>(invalid);
    insert.item.text_capacity = 8;
    EXPECT_EQ(tl_SendMessageA(&tree, kTestTreeInsertItemA, 0,
                              reinterpret_cast<abi::Lparam>(&insert)), 0);

    char label[] = "Node";
    insert.item.text = label;
    ASSERT_GT(tl_SendMessageA(&tree, kTestTreeInsertItemA, 0,
                              reinterpret_cast<abi::Lparam>(&insert)), 0);

    TestTreeItemA query{};
    query.mask = kTestTreeItemText;
    query.h_item = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1U));
    query.text = static_cast<char*>(invalid);
    query.text_capacity = 8;
    EXPECT_EQ(tl_SendMessageA(&tree, kTestTreeGetItemA, 0,
                              reinterpret_cast<abi::Lparam>(&query)), 0);

    g_windows = {};
}

TEST(CommonControls, ToolbarMessagesBuildLogicalButtonModel) {
    g_windows = {};
    WindowSlot& parent = g_windows[0];
    parent.used = true;
    parent.width = 640;
    parent.height = 480;
    WindowSlot* const toolbar = create_logical_control(
        parent, "ToolbarWindow32", {}, 0, 901, 0, 0, 640, 36);
    ASSERT_NE(toolbar, nullptr);

    struct TestToolbarButton {
        std::int32_t bitmap;
        std::int32_t command_id;
    } buttons[]{{0, 303}, {1, 404}};
    EXPECT_EQ(tl_SendMessageA(toolbar, abi::kTbButtonStructSize, sizeof(TestToolbarButton), 0), 1);
    EXPECT_EQ(tl_SendMessageA(toolbar, abi::kTbAddButtons, 2,
                              reinterpret_cast<abi::Lparam>(buttons)),
              1);
    EXPECT_EQ(tl_SendMessageA(toolbar, abi::kTbButtonCount, 0, 0), 2);
    ASSERT_EQ(toolbar->toolbar_buttons.size(), 2U);
    EXPECT_EQ(toolbar->toolbar_buttons[0].command_id, 303);
    EXPECT_EQ(toolbar->toolbar_buttons[1].command_id, 404);
    EXPECT_EQ(tl_SendMessageA(toolbar, abi::kTbDeleteButton, 0, 0), 1);
    EXPECT_EQ(tl_SendMessageA(toolbar, abi::kTbButtonCount, 0, 0), 1);
    g_windows = {};
}

}  // namespace
}  // namespace tradutorlinux::runtime_gui
