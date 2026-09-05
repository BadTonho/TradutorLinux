#include "tradutorlinux/catalog/app_catalog.hpp"
#include "tradutorlinux/compat/profile.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/util/sha256.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>
#include <unistd.h>

namespace tradutorlinux::compat {
namespace {

class CompatProfileTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() /
                ("tl-compat-profile-" + std::to_string(static_cast<unsigned long long>(::getpid())));
        std::filesystem::remove_all(root_);
        ASSERT_TRUE(prefix::initialize_prefix(root_));
    }

    void TearDown() override {
        std::filesystem::remove_all(root_);
    }

    void write_profile(const std::string_view contents) {
        std::ofstream output(profile_path(root_), std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output);
        output << contents;
    }

    void write_source(const std::string_view name = "fixture.dat") {
        std::ofstream output(files_directory(root_) / std::filesystem::path{name},
                             std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output);
        output << "compatibility fixture\n";
    }

    std::filesystem::path root_;
};

TEST_F(CompatProfileTest, PrefixCreatesPrivateCompatibilityTree) {
    const auto paths = prefix::get_environment_paths(root_);

    EXPECT_TRUE(std::filesystem::is_directory(paths.compat_dir));
    EXPECT_TRUE(std::filesystem::is_directory(paths.compat_files_dir));
    EXPECT_FALSE(std::filesystem::is_directory(paths.drive_c / "compat"));
}

TEST_F(CompatProfileTest, MissingProfileFallsBackWithoutLoading) {
    const ProfileLoadResult result = load_profile(root_, "fixture");

    EXPECT_EQ(result.status, ProfileStatus::Missing);
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.profile.files.empty());
}

TEST_F(CompatProfileTest, LoadsStrictProfileWithOptionalIdentity) {
    write_source();
    write_profile(R"json(
{
  "schema": 1,
  "app_id": "fixture",
  "app_sha256": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
  "app_version": "1.2.3",
  "files": [
    {"source": "fixture.dat", "target": "C:\\Program Files\\Fixture\\compat.dat"}
  ]
}
)json");

    const ProfileLoadResult result = load_profile(
        root_, "fixture",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "1.2.3");

    ASSERT_EQ(result.status, ProfileStatus::Loaded);
    ASSERT_EQ(result.profile.schema, 1U);
    EXPECT_EQ(result.profile.app_id, "fixture");
    EXPECT_EQ(result.profile.app_sha256,
              "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    EXPECT_EQ(result.profile.app_version, "1.2.3");
    ASSERT_EQ(result.profile.files.size(), 1U);
    EXPECT_EQ(result.profile.files[0].source, std::filesystem::path{"fixture.dat"});
    EXPECT_EQ(result.profile.files[0].target, "C:\\Program Files\\Fixture\\compat.dat");
}

TEST_F(CompatProfileTest, UnknownFieldInvalidatesWholeProfile) {
    write_source();
    write_profile(R"json({
  "schema": 1,
  "app_id": "fixture",
  "unexpected": true,
  "files": []
})json");

    const ProfileLoadResult result = load_profile(root_, "fixture");

    EXPECT_EQ(result.status, ProfileStatus::Invalid);
    EXPECT_NE(result.error.find("desconhecido"), std::string::npos);
}

TEST_F(CompatProfileTest, MissingSourceInvalidatesWholeProfile) {
    write_profile(R"json({
  "schema": 1,
  "app_id": "fixture",
  "files": [{"source": "missing.dat", "target": "C:\\Fixture\\missing.dat"}]
})json");

    const ProfileLoadResult result = load_profile(root_, "fixture");

    EXPECT_EQ(result.status, ProfileStatus::Invalid);
    EXPECT_NE(result.error.find("ausente"), std::string::npos);
}

TEST_F(CompatProfileTest, SourceTraversalInvalidatesWholeProfile) {
    const auto outside = root_.parent_path() / "tl-compat-outside.dat";
    {
        std::ofstream output(outside, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output);
        output << "outside\n";
    }
    write_profile(R"json({
  "schema": 1,
  "app_id": "fixture",
  "files": [{"source": "../tl-compat-outside.dat", "target": "C:\\Fixture\\file.dat"}]
})json");

    const ProfileLoadResult result = load_profile(root_, "fixture");

    EXPECT_EQ(result.status, ProfileStatus::Invalid);
    EXPECT_NE(result.error.find("relativa"), std::string::npos);
    std::filesystem::remove(outside);
}

TEST_F(CompatProfileTest, TargetOutsideDriveCInvalidatesWholeProfile) {
    write_source();
    write_profile(R"json({
  "schema": 1,
  "app_id": "fixture",
  "files": [{"source": "fixture.dat", "target": "C:\\..\\outside.dat"}]
})json");

    const ProfileLoadResult result = load_profile(root_, "fixture");

    EXPECT_EQ(result.status, ProfileStatus::Invalid);
    EXPECT_NE(result.error.find("drive_c"), std::string::npos);
}

TEST_F(CompatProfileTest, DuplicateMappingsInvalidatesWholeProfile) {
    write_source("first.dat");
    write_source("second.dat");
    write_profile(R"json({
  "schema": 1,
  "app_id": "fixture",
  "files": [
    {"source": "first.dat", "target": "C:\\Fixture\\same.dat"},
    {"source": "second.dat", "target": "c:/fixture/same.dat"}
  ]
})json");

    const ProfileLoadResult result = load_profile(root_, "fixture");

    EXPECT_EQ(result.status, ProfileStatus::Invalid);
    EXPECT_NE(result.error.find("duplicado"), std::string::npos);
}

TEST_F(CompatProfileTest, IdentityMismatchFallsBackToGenericProfile) {
    write_source();
    write_profile(R"json({
  "schema": 1,
  "app_id": "fixture",
  "app_sha256": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
  "app_version": "1.2.3",
  "files": [{"source": "fixture.dat", "target": "C:\\Fixture\\file.dat"}]
})json");

    EXPECT_EQ(load_profile(root_, "other").status, ProfileStatus::Invalid);
    EXPECT_EQ(load_profile(root_, "fixture", std::string(64, 'b'), "1.2.3").status,
              ProfileStatus::Invalid);
    EXPECT_EQ(load_profile(root_, "fixture", std::string(64, 'a'), "9.9.9").status,
              ProfileStatus::Invalid);
}

TEST(Sha256Test, ProducesKnownDigest) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("tl-sha256-" + std::to_string(static_cast<unsigned long long>(::getpid())));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto file = root / "abc.txt";
    {
        std::ofstream output(file, std::ios::binary);
        ASSERT_TRUE(output);
        output << "abc";
    }

    const auto digest = util::sha256_file(file);

    ASSERT_TRUE(digest.has_value());
    EXPECT_EQ(*digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::filesystem::remove_all(root);
}

TEST(AppCatalogProfileTest, PersistsOptionalApplicationIdentity) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("tl-catalog-profile-" + std::to_string(static_cast<unsigned long long>(::getpid())));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto catalog_path = root / "library.json";

    catalog::AppCatalog saved;
    catalog::AppEntry entry;
    entry.id = "fixture";
    entry.name = "Fixture";
    entry.executable_path = "/tmp/fixture.exe";
    entry.prefix_path = (root / "prefix").string();
    entry.app_sha256 = std::string(64, 'a');
    entry.app_version = "1.2.3";
    ASSERT_TRUE(saved.add_app(entry));
    ASSERT_TRUE(saved.save_to_file(catalog_path));

    catalog::AppCatalog loaded;
    ASSERT_TRUE(loaded.load_from_file(catalog_path));
    const auto result = loaded.find_app("fixture");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->app_sha256, std::string(64, 'a'));
    EXPECT_EQ(result->app_version, "1.2.3");
    std::filesystem::remove_all(root);
}

}  // namespace
}  // namespace tradutorlinux::compat
