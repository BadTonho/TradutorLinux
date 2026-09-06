#include "tradutorlinux/catalog/app_catalog.hpp"
#include "tradutorlinux/backend/proton.hpp"
#include "tradutorlinux/compat/profile.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/util/sha256.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <array>

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

    void write_dll_source(const std::string_view name = "compat.dll") {
        std::ofstream output(dlls_directory(root_) / std::filesystem::path{name},
                             std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output);
        output << "not a PE yet\n";
    }

    std::filesystem::path root_;
};

void expect_rust_profile_success(const ProfileLoadResult& result) {
#if defined(TRADUTORLINUX_RUST_PROFILE_PARSER)
    EXPECT_TRUE(result.parser.attempted);
    EXPECT_EQ(result.parser.backend, ProfileParserBackend::Rust);
    EXPECT_EQ(result.parser.status, ProfileParserStatus::Success);
#else
    EXPECT_FALSE(result.parser.attempted);
#endif
}

void expect_rust_profile_malformed(const ProfileLoadResult& result) {
#if defined(TRADUTORLINUX_RUST_PROFILE_PARSER)
    EXPECT_TRUE(result.parser.attempted);
    EXPECT_EQ(result.parser.backend, ProfileParserBackend::Rust);
    EXPECT_EQ(result.parser.status, ProfileParserStatus::Malformed);
    EXPECT_NE(result.parser.code, 0U);
#else
    EXPECT_FALSE(result.parser.attempted);
#endif
}

void expect_rust_profile_unsupported(const ProfileLoadResult& result) {
#if defined(TRADUTORLINUX_RUST_PROFILE_PARSER)
    EXPECT_TRUE(result.parser.attempted);
    EXPECT_EQ(result.parser.backend, ProfileParserBackend::Rust);
    EXPECT_EQ(result.parser.status, ProfileParserStatus::UnsupportedFormat);
#else
    EXPECT_FALSE(result.parser.attempted);
#endif
}

void expect_rust_profile_not_attempted(const ProfileLoadResult& result) {
    EXPECT_FALSE(result.parser.attempted);
    EXPECT_EQ(result.parser.backend, ProfileParserBackend::NotUsed);
    EXPECT_EQ(result.parser.status, ProfileParserStatus::NotAttempted);
}

TEST_F(CompatProfileTest, PrefixCreatesPrivateCompatibilityTree) {
    const auto paths = prefix::get_environment_paths(root_);

    EXPECT_TRUE(std::filesystem::is_directory(paths.compat_dir));
    EXPECT_TRUE(std::filesystem::is_directory(paths.compat_files_dir));
    EXPECT_TRUE(std::filesystem::is_directory(paths.compat_dlls_dir));
    EXPECT_FALSE(std::filesystem::is_directory(paths.drive_c / "compat"));
}

TEST_F(CompatProfileTest, LoadsSchema2WithExplicitDllMapping) {
    write_dll_source("shim.bin");
    write_profile(R"json({
  "schema": 2,
  "app_id": "fixture",
  "files": [],
  "dlls": [{"module": "KERNEL32", "source": "shim.bin"}]
})json");

    const ProfileLoadResult result = load_profile(root_, "fixture");

    ASSERT_EQ(result.status, ProfileStatus::Loaded);
    ASSERT_EQ(result.profile.schema, 2U);
    ASSERT_EQ(result.profile.dlls.size(), 1U);
    EXPECT_EQ(result.profile.dlls[0].module, "kernel32.dll");
    EXPECT_EQ(result.profile.dlls[0].source, std::filesystem::path{"shim.bin"});
    expect_rust_profile_success(result);
}

TEST_F(CompatProfileTest, LoadsSchema3WithExplicitProtonBackend) {
    write_profile(R"json({
  "schema": 3,
  "app_id": "fixture",
  "files": [],
  "dlls": [],
  "backend": {"kind": "proton", "min_version": "11.0"}
})json");

    const ProfileLoadResult result = load_profile(root_, "fixture");

    ASSERT_EQ(result.status, ProfileStatus::Loaded);
    EXPECT_EQ(result.profile.schema, 3U);
    EXPECT_EQ(result.profile.backend.kind, BackendKind::Proton);
    EXPECT_EQ(result.profile.backend.min_version, "11.0");
    expect_rust_profile_success(result);
}

TEST_F(CompatProfileTest, Schema2CannotDeclareBackend) {
    write_profile(R"json({
  "schema": 2,
  "app_id": "fixture",
  "files": [],
  "backend": {"kind": "native"}
})json");

    const ProfileLoadResult result = load_profile(root_, "fixture");

    EXPECT_EQ(result.status, ProfileStatus::Invalid);
    EXPECT_NE(result.error.find("schema 3"), std::string::npos);
    expect_rust_profile_malformed(result);
}

TEST_F(CompatProfileTest, Schema3RejectsAutomaticBackend) {
    write_profile(R"json({
  "schema": 3,
  "app_id": "fixture",
  "files": [],
  "backend": {"kind": "auto"}
})json");

    const ProfileLoadResult result = load_profile(root_, "fixture");
    EXPECT_EQ(result.status, ProfileStatus::Invalid);
    expect_rust_profile_malformed(result);
}

TEST_F(CompatProfileTest, UnknownSchemaUsesGenericFallback) {
    write_profile(R"json({
  "schema": 99,
  "app_id": "fixture",
  "files": []
})json");

    const ProfileLoadResult result = load_profile(root_, "fixture");

    EXPECT_EQ(result.status, ProfileStatus::Invalid);
    expect_rust_profile_unsupported(result);
}

TEST_F(CompatProfileTest, NativeBackendRejectsMinimumVersion) {
    write_profile(R"json({
  "schema": 3,
  "app_id": "fixture",
  "files": [],
  "backend": {"kind": "native", "min_version": "11.0"}
})json");

    const ProfileLoadResult result = load_profile(root_, "fixture");

    EXPECT_EQ(result.status, ProfileStatus::Invalid);
    EXPECT_NE(result.error.find("backend proton"), std::string::npos);
    expect_rust_profile_malformed(result);
}

TEST_F(CompatProfileTest, Schema1CannotDeclareDlls) {
    write_profile(R"json({
  "schema": 1,
  "app_id": "fixture",
  "files": [],
  "dlls": [{"module": "compat.dll", "source": "compat.dll"}]
})json");

    const ProfileLoadResult result = load_profile(root_, "fixture");

    EXPECT_EQ(result.status, ProfileStatus::Invalid);
    EXPECT_NE(result.error.find("schema 2"), std::string::npos);
    expect_rust_profile_malformed(result);
}

TEST_F(CompatProfileTest, DuplicateDllModulesAreRejectedCaseInsensitively) {
    write_profile(R"json({
  "schema": 2,
  "app_id": "fixture",
  "files": [],
  "dlls": [
    {"module": "compat.dll", "source": "one.dll"},
    {"module": "COMPAT", "source": "two.dll"}
  ]
})json");

    const ProfileLoadResult result = load_profile(root_, "fixture");

    EXPECT_EQ(result.status, ProfileStatus::Invalid);
    EXPECT_NE(result.error.find("duplicado"), std::string::npos);
    expect_rust_profile_malformed(result);
}

TEST_F(CompatProfileTest, MissingDllSourceRemainsAProviderFailure) {
    write_profile(R"json({
  "schema": 2,
  "app_id": "fixture",
  "files": [],
  "dlls": [{"module": "compat.dll", "source": "missing.dll"}]
})json");

    const ProfileLoadResult result = load_profile(root_, "fixture");

    ASSERT_EQ(result.status, ProfileStatus::Loaded);
    ASSERT_EQ(result.profile.dlls.size(), 1U);
    expect_rust_profile_success(result);
}

TEST_F(CompatProfileTest, MissingProfileFallsBackWithoutLoading) {
    const ProfileLoadResult result = load_profile(root_, "fixture");

    EXPECT_EQ(result.status, ProfileStatus::Missing);
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.profile.files.empty());
    expect_rust_profile_not_attempted(result);
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
    expect_rust_profile_success(result);
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
    expect_rust_profile_malformed(result);
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
    expect_rust_profile_success(result);
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
    expect_rust_profile_malformed(result);
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
    expect_rust_profile_malformed(result);
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
    expect_rust_profile_malformed(result);
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

    const ProfileLoadResult wrong_id = load_profile(root_, "other");
    const ProfileLoadResult wrong_hash = load_profile(
        root_, "fixture", std::string(64, 'b'), "1.2.3");
    const ProfileLoadResult wrong_version = load_profile(
        root_, "fixture", std::string(64, 'a'), "9.9.9");
    EXPECT_EQ(wrong_id.status, ProfileStatus::Invalid);
    EXPECT_EQ(wrong_hash.status, ProfileStatus::Invalid);
    EXPECT_EQ(wrong_version.status, ProfileStatus::Invalid);
    expect_rust_profile_malformed(wrong_id);
    expect_rust_profile_malformed(wrong_hash);
    expect_rust_profile_malformed(wrong_version);
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

namespace backend {
namespace {

using namespace ::tradutorlinux::backend;

class ProtonFixture : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() /
                ("tl-proton-config-" +
                 std::to_string(static_cast<unsigned long long>(::getpid())));
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_ / "files" / "bin");
        std::filesystem::create_directories(root_ / "files" / "share" / "wine");
        std::filesystem::create_directories(root_ / "files" / "share" / "default_pfx");
        write_file(root_ / "proton", "#!/bin/sh\n");
        write_file(root_ / "version", "11.0-1\n");
        write_elf(root_ / "files" / "bin" / "wine");
        write_elf(root_ / "files" / "bin" / "wineserver");
        write_file(root_ / "files" / "share" / "wine" / "wine.inf", "[Version]\n");
    }

    void TearDown() override {
        std::filesystem::remove_all(root_);
    }

    void write_file(const std::filesystem::path& path, const std::string_view contents) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output);
        output << contents;
        output.close();
        std::error_code ec;
        std::filesystem::permissions(
            path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                      std::filesystem::perms::owner_exec,
            std::filesystem::perm_options::replace, ec);
        ASSERT_FALSE(ec);
    }

    void write_elf(const std::filesystem::path& path) {
        std::array<unsigned char, 20> header{};
        header[0] = 0x7fU;
        header[1] = 'E';
        header[2] = 'L';
        header[3] = 'F';
        header[4] = 2U;
        header[5] = 1U;
        header[18] = 62U;
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output);
        output.write(reinterpret_cast<const char*>(header.data()),
                     static_cast<std::streamsize>(header.size()));
        output.close();
        std::error_code ec;
        std::filesystem::permissions(
            path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                      std::filesystem::perms::owner_exec,
            std::filesystem::perm_options::replace, ec);
        ASSERT_FALSE(ec);
    }

    std::filesystem::path root_;
};

TEST_F(ProtonFixture, ValidatesModernX8664Installation) {
    const ProtonValidationResult result = validate_proton(ProtonConfig{root_, {}});

    ASSERT_TRUE(result.valid) << result.error;
    EXPECT_EQ(result.version, "11.0-1");
}

TEST_F(ProtonFixture, AcceptsSteamExperimentalVersionFormat) {
    write_file(root_ / "version", "1788532552 experimental-11.0-20260903b-x86_64\n");

    const ProtonValidationResult result = validate_proton(ProtonConfig{root_, {}}, "11.0");

    ASSERT_TRUE(result.valid) << result.error;
    EXPECT_EQ(result.version, "1788532552 experimental-11.0-20260903b-x86_64");
}

TEST_F(ProtonFixture, RejectsMissingRequiredComponent) {
    std::filesystem::remove(root_ / "files" / "bin" / "wineserver");

    const ProtonValidationResult result = validate_proton(ProtonConfig{root_, {}});

    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.error.find("componentes obrigatórios"), std::string::npos);
}

TEST_F(ProtonFixture, RejectsArchitectureAndVersionMismatch) {
    write_file(root_ / "files" / "bin" / "wine", "not an ELF\n");
    const ProtonValidationResult architecture_result =
        validate_proton(ProtonConfig{root_, {}});
    EXPECT_FALSE(architecture_result.valid);
    EXPECT_NE(architecture_result.error.find("ELF x86-64"), std::string::npos);

    write_elf(root_ / "files" / "bin" / "wine");
    const ProtonValidationResult version_result =
        validate_proton(ProtonConfig{root_, {}}, "12.0");
    EXPECT_FALSE(version_result.valid);
    EXPECT_NE(version_result.error.find("versão mínima"), std::string::npos);
}

TEST_F(ProtonFixture, RejectsConfiguredHashMismatch) {
    const ProtonValidationResult result =
        validate_proton(ProtonConfig{root_, std::string(64U, '0')});

    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.error.find("hash da instalação"), std::string::npos);
    EXPECT_EQ(result.fingerprint.size(), 64U);
}

TEST(ProtonConfigTest, ParsesStrictExternalConfiguration) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("tl-proton-config-file-" +
                       std::to_string(static_cast<unsigned long long>(::getpid())));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto config_path = root / "backends.json";
    {
        std::ofstream output(config_path);
        ASSERT_TRUE(output);
        output << "{\"schema\":1,\"proton\":{\"root\":\"" <<
            root.generic_string() << "\",\"sha256\":\"" << std::string(64U, 'A') <<
            "\"}}";
    }

    const ProtonConfigResult result = load_config_file(config_path);

    ASSERT_EQ(result.status, ConfigStatus::Loaded);
    ASSERT_TRUE(result.config.has_value());
    EXPECT_EQ(result.config->root, root);
    EXPECT_EQ(result.config->sha256, std::string(64U, 'A'));
    std::filesystem::remove_all(root);
}

TEST(ProtonConfigTest, RejectsUnknownConfigurationField) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("tl-proton-config-invalid-" +
                       std::to_string(static_cast<unsigned long long>(::getpid())));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto config_path = root / "backends.json";
    {
        std::ofstream output(config_path);
        ASSERT_TRUE(output);
        output << "{\"schema\":1,\"proton\":{\"root\":\"/tmp/proton\"},\"extra\":true}";
    }

    const ProtonConfigResult result = load_config_file(config_path);

    EXPECT_EQ(result.status, ConfigStatus::Invalid);
    EXPECT_NE(result.error.find("desconhecido"), std::string::npos);
    std::filesystem::remove_all(root);
}

}  // namespace
}  // namespace backend

}  // namespace
}  // namespace tradutorlinux::compat
