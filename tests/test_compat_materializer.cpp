#include "tradutorlinux/compat/materializer.hpp"
#include "tradutorlinux/prefix/prefix.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include <gtest/gtest.h>
#include <unistd.h>

namespace tradutorlinux::compat {
namespace {

class FileExposureTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() /
                ("tl-compat-exposure-" +
                 std::to_string(static_cast<unsigned long long>(::getpid())));
        std::filesystem::remove_all(root_);
        ASSERT_TRUE(prefix::initialize_prefix(root_));
    }

    void TearDown() override {
        std::filesystem::remove_all(root_);
    }

    void write_source(const std::string_view name, const std::string_view contents) {
        std::ofstream output(files_directory(root_) / std::filesystem::path{name},
                             std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output);
        output << contents;
    }

    [[nodiscard]] std::string read_file(const std::filesystem::path& path) const {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }

    std::filesystem::path root_;
};

TEST_F(FileExposureTest, CopiesFilesIntoDriveCAndCleansCreatedPaths) {
    write_source("fixture.dat", "profile payload\n");
    Profile profile;
    profile.files.push_back(
        {"fixture.dat", "C:\\Program Files\\Fixture\\compat.dat"});

    FileExposure exposure = FileExposure::materialize(root_, profile);
    ASSERT_TRUE(exposure.applied()) << exposure.error();

    const auto target = root_ / "drive_c" / "Program Files" / "Fixture" / "compat.dat";
    EXPECT_EQ(read_file(target), "profile payload\n");
    EXPECT_TRUE(std::filesystem::exists(files_directory(root_) / "fixture.dat"));
    EXPECT_FALSE(std::filesystem::exists(root_ / "drive_c" / "compat"));
    ASSERT_TRUE(exposure.cleanup());
    EXPECT_FALSE(std::filesystem::exists(target));
    EXPECT_FALSE(std::filesystem::exists(target.parent_path()));
    EXPECT_TRUE(std::filesystem::exists(files_directory(root_) / "fixture.dat"));
    EXPECT_EQ(exposure.cleanup_summary().files_removed, 1U);
}

TEST_F(FileExposureTest, CreatesMultipleParentsAndKeepsPreexistingDirectory) {
    const auto existing = root_ / "drive_c" / "Existing";
    ASSERT_TRUE(std::filesystem::create_directory(existing));
    write_source("first.dat", "first\n");
    write_source("second.dat", "second\n");
    Profile profile;
    profile.files = {
        {"first.dat", "C:\\Existing\\Nested\\A\\first.dat"},
        {"second.dat", "C:\\Existing\\Nested\\B\\second.dat"},
    };

    FileExposure exposure = FileExposure::materialize(root_, profile);
    ASSERT_TRUE(exposure.applied()) << exposure.error();
    EXPECT_EQ(exposure.files().size(), 2U);
    EXPECT_TRUE(std::filesystem::exists(existing / "Nested" / "A" / "first.dat"));
    EXPECT_TRUE(std::filesystem::exists(existing / "Nested" / "B" / "second.dat"));

    ASSERT_TRUE(exposure.cleanup());
    EXPECT_TRUE(std::filesystem::is_directory(existing));
    EXPECT_FALSE(std::filesystem::exists(existing / "Nested"));
}

TEST_F(FileExposureTest, RejectsExistingDestinationWithoutChangingIt) {
    const auto target = root_ / "drive_c" / "Fixture" / "existing.dat";
    ASSERT_TRUE(std::filesystem::create_directories(target.parent_path()));
    {
        std::ofstream output(target);
        ASSERT_TRUE(output);
        output << "original\n";
    }
    write_source("replacement.dat", "replacement\n");
    Profile profile;
    profile.files.push_back({"replacement.dat", "C:\\Fixture\\existing.dat"});

    FileExposure exposure = FileExposure::materialize(root_, profile);
    EXPECT_FALSE(exposure.applied());
    EXPECT_EQ(exposure.status(), FileExposureStatus::Rejected);
    EXPECT_NE(exposure.error().find("já existe"), std::string_view::npos);
    EXPECT_EQ(read_file(target), "original\n");
}

TEST_F(FileExposureTest, RejectsSourceSymlink) {
    write_source("real.dat", "source\n");
    const auto link = files_directory(root_) / "link.dat";
    std::error_code symlink_error;
    std::filesystem::create_symlink("real.dat", link, symlink_error);
    ASSERT_FALSE(symlink_error);
    Profile profile;
    profile.files.push_back({"link.dat", "C:\\Fixture\\link.dat"});

    FileExposure exposure = FileExposure::materialize(root_, profile);
    EXPECT_EQ(exposure.status(), FileExposureStatus::Rejected);
    EXPECT_NE(exposure.error().find("symlink"), std::string_view::npos);
    EXPECT_FALSE(std::filesystem::exists(root_ / "drive_c" / "Fixture"));
}

TEST_F(FileExposureTest, RejectsSourceContainingNulBeforeMaterialization) {
    const std::string source_name{"bad\0name", 8U};
    Profile profile;
    profile.files.push_back(
        {std::filesystem::path{source_name}, "C:\\Fixture\\nul.dat"});

    FileExposure exposure = FileExposure::materialize(root_, profile);

    EXPECT_EQ(exposure.status(), FileExposureStatus::Rejected);
    EXPECT_FALSE(std::filesystem::exists(root_ / "drive_c" / "Fixture"));
}

TEST_F(FileExposureTest, RejectsDestinationSymlink) {
    const auto destination_dir = root_ / "drive_c" / "Fixture";
    ASSERT_TRUE(std::filesystem::create_directories(destination_dir));
    const auto original = destination_dir / "original.dat";
    {
        std::ofstream output(original);
        ASSERT_TRUE(output);
        output << "original\n";
    }
    std::error_code symlink_error;
    std::filesystem::create_symlink("original.dat", destination_dir / "link.dat",
                                    symlink_error);
    ASSERT_FALSE(symlink_error);
    write_source("source.dat", "source\n");
    Profile profile;
    profile.files.push_back({"source.dat", "C:\\Fixture\\link.dat"});

    FileExposure exposure = FileExposure::materialize(root_, profile);
    EXPECT_EQ(exposure.status(), FileExposureStatus::Rejected);
    EXPECT_NE(exposure.error().find("symlink"), std::string_view::npos);
    EXPECT_EQ(read_file(original), "original\n");
}

TEST_F(FileExposureTest, RejectsSymlinkInDestinationParent) {
    const auto real_directory = root_ / "drive_c" / "Real";
    ASSERT_TRUE(std::filesystem::create_directories(real_directory));
    std::error_code symlink_error;
    std::filesystem::create_directory_symlink("Real", root_ / "drive_c" / "Alias",
                                              symlink_error);
    ASSERT_FALSE(symlink_error);
    write_source("source.dat", "source\n");
    Profile profile;
    profile.files.push_back({"source.dat", "C:\\Alias\\injected.dat"});

    FileExposure exposure = FileExposure::materialize(root_, profile);
    EXPECT_EQ(exposure.status(), FileExposureStatus::Rejected);
    EXPECT_NE(exposure.error().find("symlink"), std::string_view::npos);
    EXPECT_FALSE(std::filesystem::exists(real_directory / "injected.dat"));
}

TEST_F(FileExposureTest, RollsBackEarlierCopiesWhenLaterMappingFails) {
    write_source("first.dat", "first\n");
    write_source("second.dat", "second\n");
    Profile profile;
    profile.files = {
        {"first.dat", "C:\\Rollback\\Directory\\first.dat"},
        {"second.dat", "C:\\Rollback\\Directory"},
    };

    FileExposure exposure = FileExposure::materialize(root_, profile);
    EXPECT_EQ(exposure.status(), FileExposureStatus::Rejected);
    EXPECT_FALSE(exposure.rollback_failed()) << exposure.error();
    EXPECT_FALSE(std::filesystem::exists(root_ / "drive_c" / "Rollback"));
    EXPECT_EQ(exposure.cleanup_summary().files_removed, 1U);
}

TEST_F(FileExposureTest, CleanupRemovesModifiedStagedFile) {
    write_source("fixture.dat", "before\n");
    Profile profile;
    profile.files.push_back({"fixture.dat", "C:\\Fixture\\modified.dat"});
    FileExposure exposure = FileExposure::materialize(root_, profile);
    ASSERT_TRUE(exposure.applied()) << exposure.error();

    const auto target = root_ / "drive_c" / "Fixture" / "modified.dat";
    {
        std::ofstream output(target, std::ios::binary | std::ios::app);
        ASSERT_TRUE(output);
        output << "after\n";
    }
    ASSERT_TRUE(exposure.cleanup());
    EXPECT_FALSE(std::filesystem::exists(target));
}

TEST_F(FileExposureTest, CleanupDoesNotRemoveReplacementAtStagedPath) {
    write_source("fixture.dat", "profile\n");
    Profile profile;
    profile.files.push_back({"fixture.dat", "C:\\Fixture\\replacement.dat"});
    FileExposure exposure = FileExposure::materialize(root_, profile);
    ASSERT_TRUE(exposure.applied()) << exposure.error();

    const auto target = root_ / "drive_c" / "Fixture" / "replacement.dat";
    ASSERT_TRUE(std::filesystem::remove(target));
    {
        std::ofstream output(target);
        ASSERT_TRUE(output);
        output << "guest data\n";
    }
    ASSERT_TRUE(exposure.cleanup());
    EXPECT_EQ(read_file(target), "guest data\n");
    EXPECT_GE(exposure.cleanup_summary().paths_retained, 1U);
}

}  // namespace
}  // namespace tradutorlinux::compat
