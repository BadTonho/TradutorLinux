#include "test_support.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/process.hpp"
#include "tradutorlinux/process/isolate.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include <gtest/gtest.h>

namespace tradutorlinux::loader {
namespace {

using tradutorlinux::test_support::maps_permissions_for;

std::vector<std::byte> read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    EXPECT_TRUE(input.good()) << "fixture não encontrada: " << path;
    std::vector<std::byte> bytes;
    char byte = '\0';
    while (input.get(byte)) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
    }
    return bytes;
}

std::string fixture_path(const std::string& name) {
    return std::string(TL_FIXTURE_OUTPUT_DIRECTORY) + "/" + name + ".exe";
}

class ProcessTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_modules();
        register_builtin_modules();
        page = sysconf(_SC_PAGESIZE) > 0 ? static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE))
                                         : 0x1000ULL;
    }

    void TearDown() override {
        clear_modules();
    }

    std::uint64_t page{0x1000};
};

TEST_F(ProcessTest, PreparesNopProcessWithStackAndGuardPage) {
    const std::vector<std::byte> bytes = read_file(fixture_path("tl_nop"));
    const pe::ParseResult parse_result = pe::parse_pe(bytes);
    ASSERT_EQ(parse_result.status, pe::ParseStatus::Success);

    PrepareResult result = prepare_process(parse_result.info, bytes);
    ASSERT_EQ(result.status, PrepareStatus::Success);
    ASSERT_EQ(result.process.thread.entry_point,
              result.process.image.base +
                  static_cast<std::uint64_t>(result.process.info.address_of_entry_point));
    EXPECT_EQ(result.process.imports.status, ImportStatus::Resolved);
    EXPECT_TRUE(result.process.imports.imports.empty());

    const GuestThread& thread = result.process.thread;
    ASSERT_GT(thread.stack_top, 0U);
    EXPECT_EQ(thread.stack_size, kGuestStackSize);
    EXPECT_EQ(thread.stack_top % 16, 0U);

    const std::uintptr_t first_usable =
        static_cast<std::uintptr_t>(thread.stack_top - thread.stack_size);
    const std::uintptr_t guard_page =
        static_cast<std::uintptr_t>(thread.stack_top - thread.stack_size - page);
    EXPECT_EQ(maps_permissions_for(first_usable), "rw-p");
    EXPECT_EQ(maps_permissions_for(guard_page), "---p");

    destroy_process(result.process);
    EXPECT_EQ(result.process.image.memory, nullptr);
    EXPECT_EQ(result.process.stack, nullptr);
    EXPECT_TRUE(result.process.imports.imports.empty());
}

TEST_F(ProcessTest, PreparesHelloWithAllImportsResolved) {
    const std::vector<std::byte> bytes = read_file(fixture_path("tl_hello"));
    const pe::ParseResult parse_result = pe::parse_pe(bytes);
    ASSERT_EQ(parse_result.status, pe::ParseStatus::Success);

    PrepareResult result = prepare_process(parse_result.info, bytes);
    ASSERT_EQ(result.status, PrepareStatus::Success);
    ASSERT_EQ(result.process.imports.imports.size(), 3U);
    for (const ResolvedImport& entry : result.process.imports.imports) {
        EXPECT_EQ(entry.status, ImportStatus::Resolved);
        EXPECT_FALSE(entry.by_ordinal);
    }

    destroy_process(result.process);
}

TEST_F(ProcessTest, RejectsUnknownSymbolWithUnresolvedImports) {
    const std::vector<std::byte> bytes = read_file(fixture_path("tl_missing_dll"));
    const pe::ParseResult parse_result = pe::parse_pe(bytes);
    ASSERT_EQ(parse_result.status, pe::ParseStatus::Success);

    PrepareResult result = prepare_process(parse_result.info, bytes);
    ASSERT_EQ(result.status, PrepareStatus::UnresolvedImports);
    EXPECT_FALSE(result.error_message.empty());
    ASSERT_EQ(result.process.imports.imports.size(), 1U);
    EXPECT_EQ(result.process.imports.imports[0].status, ImportStatus::UnknownSymbol);

    destroy_process(result.process);
}

TEST_F(ProcessTest, DestroyProcessClearsState) {
    const std::vector<std::byte> bytes = read_file(fixture_path("tl_nop"));
    const pe::ParseResult parse_result = pe::parse_pe(bytes);
    ASSERT_EQ(parse_result.status, pe::ParseStatus::Success);

    PrepareResult result = prepare_process(parse_result.info, bytes);
    ASSERT_EQ(result.status, PrepareStatus::Success);
    const std::uint64_t image_base = result.process.image.base;
    ASSERT_GT(image_base, 0U);

    destroy_process(result.process);
    EXPECT_EQ(result.process.image.memory, nullptr);
    EXPECT_EQ(result.process.image.base, 0U);
    EXPECT_EQ(result.process.stack, nullptr);
    EXPECT_EQ(result.process.stack_size, 0U);
    EXPECT_TRUE(result.process.imports.imports.empty());
    EXPECT_EQ(result.process.thread.stack_top, 0U);
    EXPECT_EQ(result.process.thread.entry_point, 0U);
}

TEST(ExternalProcessTest, PreservesExitAndForwardsStreamsAndEnvironment) {
    const auto output_path = std::filesystem::temp_directory_path() /
                             ("tl-external-output-" +
                              std::to_string(static_cast<unsigned long long>(::getpid())));
    std::error_code cleanup_error;
    std::filesystem::remove(output_path, cleanup_error);
    std::ostringstream diagnostics;
    const std::vector<std::string> argv{
        "/bin/sh", "-c",
        "printf '%s' \"$TL_EXTERNAL_TEST_VALUE\" > \"$TL_EXTERNAL_TEST_OUTPUT\"; printf 'mock stderr\\n' >&2; exit 37"};
    const process::GuestOutcome outcome = process::run_external_isolated(
        argv,
        {{"TL_EXTERNAL_TEST_VALUE", "environment-ok"},
         {"TL_EXTERNAL_TEST_OUTPUT", output_path.string()}},
        1000, {}, {}, diagnostics, "[test] ");

    ASSERT_EQ(outcome.kind, process::GuestOutcomeKind::Exited);
    EXPECT_EQ(outcome.exit_code, 37U);
    EXPECT_EQ(diagnostics.str(), "[test] mock stderr\n");
    std::ifstream output(output_path, std::ios::binary);
    ASSERT_TRUE(output);
    const std::string output_contents{std::istreambuf_iterator<char>{output},
                                      std::istreambuf_iterator<char>()};
    EXPECT_EQ(output_contents, "environment-ok");
    std::filesystem::remove(output_path, cleanup_error);
}

TEST(ExternalProcessTest, TerminatesTimedOutProcessGroup) {
    std::ostringstream diagnostics;
    const std::vector<std::string> argv{"/bin/sh", "-c", "sleep 5"};
    const process::GuestOutcome outcome = process::run_external_isolated(
        argv, {}, 50, {}, {}, diagnostics);
    EXPECT_EQ(outcome.kind, process::GuestOutcomeKind::TimedOut);
}

TEST(ExternalProcessTest, ReportsSignalTermination) {
    std::ostringstream diagnostics;
    const std::vector<std::string> argv{"/bin/sh", "-c", "kill -TERM $$"};
    const process::GuestOutcome outcome = process::run_external_isolated(
        argv, {}, 1000, {}, {}, diagnostics);
    EXPECT_EQ(outcome.kind, process::GuestOutcomeKind::Signaled);
    EXPECT_EQ(outcome.signal_number, SIGTERM);
}

TEST(ExternalProcessTest, NetworkIsolationNoneDisablesNetworkAccess) {
    std::ostringstream diagnostics;
    const std::vector<std::string> argv{
        "/bin/sh", "-c",
        "python3 -c \""
        "import socket, sys\n"
        "s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)\n"
        "try:\n"
        "    s.connect(('8.8.8.8', 53))\n"
        "    sys.exit(0)\n"
        "except OSError as e:\n"
        "    if e.errno == 101: sys.exit(42)\n"
        "    sys.exit(1)\n"
        "\""
    };
    const process::GuestOutcome outcome = process::run_external_isolated(
        argv, {}, 2000, {}, {}, diagnostics, "[test] ", process::NetworkMode::None);
    ASSERT_EQ(outcome.kind, process::GuestOutcomeKind::Exited);
    EXPECT_EQ(outcome.exit_code, 42U);
}

TEST(ExternalProcessTest, NetworkIsolationLoopbackAllowsLocalLoopback) {
    std::ostringstream diagnostics;
    const std::vector<std::string> argv{
        "/bin/sh", "-c",
        "python3 -c \""
        "import socket, sys\n"
        "s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)\n"
        "try:\n"
        "    s.bind(('127.0.0.1', 0))\n"
        "    s.listen(1)\n"
        "    sys.exit(55)\n"
        "except Exception:\n"
        "    sys.exit(1)\n"
        "\""
    };
    const process::GuestOutcome outcome = process::run_external_isolated(
        argv, {}, 2000, {}, {}, diagnostics, "[test] ", process::NetworkMode::Loopback);
    ASSERT_EQ(outcome.kind, process::GuestOutcomeKind::Exited);
    EXPECT_EQ(outcome.exit_code, 55U);
}

}  // namespace
}  // namespace tradutorlinux::loader
