#include "tradutorlinux/cli.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"

#include <array>
#include <filesystem>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace tradutorlinux {
namespace {

constexpr std::string_view kUsage = "Uso: tradutorlinux [--trace] <arquivo.exe>\n";

[[nodiscard]] bool is_option(const std::string_view argument) {
    return argument.starts_with('-');
}

}  // namespace

ParseResult parse_command_line(const int argc, const char* const argv[]) {
    CommandLine command_line;
    bool options_ended = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};

        if (!options_ended && argument == "--") {
            options_ended = true;
            continue;
        }

        if (!options_ended && argument == "--help") {
            if (command_line.show_help) {
                return {.command_line = std::nullopt,
                        .error_message = "a opção --help foi repetida"};
            }
            command_line.show_help = true;
            continue;
        }

        if (!options_ended && argument == "--version") {
            if (command_line.show_version) {
                return {.command_line = std::nullopt,
                        .error_message = "a opção --version foi repetida"};
            }
            command_line.show_version = true;
            continue;
        }

        if (!options_ended && argument == "--trace") {
            if (command_line.trace_enabled) {
                return {.command_line = std::nullopt,
                        .error_message = "a opção --trace foi repetida"};
            }
            command_line.trace_enabled = true;
            continue;
        }

        if (!options_ended && is_option(argument)) {
            return {.command_line = std::nullopt,
                    .error_message = "opção desconhecida: " + std::string{argument}};
        }

        if (command_line.executable_path.has_value()) {
            return {.command_line = std::nullopt,
                    .error_message = "apenas um arquivo executável pode ser informado"};
        }
        command_line.executable_path = std::filesystem::path{std::string{argument}};
    }

    if ((command_line.show_help || command_line.show_version) && command_line.executable_path.has_value()) {
        return {.command_line = std::nullopt,
                .error_message = "--help e --version não podem ser usados com um arquivo executável"};
    }

    if (command_line.show_help && command_line.show_version) {
        return {.command_line = std::nullopt,
                .error_message = "--help e --version não podem ser usados juntos"};
    }

    return {.command_line = std::move(command_line), .error_message = {}};
}

ExitCode run_command(const CommandLine& command_line, std::ostream& stdout_stream,
                     std::ostream& stderr_stream) {
    if (command_line.show_help) {
        print_help(stdout_stream);
        return ExitCode::Success;
    }

    if (command_line.show_version) {
        stdout_stream << "TradutorLinux " << TRADUTORLINUX_VERSION << '\n';
        return ExitCode::Success;
    }

    if (!command_line.executable_path.has_value()) {
        stderr_stream << "erro: informe um arquivo .exe\n";
        print_help(stderr_stream);
        return ExitCode::Usage;
    }

    std::error_code filesystem_error;
    const bool is_regular_file =
        std::filesystem::is_regular_file(*command_line.executable_path, filesystem_error);
    if (filesystem_error || !is_regular_file) {
        stderr_stream << "erro: não foi possível acessar o arquivo: "
                      << command_line.executable_path->string() << '\n';
        return ExitCode::InputUnavailable;
    }

    if (command_line.trace_enabled) {
        const std::string input_path = command_line.executable_path->string();
        const std::array input_fields{diagnostics::TraceField{"path", input_path}};
        const std::array feature_fields{
            diagnostics::TraceField{"feature", "pe-loader"},
            diagnostics::TraceField{"phase", "1"},
        };
        diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Cli,
                                 diagnostics::TraceLevel::Info, "input", input_fields);
        diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Runtime,
                                 diagnostics::TraceLevel::Error, "feature-unavailable",
                                 feature_fields);
    }

    stderr_stream << "erro: o carregamento de PE ainda não está implementado (Fase 1)\n";
    return ExitCode::Unsupported;
}

void print_help(std::ostream& stream) {
    stream << kUsage;
    stream << "\n";
    stream << "Opções:\n";
    stream << "  --trace    escreve diagnóstico estruturado em stderr\n";
    stream << "  --help     mostra esta ajuda\n";
    stream << "  --version  mostra a versão do TradutorLinux\n";
}

}  // namespace tradutorlinux
