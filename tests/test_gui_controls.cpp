#include "../src/runtime/gui_controls.hpp"
#include "../src/runtime/core/runtime_context.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>

namespace tradutorlinux::runtime_gui {
namespace {

class SevenZipDirectoryRowsTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory_ = std::filesystem::temp_directory_path() /
                     ("tradutorlinux-gui-rows-" + std::to_string(getpid()) + "-" +
                      std::to_string(stamp));
        std::error_code error;
        ASSERT_TRUE(std::filesystem::create_directories(directory_, error));
        ASSERT_FALSE(error);
    }

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    std::filesystem::path directory_;
};

TEST_F(SevenZipDirectoryRowsTest, ListsOnlyImmediateEntriesWithDirectoriesFirst) {
    ASSERT_TRUE(std::filesystem::create_directory(directory_ / "Folder"));
    std::ofstream file(directory_ / "archive.7z");
    file << "abc";
    file.close();
    ASSERT_TRUE(file);
    ASSERT_TRUE(std::filesystem::create_directory(directory_ / "Folder" / "Nested"));

    const std::vector<ListViewRow> rows = collect_seven_zip_directory_rows(directory_);

    ASSERT_EQ(rows.size(), 3U);
    ASSERT_EQ(rows[0].columns, (std::vector<std::string>{"..", "<DIR>"}));
    ASSERT_EQ(rows[1].columns, (std::vector<std::string>{"Folder", "<DIR>"}));
    ASSERT_EQ(rows[2].columns, (std::vector<std::string>{"archive.7z", "3"}));
}

TEST_F(SevenZipDirectoryRowsTest, IncludesParentAndHonorsRowLimit) {
    std::ofstream file(directory_ / "one.txt");
    file << "1";
    file.close();
    ASSERT_TRUE(file);
    std::ofstream second_file(directory_ / "two.txt");
    second_file << "22";
    second_file.close();
    ASSERT_TRUE(second_file);

    const std::vector<ListViewRow> rows = collect_seven_zip_directory_rows(directory_, 2);

    ASSERT_EQ(rows.size(), 2U);
    ASSERT_EQ(rows[0].columns, (std::vector<std::string>{"..", "<DIR>"}));
}

TEST(SevenZipDirectoryRows, InvalidDirectoryReturnsNoRows) {
    const auto missing = std::filesystem::temp_directory_path() /
                         "tradutorlinux-gui-rows-does-not-exist";
    const std::vector<ListViewRow> rows = collect_seven_zip_directory_rows(missing);
    EXPECT_TRUE(rows.empty());
}

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

TEST(CommonControls, SevenZipVisualToolbarUsesGuestCommandOrder) {
    g_windows = {};
    WindowSlot& parent = g_windows[0];
    parent.used = true;
    parent.class_name = "7-Zip::FM";
    parent.width = 800;
    parent.height = 600;

    WindowSlot& toolbar = g_windows[1];
    toolbar.used = true;
    toolbar.is_control = true;
    toolbar.control_kind = ControlKind::Toolbar;
    toolbar.parent = &parent;
    toolbar.visible = true;
    toolbar.toolbar_buttons = {{1070}, {1071}, {1072}, {546}, {547}, {548}, {551}};

    EXPECT_EQ(find_control_at(parent, std::span<WindowSlot>{g_windows}, 20, 45), &toolbar);
    EXPECT_EQ(find_control_at(parent, std::span<WindowSlot>{g_windows}, 20, 90), nullptr);

    WindowSlot* focused = nullptr;
    handle_control_mouse(parent, std::span<WindowSlot>{g_windows}, focused,
                         gui::WindowEvent{gui::WindowEventType::Press, 20, 45});
    handle_control_mouse(parent, std::span<WindowSlot>{g_windows}, focused,
                         gui::WindowEvent{gui::WindowEventType::Release, 20, 45});

    ASSERT_EQ(parent.queued_messages.size(), 1U);
    EXPECT_EQ(parent.queued_messages.front().message, abi::kWmCommand);
    EXPECT_EQ(parent.queued_messages.front().wparam & 0xFFFFU, 1070U);
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
