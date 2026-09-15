#include "tradutorlinux/cli.hpp"
#include "tradutorlinux/catalog/app_catalog.hpp"
#include "tradutorlinux/diagnostics/trace.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <unistd.h>

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

#if defined(TRADUTORLINUX_RUST_PE_PARSER)
TEST(CommandRunTest, DirectRustReportPreservesStructuredParseFailure) {
    CommandLine command_line;
    command_line.report_only = true;
    command_line.trace_enabled = true;
    command_line.executable_path = std::filesystem::path{__FILE__};
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);

    EXPECT_EQ(exit_code, ExitCode::MalformedPe);
    EXPECT_TRUE(stdout_stream.str().empty());
    const std::string trace = stderr_stream.str();
    EXPECT_NE(trace.find("[tl][pe][error] parse-failed"), std::string::npos);
    EXPECT_NE(trace.find("backend=\"rust\""), std::string::npos);
    EXPECT_NE(trace.find("code=\"16\""), std::string::npos);
    EXPECT_NE(trace.find("phase=\"2\""), std::string::npos);
    EXPECT_NE(trace.find("input-offset=\"0\""), std::string::npos);
    EXPECT_NE(trace.find("detail-value=\""), std::string::npos);
}

TEST(CommandRunTest, AppRunReportRemainsOnCppParserPath) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("tradutorlinux-r21-3-app-run-report-" + std::to_string(static_cast<long long>(getpid())));
    const std::filesystem::path config_home = root / "config";
    const std::filesystem::path prefix_root = root / "prefix";
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    std::filesystem::create_directories(config_home, cleanup_error);

    const char* old_prefix_value = std::getenv("TL_PREFIX");
    const char* old_config_value = std::getenv("XDG_CONFIG_HOME");
    const std::string old_prefix = old_prefix_value == nullptr ? "" : old_prefix_value;
    const std::string old_config = old_config_value == nullptr ? "" : old_config_value;
    const bool had_prefix = old_prefix_value != nullptr;
    const bool had_config = old_config_value != nullptr;
    setenv("TL_PREFIX", prefix_root.c_str(), 1);
    setenv("XDG_CONFIG_HOME", config_home.c_str(), 1);

    catalog::AppCatalog app_catalog;
    catalog::AppEntry app;
    app.id = "r21-3-app-run-report";
    app.name = "R21.3 app run report";
    app.executable_path =
        (std::filesystem::path{TL_FIXTURE_OUTPUT_DIRECTORY} / "tl_file.exe").string();
    app.prefix_path = prefix_root.string();
    app.working_directory = std::filesystem::path{TL_FIXTURE_OUTPUT_DIRECTORY}.string();
    EXPECT_TRUE(app_catalog.add_app(app));
    EXPECT_TRUE(app_catalog.save_to_file());

    CommandLine command_line;
    command_line.mode = CommandMode::AppRun;
    command_line.app_id = app.id;
    command_line.report_only = true;
    command_line.trace_enabled = true;
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;
    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);

    if (had_prefix) {
        setenv("TL_PREFIX", old_prefix.c_str(), 1);
    } else {
        unsetenv("TL_PREFIX");
    }
    if (had_config) {
        setenv("XDG_CONFIG_HOME", old_config.c_str(), 1);
    } else {
        unsetenv("XDG_CONFIG_HOME");
    }
    std::filesystem::remove_all(root, cleanup_error);

    EXPECT_EQ(exit_code, ExitCode::Success);
    EXPECT_NE(stdout_stream.str().find("result: supported"), std::string::npos);
    EXPECT_NE(stderr_stream.str().find("[tl][pe][info] image"), std::string::npos);
    EXPECT_EQ(stderr_stream.str().find("backend=\"rust\""), std::string::npos);
    EXPECT_EQ(stderr_stream.str().find("mapped"), std::string::npos);
}
#endif

TEST(CommandRunTest, ReportsSupportWithoutExecutingEntryPoint) {
    CommandLine command_line;
    command_line.report_only = true;
    command_line.trace_enabled = true;
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
#if defined(TRADUTORLINUX_RUST_PE_PARSER)
    EXPECT_NE(stderr_stream.str().find("[tl][pe][info] image"), std::string::npos);
    EXPECT_NE(stderr_stream.str().find("backend=\"rust\""), std::string::npos);
#endif
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
    EXPECT_NE(stderr_stream.str().find("timeout-samples=\""), std::string::npos);
    EXPECT_NE(stderr_stream.str().find("timeout-pe-samples=\""), std::string::npos);
    EXPECT_NE(stderr_stream.str().find("timeout-host-samples=\""), std::string::npos);
    if (stderr_stream.str().find("timeout-rip=\"0x") == std::string::npos) {
        GTEST_SKIP() << "captura de registradores indisponível neste ambiente";
    }
    if (stderr_stream.str().find("timeout-stack-top-module-offset=\"0x") ==
        std::string::npos) {
        GTEST_SKIP() << "leitura do topo da pilha indisponível neste ambiente";
    }
    EXPECT_EQ(stderr_stream.str().find("exit exit-code="), std::string::npos);
}

TEST(CommandRunTest, TraceJsonRecordsGuestExecutionBoundary) {
    const std::filesystem::path trace_directory =
        std::filesystem::temp_directory_path() / "tl-guest-execution-boundary-test";
    std::filesystem::remove_all(trace_directory);

    CommandLine command_line;
    command_line.trace_enabled = true;
    command_line.trace_json_directory = trace_directory;
    command_line.executable_path =
        std::filesystem::path{TL_FIXTURE_OUTPUT_DIRECTORY} / "tl_nop.exe";
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);
    diagnostics::disable_trace_json_directory();

    ASSERT_EQ(exit_code, ExitCode::Success);
    std::string trace;
    for (const auto& entry : std::filesystem::directory_iterator(trace_directory)) {
        if (entry.path().extension() != ".jsonl") continue;
        std::ifstream input(entry.path());
        trace.append(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
    }
    EXPECT_NE(trace.find("\"event\": \"guest-execution\""), std::string::npos);
    EXPECT_NE(trace.find("\"phase\": \"enter\""), std::string::npos);
    EXPECT_NE(trace.find("\"phase\": \"return\""), std::string::npos);
    std::filesystem::remove_all(trace_directory);
}

TEST(CommandRunTest, ReportOutputsResourcesWhenPresent) {
    CommandLine command_line;
    command_line.report_only = true;
    command_line.executable_path =
        std::filesystem::path{TL_FIXTURE_OUTPUT_DIRECTORY} / "tl_resources.exe";
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);
    EXPECT_EQ(exit_code, ExitCode::Success);

    const std::string report = stdout_stream.str();
    EXPECT_NE(report.find("resources: 1 types (rcdata=1)\n"), std::string::npos)
        << "Relatório:\n" << report;
}

TEST(CommandRunTest, ReportDetectsDirectXAndRecommendsProton) {
    CommandLine command_line;
    command_line.report_only = true;
    command_line.executable_path =
        std::filesystem::path{TL_FIXTURE_OUTPUT_DIRECTORY} / "tl_graphics_probe.exe";
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);
    EXPECT_EQ(exit_code, ExitCode::Unsupported);

    const std::string report = stdout_stream.str();
    EXPECT_NE(report.find("recommendation: requer aceleracao grafica 3D (d3d11.dll); configure profile.json com 'backend.kind: proton'"),
              std::string::npos)
        << "Relatório:\n" << report;
}

TEST(CommandRunTest, ReportDetectsD3D12AndDxgiRecommendsProton) {
    CommandLine command_line;
    command_line.report_only = true;
    command_line.executable_path =
        std::filesystem::path{TL_FIXTURE_OUTPUT_DIRECTORY} / "tl_d3d12_probe.exe";
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);
    EXPECT_EQ(exit_code, ExitCode::Unsupported);

    const std::string report = stdout_stream.str();
    EXPECT_NE(report.find("recommendation: requer aceleracao grafica 3D (d3d12.dll, dxgi.dll); configure profile.json com 'backend.kind: proton'"),
              std::string::npos)
        << "Relatório:\n" << report;
}

TEST(CommandRunTest, ReportDoesNotRecommendProtonWhenNoGraphics) {
    CommandLine command_line;
    command_line.report_only = true;
    command_line.executable_path =
        std::filesystem::path{TL_FIXTURE_OUTPUT_DIRECTORY} / "tl_hello.exe";
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);
    EXPECT_EQ(exit_code, ExitCode::Success);

    const std::string report = stdout_stream.str();
    EXPECT_EQ(report.find("recommendation:"), std::string::npos)
        << "Relatório inesperadamente emitiu recomendação:\n" << report;
}

TEST(CommandRunTest, ReportDisplaysPeSecurityMitigations) {
    CommandLine command_line;
    command_line.report_only = true;
    command_line.executable_path =
        std::filesystem::path{TL_FIXTURE_OUTPUT_DIRECTORY} / "tl_hello.exe";
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);
    EXPECT_EQ(exit_code, ExitCode::Success);

    const std::string report = stdout_stream.str();
    EXPECT_NE(report.find("mitigations: aslr="), std::string::npos)
        << "Relatório não continha linha de mitigações:\n" << report;
}

TEST(CommandLineTest, ParsesReportJsonOptions) {
    const std::vector<const char*> valid_args{"tradutorlinux", "--report", "--json", "app.exe"};
    const ParseResult valid_res = parse_command_line(static_cast<int>(valid_args.size()), valid_args.data());
    ASSERT_TRUE(valid_res.command_line.has_value());
    EXPECT_TRUE(valid_res.command_line->report_only);
    EXPECT_TRUE(valid_res.command_line->report_json);

    const std::vector<const char*> json_only{"tradutorlinux", "--json", "app.exe"};
    const ParseResult err_res = parse_command_line(static_cast<int>(json_only.size()), json_only.data());
    EXPECT_FALSE(err_res.command_line.has_value());
    EXPECT_NE(err_res.error_message.find("a opção --json requer --report"), std::string::npos);

    const std::vector<const char*> repeated{"tradutorlinux", "--report", "--json", "--json", "app.exe"};
    const ParseResult rep_res = parse_command_line(static_cast<int>(repeated.size()), repeated.data());
    EXPECT_FALSE(rep_res.command_line.has_value());
    EXPECT_NE(rep_res.error_message.find("a opção --json foi repetida"), std::string::npos);
}

TEST(CommandRunTest, ReportEmitsStructuredJson) {
    CommandLine command_line;
    command_line.report_only = true;
    command_line.report_json = true;
    command_line.executable_path =
        std::filesystem::path{TL_FIXTURE_OUTPUT_DIRECTORY} / "tl_hello.exe";
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);
    EXPECT_EQ(exit_code, ExitCode::Success);

    const std::string report = stdout_stream.str();
    EXPECT_EQ(report.front(), '{');
    EXPECT_NE(report.find("\"format\": \"PE32+ x86-64\""), std::string::npos) << report;
    EXPECT_NE(report.find("\"mitigations\": {"), std::string::npos) << report;
    EXPECT_NE(report.find("\"packer\": {"), std::string::npos) << report;
    EXPECT_NE(report.find("\"toolchain\": {"), std::string::npos) << report;
    EXPECT_NE(report.find("\"security\": {"), std::string::npos) << report;
    EXPECT_NE(report.find("\"imports\": {"), std::string::npos) << report;
    EXPECT_NE(report.find("\"status\": \"supported\""), std::string::npos) << report;
    EXPECT_NE(report.find("\"recommendations\": ["), std::string::npos) << report;
}

TEST(CommandRunTest, ReportJsonContains3DGraphicsRecommendation) {
    CommandLine command_line;
    command_line.report_only = true;
    command_line.report_json = true;
    command_line.executable_path =
        std::filesystem::path{TL_FIXTURE_OUTPUT_DIRECTORY} / "tl_graphics_probe.exe";
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);
    EXPECT_EQ(exit_code, ExitCode::Unsupported);

    const std::string report = stdout_stream.str();
    EXPECT_NE(report.find("\"recommendations\": ["), std::string::npos) << report;
    EXPECT_NE(report.find("requer aceleracao grafica 3D (d3d11.dll); configure profile.json com 'backend.kind: proton'"),
              std::string::npos)
        << report;
}

TEST(CommandLineTest, ParsesDoctorCommand) {
    const std::vector<const char*> doctor_args{"tradutorlinux", "doctor"};
    const ParseResult doc_res = parse_command_line(static_cast<int>(doctor_args.size()), doctor_args.data());
    ASSERT_TRUE(doc_res.command_line.has_value());
    EXPECT_EQ(doc_res.command_line->mode, CommandMode::Doctor);
    EXPECT_FALSE(doc_res.command_line->report_json);

    const std::vector<const char*> doctor_json_args{"tradutorlinux", "doctor", "--json"};
    const ParseResult json_res = parse_command_line(static_cast<int>(doctor_json_args.size()), doctor_json_args.data());
    ASSERT_TRUE(json_res.command_line.has_value());
    EXPECT_EQ(json_res.command_line->mode, CommandMode::Doctor);
    EXPECT_TRUE(json_res.command_line->report_json);

    const std::vector<const char*> doctor_err_args{"tradutorlinux", "doctor", "--invalid-flag"};
    const ParseResult err_res = parse_command_line(static_cast<int>(doctor_err_args.size()), doctor_err_args.data());
    EXPECT_FALSE(err_res.command_line.has_value());
}

TEST(CommandRunTest, DoctorProducesHumanReadableDiagnostics) {
    CommandLine command_line;
    command_line.mode = CommandMode::Doctor;
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);
    EXPECT_EQ(exit_code, ExitCode::Success);

    const std::string output = stdout_stream.str();
    EXPECT_NE(output.find("TradutorLinux Doctor - Diagnostico do Ambiente Hospedeiro"), std::string::npos);
    EXPECT_NE(output.find("[OK] Arquitetura:"), std::string::npos);
    EXPECT_NE(output.find("Status: ambiente pronto"), std::string::npos);
}

TEST(CommandRunTest, DoctorEmitsStructuredJson) {
    CommandLine command_line;
    command_line.mode = CommandMode::Doctor;
    command_line.report_json = true;
    std::ostringstream stdout_stream;
    std::ostringstream stderr_stream;

    const ExitCode exit_code = run_command(command_line, stdout_stream, stderr_stream);
    EXPECT_EQ(exit_code, ExitCode::Success);

    const std::string output = stdout_stream.str();
    EXPECT_EQ(output.front(), '{');
    EXPECT_NE(output.find("\"doctor\": {"), std::string::npos);
    EXPECT_NE(output.find("\"architecture\": {"), std::string::npos);
    EXPECT_NE(output.find("\"display\": {"), std::string::npos);
    EXPECT_NE(output.find("\"ready\": true"), std::string::npos);
}

TEST(CommandLineTest, ParsesNoNetworkOption) {
    const std::array<const char*, 3> argv{"tradutorlinux", "--no-network", "app.exe"};
    const ParseResult result = parse_command_line(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.command_line.has_value());
    EXPECT_EQ(result.command_line->network_mode, process::NetworkMode::None);
    EXPECT_TRUE(result.command_line->network_set);
}

TEST(CommandLineTest, ParsesNetworkModes) {
    {
        const std::array<const char*, 3> argv{"tradutorlinux", "--network=none", "app.exe"};
        const ParseResult result = parse_command_line(static_cast<int>(argv.size()), argv.data());
        ASSERT_TRUE(result.command_line.has_value());
        EXPECT_EQ(result.command_line->network_mode, process::NetworkMode::None);
    }
    {
        const std::array<const char*, 4> argv{"tradutorlinux", "--network", "loopback", "app.exe"};
        const ParseResult result = parse_command_line(static_cast<int>(argv.size()), argv.data());
        ASSERT_TRUE(result.command_line.has_value());
        EXPECT_EQ(result.command_line->network_mode, process::NetworkMode::Loopback);
    }
    {
        const std::array<const char*, 3> argv{"tradutorlinux", "--network=full", "app.exe"};
        const ParseResult result = parse_command_line(static_cast<int>(argv.size()), argv.data());
        ASSERT_TRUE(result.command_line.has_value());
        EXPECT_EQ(result.command_line->network_mode, process::NetworkMode::Full);
    }
}

TEST(CommandLineTest, RejectsInvalidNetworkOption) {
    const std::array<const char*, 3> argv{"tradutorlinux", "--network=wifi", "app.exe"};
    const ParseResult result = parse_command_line(static_cast<int>(argv.size()), argv.data());
    EXPECT_FALSE(result.command_line.has_value());
    EXPECT_NE(result.error_message.find("modo inválido para --network"), std::string::npos);
}

TEST(CommandLineTest, RejectsDuplicateNetworkOption) {
    const std::array<const char*, 4> argv{"tradutorlinux", "--no-network", "--network=none", "app.exe"};
    const ParseResult result = parse_command_line(static_cast<int>(argv.size()), argv.data());
    EXPECT_FALSE(result.command_line.has_value());
    EXPECT_NE(result.error_message.find("a opção --network foi repetida"), std::string::npos);
}

}  // namespace
}  // namespace tradutorlinux
