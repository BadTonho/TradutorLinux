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

}  // namespace
}  // namespace tradutorlinux::runtime_gui
