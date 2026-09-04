#include "../src/runtime/gui_controls.hpp"

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

}  // namespace
}  // namespace tradutorlinux::runtime_gui
