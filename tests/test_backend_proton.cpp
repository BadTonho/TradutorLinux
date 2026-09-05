#include "tradutorlinux/backend/proton.hpp"

#include "tradutorlinux/compat/profile.hpp"
#include "tradutorlinux/prefix/prefix.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <iterator>

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

namespace tradutorlinux::backend {
namespace {

class ProtonRuntimeTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() /
                ("tl-proton-runtime-" +
                 std::to_string(static_cast<unsigned long long>(::getpid())));
        std::filesystem::remove_all(root_);
        ASSERT_TRUE(prefix::initialize_prefix(root_));

        proton_root_ = root_ / "fake-proton";
        ASSERT_TRUE(std::filesystem::create_directories(
            proton_root_ / "files" / "bin"));
        ASSERT_TRUE(std::filesystem::create_directories(
            proton_root_ / "files" / "share" / "wine"));
        ASSERT_TRUE(std::filesystem::create_directories(
            proton_root_ / "files" / "share" / "default_pfx"));
        write_text(proton_root_ / "version", "11.0-1\n");
        write_elf(proton_root_ / "files" / "bin" / "wine");
        write_elf(proton_root_ / "files" / "bin" / "wineserver");
        write_text(proton_root_ / "files" / "share" / "wine" / "wine.inf", "fake\n");

        launcher_output_ = root_ / "launcher-output.txt";
        write_text(proton_root_ / "proton",
                   "#!/bin/sh\n"
                   "printf 'arg1=%s\\narg2=%s\\ncompat=%s\\nwineprefix=%s\\ninstall=%s\\n' "
                   "\"$1\" \"$2\" \"$STEAM_COMPAT_DATA_PATH\" \"$WINEPREFIX\" "
                   "\"$STEAM_COMPAT_INSTALL_PATH\" > \"$TL_PROTON_TEST_OUTPUT\"\n"
                   "printf 'mock proton stderr\\n' >&2\n"
                   "exit 37\n");
        ASSERT_EQ(::chmod((proton_root_ / "proton").c_str(), 0755), 0);
        ASSERT_EQ(::setenv("TL_PROTON_TEST_OUTPUT", launcher_output_.c_str(), 1), 0);

        const auto app_dir = root_ / "drive_c" / "Program Files" / "Probe";
        ASSERT_TRUE(std::filesystem::create_directories(app_dir));
        write_text(app_dir / "probe.exe", "not-a-real-pe-for-staging\n");
        write_text(app_dir / "data.txt", "application-data\n");
        write_text(compat::files_directory(root_) / "helper.dat", "profile-helper\n");
    }

    void TearDown() override {
        ::unsetenv("TL_PROTON_TEST_OUTPUT");
        std::filesystem::remove_all(root_);
    }

    static void write_text(const std::filesystem::path& path, const std::string& value) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output);
        output << value;
        ASSERT_TRUE(output);
    }

    static void write_elf(const std::filesystem::path& path) {
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
        ASSERT_TRUE(output);
        ASSERT_EQ(::chmod(path.c_str(), 0755), 0);
    }

    static std::string read_file(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        EXPECT_TRUE(input);
        return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }

    ProtonConfig config() const { return ProtonConfig{proton_root_, {}}; }

    std::filesystem::path root_;
    std::filesystem::path proton_root_;
    std::filesystem::path launcher_output_;
};

TEST_F(ProtonRuntimeTest, StagesRunsAndKeepsApplicationTree) {
    compat::Profile profile;
    profile.schema = 3;
    profile.app_id = "probe";
    profile.backend.kind = compat::BackendKind::Proton;
    profile.files.push_back({"helper.dat", "C:\\Program Files\\Probe\\helper.dat"});

    ProtonRunRequest request;
    request.prefix_root = root_;
    request.executable = root_ / "drive_c" / "Program Files" / "Probe" / "probe.exe";
    request.working_directory = request.executable.parent_path();
    request.app_id = "probe";
    request.trace_enabled = true;

    std::ostringstream diagnostics;
    const ProtonRunResult result = run_proton_application(
        config(), profile, request, diagnostics);

    ASSERT_EQ(result.status, ProtonRunStatus::Completed) << result.error;
    ASSERT_EQ(result.outcome.kind, process::GuestOutcomeKind::Exited);
    EXPECT_EQ(result.outcome.exit_code, 37U);
    EXPECT_EQ(result.version, "11.0-1");
    EXPECT_EQ(read_file(launcher_output_),
              "arg1=run\narg2=" + result.staged_executable.string() +
                  "\ncompat=" + (root_ / "proton" / "compatdata").string() +
                  "\nwineprefix=" + (root_ / "proton" / "compatdata" / "pfx").string() +
                  "\ninstall=" + (root_ / "proton").string() + "\n");
    EXPECT_NE(diagnostics.str().find("[tl][proton] mock proton stderr\n"), std::string::npos);
    EXPECT_NE(diagnostics.str().find("provider-selected"), std::string::npos);
    EXPECT_NE(diagnostics.str().find("staging-complete"), std::string::npos);
    EXPECT_NE(diagnostics.str().find("files-cleanup"), std::string::npos);

    EXPECT_EQ(read_file(root_ / "drive_c" / "Program Files" / "Probe" / "probe.exe"),
              "not-a-real-pe-for-staging\n");
    EXPECT_EQ(read_file(root_ / "proton" / "compatdata" / "pfx" / "drive_c" /
                        "Program Files" / "Probe" / "probe.exe"),
              "not-a-real-pe-for-staging\n");
    EXPECT_EQ(read_file(root_ / "proton" / "compatdata" / "pfx" / "drive_c" /
                        "Program Files" / "Probe" / "data.txt"),
              "application-data\n");
    EXPECT_TRUE(std::filesystem::exists(compat::files_directory(root_) / "helper.dat"));
    EXPECT_FALSE(std::filesystem::exists(root_ / "proton" / "compatdata" / "pfx" /
                                         "drive_c" / "Program Files" / "Probe" / "helper.dat"));
    EXPECT_FALSE(std::filesystem::exists(root_ / "proton" / "compat"));
    EXPECT_TRUE(std::filesystem::is_regular_file(root_ / "proton" / "application-manifest.json"));
}

TEST_F(ProtonRuntimeTest, RejectsNativeDllExtensionsBeforePreparation) {
    compat::Profile profile;
    profile.schema = 3;
    profile.app_id = "probe";
    profile.backend.kind = compat::BackendKind::Proton;
    profile.dlls.push_back({"custom.dll", "custom.dll"});

    ProtonRunRequest request;
    request.prefix_root = root_;
    request.executable = root_ / "drive_c" / "Program Files" / "Probe" / "probe.exe";
    request.app_id = "probe";

    std::ostringstream diagnostics;
    const ProtonRunResult result = run_proton_application(
        config(), profile, request, diagnostics);
    EXPECT_EQ(result.status, ProtonRunStatus::Unsupported);
    EXPECT_NE(result.error.find("dlls"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(root_ / "proton"));
}

}  // namespace
}  // namespace tradutorlinux::backend
