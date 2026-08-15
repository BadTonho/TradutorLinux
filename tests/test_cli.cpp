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

TEST(CommandLineTest, AcceptsReportAndExecutable) {
    const std::vector<const char*> arguments{"tradutorlinux", "--report", "programa.exe"};

    const ParseResult result = parse_arguments(arguments);

    ASSERT_TRUE(result.command_line.has_value());
    EXPECT_TRUE(result.command_line->report_only);
    ASSERT_TRUE(result.command_line->executable_path.has_value());
}

TEST(CommandLineTest, RejectsUnknownOption) {
    const std::vector<const char*> arguments{"tradutorlinux", "--invalida"};

    const ParseResult result = parse_arguments(arguments);

    EXPECT_FALSE(result.command_line.has_value());
    EXPECT_EQ(result.error_message, "opção desconhecida: --invalida");
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
    EXPECT_NE(stdout_stream.str().find("result: supported"), std::string::npos);
    EXPECT_NE(stdout_stream.str().find("execution: not-attempted"), std::string::npos);
    EXPECT_EQ(stdout_stream.str().find("fase5"), std::string::npos);
    EXPECT_EQ(stderr_stream.str().find("mapped"), std::string::npos);
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

}  // namespace
}  // namespace tradutorlinux
