#include "tradutorlinux/cli.hpp"

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace tradutorlinux {
namespace {

[[nodiscard]] ParseResult parse_arguments(const std::vector<const char*>& arguments) {
    return parse_command_line(static_cast<int>(arguments.size()), arguments.data());
}

TEST(CommandLineTest, AcceptsTraceAndExecutable) {
    const std::vector<const char*> arguments{"tradutorlinux", "--trace", "programa.exe"};

    const ParseResult result = parse_arguments(arguments);

    ASSERT_TRUE(result.command_line.has_value());
    EXPECT_TRUE(result.command_line->trace_enabled);
    ASSERT_TRUE(result.command_line->executable_path.has_value());
    EXPECT_EQ(result.command_line->executable_path->string(), "programa.exe");
}

TEST(CommandLineTest, AcceptsJsonTraceDirectory) {
    const std::vector<const char*> arguments{
        "tradutorlinux", "--trace-json", "/tmp/tl-events", "programa.exe"};

    const ParseResult result = parse_arguments(arguments);

    ASSERT_TRUE(result.command_line.has_value());
    ASSERT_TRUE(result.command_line->trace_json_directory.has_value());
    EXPECT_EQ(result.command_line->trace_json_directory->string(), "/tmp/tl-events");
}

TEST(CommandLineTest, AcceptsReportAndExecutable) {
    const std::vector<const char*> arguments{"tradutorlinux", "--report", "programa.exe"};

    const ParseResult result = parse_arguments(arguments);

    ASSERT_TRUE(result.command_line.has_value());
    EXPECT_TRUE(result.command_line->report_only);
    ASSERT_TRUE(result.command_line->executable_path.has_value());
}

TEST(CommandLineTest, AcceptsInstallWithExplicitExecutableAndPrefix) {
    const std::vector<const char*> arguments{
        "tradutorlinux", "install", "setup.exe", "--name", "Aplicativo de teste", "--prefix",
        "/tmp/tl-prefix", "--app-exe", "C:\\Program Files\\Teste\\app.exe", "--timeout", "2",
        "--trace=install"};

    const ParseResult result = parse_arguments(arguments);

    ASSERT_TRUE(result.command_line.has_value());
    EXPECT_EQ(result.command_line->mode, CommandMode::Install);
    EXPECT_EQ(result.command_line->app_name, "Aplicativo de teste");
    ASSERT_TRUE(result.command_line->executable_path.has_value());
    EXPECT_EQ(result.command_line->executable_path->string(), "setup.exe");
    ASSERT_TRUE(result.command_line->custom_prefix.has_value());
    EXPECT_EQ(result.command_line->custom_prefix->string(), "/tmp/tl-prefix");
    ASSERT_TRUE(result.command_line->installed_executable_path.has_value());
    EXPECT_EQ(result.command_line->installed_executable_path->string(),
              "C:\\Program Files\\Teste\\app.exe");
    EXPECT_EQ(result.command_line->timeout_ms, 2000U);
    ASSERT_EQ(result.command_line->trace_channels_raw.size(), 1U);
    EXPECT_EQ(result.command_line->trace_channels_raw.front(), "install");
}

TEST(CommandLineTest, RejectsUnknownOption) {
    const std::vector<const char*> arguments{"tradutorlinux", "--invalida"};

    const ParseResult result = parse_arguments(arguments);

    EXPECT_FALSE(result.command_line.has_value());
    EXPECT_EQ(result.error_message, "opção desconhecida: --invalida");
}

TEST(CommandLineTest, AcceptsTimeoutOption) {
    const std::vector<const char*> arguments{"tradutorlinux", "--timeout", "5", "programa.exe"};

    const ParseResult result = parse_arguments(arguments);

    ASSERT_TRUE(result.command_line.has_value());
    EXPECT_EQ(result.command_line->timeout_ms, 5000);
    EXPECT_TRUE(result.command_line->timeout_set);
}

TEST(CommandLineTest, AcceptsCpuAndMemoryLimits) {
    const std::vector<const char*> arguments{
        "tradutorlinux", "--cpu", "3", "--memory", "128", "programa.exe"};

    const ParseResult result = parse_arguments(arguments);

    ASSERT_TRUE(result.command_line.has_value());
    EXPECT_EQ(result.command_line->cpu_limit_seconds, 3U);
    EXPECT_TRUE(result.command_line->cpu_limit_set);
    EXPECT_EQ(result.command_line->memory_limit_mib, 128U);
    EXPECT_TRUE(result.command_line->memory_limit_set);
}

TEST(CommandLineTest, AcceptsResourceLimitsForCatalogRun) {
    const std::vector<const char*> arguments{
        "tradutorlinux", "app", "run", "app-id", "--cpu", "2", "--memory", "64", "argument"};

    const ParseResult result = parse_arguments(arguments);

    ASSERT_TRUE(result.command_line.has_value());
    EXPECT_EQ(result.command_line->mode, CommandMode::AppRun);
    EXPECT_EQ(result.command_line->cpu_limit_seconds, 2U);
    EXPECT_TRUE(result.command_line->cpu_limit_set);
    EXPECT_EQ(result.command_line->memory_limit_mib, 64U);
    EXPECT_TRUE(result.command_line->memory_limit_set);
    ASSERT_EQ(result.command_line->guest_arguments.size(), 1U);
    EXPECT_EQ(result.command_line->guest_arguments.front(), "argument");
}

TEST(CommandLineTest, AcceptsTimeoutForCatalogRun) {
    const std::vector<const char*> arguments{
        "tradutorlinux", "app", "run", "app-id", "--timeout", "1"};

    const ParseResult result = parse_arguments(arguments);

    ASSERT_TRUE(result.command_line.has_value());
    EXPECT_EQ(result.command_line->mode, CommandMode::AppRun);
    EXPECT_EQ(result.command_line->timeout_ms, 1000U);
    EXPECT_TRUE(result.command_line->timeout_set);
    EXPECT_TRUE(result.command_line->guest_arguments.empty());
}

TEST(CommandLineTest, RejectsRepeatedResourceLimit) {
    const std::vector<const char*> arguments{
        "tradutorlinux", "--memory", "64", "--memory", "128", "programa.exe"};

    const ParseResult result = parse_arguments(arguments);

    EXPECT_FALSE(result.command_line.has_value());
    EXPECT_NE(result.error_message.find("foi repetida"), std::string::npos);
}

TEST(CommandLineTest, AcceptsZeroTimeoutAsUnlimited) {
    const std::vector<const char*> arguments{"tradutorlinux", "--timeout", "0", "programa.exe"};

    const ParseResult result = parse_arguments(arguments);

    ASSERT_TRUE(result.command_line.has_value());
    EXPECT_EQ(result.command_line->timeout_ms, 0);
    EXPECT_TRUE(result.command_line->timeout_set);
}

TEST(CommandLineTest, RejectsTimeoutWithoutValue) {
    const std::vector<const char*> arguments{"tradutorlinux", "--timeout"};

    const ParseResult result = parse_arguments(arguments);

    EXPECT_FALSE(result.command_line.has_value());
    EXPECT_NE(result.error_message.find("requer um valor em segundos"), std::string::npos);
}

TEST(CommandLineTest, RejectsNonNumericTimeout) {
    const std::vector<const char*> arguments{"tradutorlinux", "--timeout", "abc"};

    const ParseResult result = parse_arguments(arguments);

    EXPECT_FALSE(result.command_line.has_value());
    EXPECT_NE(result.error_message.find("valor inválido para --timeout"), std::string::npos);
}

TEST(CommandLineTest, RejectsRepeatedTimeout) {
    const std::vector<const char*> arguments{"tradutorlinux", "--timeout", "1", "--timeout", "2"};

    const ParseResult result = parse_arguments(arguments);

    EXPECT_FALSE(result.command_line.has_value());
    EXPECT_NE(result.error_message.find("foi repetida"), std::string::npos);
}

TEST(CommandLineTest, TimeoutAfterExecutableBelongsToGuest) {
    const std::vector<const char*> arguments{"tradutorlinux", "programa.exe", "--timeout", "3"};

    const ParseResult result = parse_arguments(arguments);

    ASSERT_TRUE(result.command_line.has_value());
    EXPECT_EQ(result.command_line->timeout_ms, 0);
    ASSERT_EQ(result.command_line->guest_arguments.size(), 2);
    EXPECT_EQ(result.command_line->guest_arguments[0], "--timeout");
    EXPECT_EQ(result.command_line->guest_arguments[1], "3");
}

TEST(CommandLineTest, RejectsHelpWithExecutable) {
    const std::vector<const char*> arguments{"tradutorlinux", "--help", "programa.exe"};

    const ParseResult result = parse_arguments(arguments);

    EXPECT_FALSE(result.command_line.has_value());
    EXPECT_EQ(result.error_message, "--help e --version não podem ser usados com um arquivo executável");
}

TEST(CommandRunTest, ReturnsUsageWhenExecutableIsMissing) {
    const CommandLine command_line{};
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);

    EXPECT_EQ(exit_code, ExitCode::Usage);
    EXPECT_TRUE(stdout_stream.str().empty());
    EXPECT_NE(stderr_stream.str().find("informe um arquivo .exe"), std::string::npos);
}

TEST(CommandRunTest, RejectsReportForInstall) {
    CommandLine command_line;
    command_line.mode = CommandMode::Install;
    command_line.report_only = true;
    command_line.executable_path = "setup.exe";
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);

    EXPECT_EQ(exit_code, ExitCode::Usage);
    EXPECT_TRUE(stdout_stream.str().empty());
    EXPECT_NE(stderr_stream.str().find("--report não pode ser usado"), std::string::npos);
}

TEST(CommandRunTest, ReturnsInputUnavailableForMissingPath) {
    CommandLine command_line;
    command_line.executable_path =
        std::filesystem::temp_directory_path() / "tradutorlinux-fixture-that-does-not-exist.exe";
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);

    EXPECT_EQ(exit_code, ExitCode::InputUnavailable);
    EXPECT_TRUE(stdout_stream.str().empty());
    EXPECT_NE(stderr_stream.str().find("não foi possível acessar o arquivo"), std::string::npos);
}

TEST(CommandRunTest, RejectsReadableNonPeInputAsMalformed) {
    CommandLine command_line;
    command_line.trace_enabled = true;
    command_line.executable_path = std::filesystem::path{__FILE__};
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);

    EXPECT_EQ(exit_code, ExitCode::MalformedPe);
    EXPECT_TRUE(stdout_stream.str().empty());
    EXPECT_NE(stderr_stream.str().find("[tl][cli][info] input path="), std::string::npos);
    EXPECT_NE(stderr_stream.str().find("[tl][pe][error] parse-failed"), std::string::npos);
    EXPECT_NE(stderr_stream.str().find("assinatura DOS ausente"), std::string::npos);
}

TEST(CommandRunTest, ReportsSupportWithoutExecutingEntryPoint) {
    CommandLine command_line;
    command_line.report_only = true;
    command_line.executable_path =
        std::filesystem::path{TL_FIXTURE_OUTPUT_DIRECTORY} / "tl_file.exe";
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);

    EXPECT_EQ(exit_code, ExitCode::Success);
    const std::string output = stdout_stream.str();
    EXPECT_NE(output.find("result: supported"), std::string::npos);
    EXPECT_NE(output.find("execution: not-attempted"), std::string::npos);
    EXPECT_NE(output.find("execution-result: not-attempted"), std::string::npos);
    EXPECT_NE(output.find("compatibility:"), std::string::npos);
    EXPECT_NE(output.find("runtime-support: full"), std::string::npos);
    EXPECT_NE(output.find("support=full"), std::string::npos);
    EXPECT_NE(output.find("dll: KERNEL32.dll"), std::string::npos);
    EXPECT_EQ(output.find("fase5"), std::string::npos);
    EXPECT_EQ(stderr_stream.str().find("mapped"), std::string::npos);
}

TEST(CommandRunTest, ReportsUnsupportedWhenDllNotRegistered) {
    CommandLine command_line;
    command_line.report_only = true;
    command_line.executable_path =
        std::filesystem::path{TL_FIXTURE_OUTPUT_DIRECTORY} / "tl_missing_dll.exe";
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);

    EXPECT_EQ(exit_code, ExitCode::Unsupported);
    const std::string output = stdout_stream.str();
    EXPECT_NE(output.find("result: unsupported"), std::string::npos);
    EXPECT_NE(output.find("execution: not-attempted"), std::string::npos);
    EXPECT_NE(output.find("execution-result: not-attempted"), std::string::npos);
    EXPECT_NE(output.find("compatibility:"), std::string::npos);
    EXPECT_NE(output.find("runtime-support: unresolved"), std::string::npos);
    EXPECT_NE(output.find("% ("), std::string::npos);
}

TEST(CommandRunTest, ReturnsGuestFaultWhenGuestTerminatesBySignal) {
    CommandLine command_line;
    command_line.trace_enabled = true;
    command_line.executable_path =
        std::filesystem::path{TL_FIXTURE_OUTPUT_DIRECTORY} / "tl_crash.exe";
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);

    EXPECT_EQ(exit_code, ExitCode::GuestFault);
    EXPECT_TRUE(stdout_stream.str().empty());
    EXPECT_NE(stderr_stream.str().find("terminated category=\"guest-signal\""),
              std::string::npos);
    EXPECT_EQ(stderr_stream.str().find("exit exit-code="), std::string::npos);
}

TEST(CommandRunTest, ReturnsGuestTimeoutWhenGuestHangs) {
    CommandLine command_line;
    command_line.trace_enabled = true;
    command_line.timeout_ms = 1000;
    command_line.executable_path =
        std::filesystem::path{TL_FIXTURE_OUTPUT_DIRECTORY} / "tl_hang.exe";
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);

    EXPECT_EQ(exit_code, ExitCode::GuestTimeout);
    EXPECT_TRUE(stdout_stream.str().empty());
    EXPECT_NE(stderr_stream.str().find("terminated category=\"guest-timeout\""),
              std::string::npos);
    EXPECT_EQ(stderr_stream.str().find("exit exit-code="), std::string::npos);
}

}  // namespace
}  // namespace tradutorlinux
