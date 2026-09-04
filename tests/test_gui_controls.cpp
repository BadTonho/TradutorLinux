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

}  // namespace
}  // namespace tradutorlinux::runtime_gui
