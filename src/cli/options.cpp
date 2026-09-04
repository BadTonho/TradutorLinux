#include "tradutorlinux/cli.hpp"
#include "cli_internal.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tradutorlinux {

using namespace cli_detail;

namespace {

[[nodiscard]] bool is_option(const std::string_view argument) {
    TL_TRACE_FUNCTION();
    return argument.starts_with('-');
}

}  // namespace

ParseResult parse_command_line(const int argc, const char* const argv[]) {
    TL_TRACE_FUNCTION();
    CommandLine command_line;
    if (argc <= 1) {
        return {.command_line = std::move(command_line), .error_message = {}};
    }

    const std::string_view first_arg{argv[1]};

    // Subcomando: install <setup.exe>
    if (first_arg == "install") {
        command_line.mode = CommandMode::Install;
        for (int i = 2; i < argc; ++i) {
            const std::string_view arg{argv[i]};
            if (arg == "--name") {
                if (i + 1 >= argc) {
                    return {.command_line = std::nullopt,
                            .error_message = "a opção --name requer um valor"};
                }
                command_line.app_name = argv[++i];
            } else if (arg == "--prefix") {
                if (i + 1 >= argc) {
                    return {.command_line = std::nullopt,
                            .error_message = "a opção --prefix requer um diretório"};
                }
                command_line.custom_prefix = std::filesystem::path{argv[++i]};
            } else if (arg == "--app-exe") {
                if (i + 1 >= argc) {
                    return {.command_line = std::nullopt,
                            .error_message = "a opção --app-exe requer um caminho"};
                }
                command_line.installed_executable_path = std::filesystem::path{argv[++i]};
            } else if (arg == "--timeout") {
                if (command_line.timeout_set) {
                    return {.command_line = std::nullopt,
                            .error_message = "a opção --timeout foi repetida"};
                }
                if (i + 1 >= argc) {
                    return {.command_line = std::nullopt,
                            .error_message = "a opção --timeout requer um valor em segundos"};
                }
                const std::string_view value{argv[++i]};
                if (value.empty() || value.size() > 9) {
                    return {.command_line = std::nullopt,
                            .error_message = "valor inválido para --timeout: " + std::string{value}};
                }
                std::uint64_t seconds = 0;
                for (const char digit : value) {
                    if (digit < '0' || digit > '9') {
                        return {.command_line = std::nullopt,
                                .error_message = "valor inválido para --timeout: " + std::string{value}};
                    }
                    seconds = seconds * 10 + static_cast<std::uint64_t>(digit - '0');
                }
                if (seconds > std::numeric_limits<std::uint64_t>::max() / 1000U) {
                    return {.command_line = std::nullopt,
                            .error_message = "valor de --timeout muito grande: " + std::string{value}};
                }
                command_line.timeout_ms = seconds * 1000U;
                command_line.timeout_set = true;
            } else if (arg == "--trace" || arg.starts_with("--trace=")) {
                if (command_line.trace_enabled) {
                    return {.command_line = std::nullopt,
                            .error_message = "a opção --trace foi repetida"};
                }
                command_line.trace_enabled = true;
                if (arg.size() > 7) {
                    const std::string_view list = arg.substr(8);
                    if (!list.empty()) {
                        if (list.front() == ',' || list.back() == ',' || list.find(",,") != std::string_view::npos) {
                            return {.command_line = std::nullopt,
                                    .error_message = "valor inválido para --trace: " + std::string(list)};
                        }
                        std::string_view rem = list;
                        while (!rem.empty()) {
                            const std::size_t comma = rem.find(',');
                            const std::string_view tok = comma == std::string_view::npos ? rem : rem.substr(0, comma);
                            if (tok.empty()) {
                                return {.command_line = std::nullopt,
                                        .error_message = "valor inválido para --trace: " + std::string(list)};
                            }
                            diagnostics::TraceComponent dummy;
                            if (!diagnostics::trace_component_from_name(tok, dummy)) {
                                return {.command_line = std::nullopt,
                                        .error_message = "canal de trace desconhecido: " + std::string(tok)};
                            }
                            command_line.trace_channels_raw.emplace_back(std::string{tok});
                            if (comma == std::string_view::npos) break;
                            rem = rem.substr(comma + 1);
                        }
                    }
                }
            } else if (arg == "--report") {
                command_line.report_only = true;
            } else if (arg == "--trace-json") {
                if (i + 1 >= argc) {
                    return {.command_line = std::nullopt,
                            .error_message = "a opção --trace-json requer um diretório"};
                }
                command_line.trace_json_directory = std::filesystem::path{argv[++i]};
            } else if (!command_line.executable_path.has_value() && !arg.starts_with('-')) {
                command_line.executable_path = std::filesystem::path{std::string{arg}};
            } else {
                return {.command_line = std::nullopt,
                        .error_message = "opção desconhecida para 'install': " + std::string{arg}};
            }
        }
        if (!command_line.executable_path.has_value()) {
            return {.command_line = std::nullopt,
                    .error_message = "o comando 'install' requer o caminho do arquivo instalador (.exe)"};
        }
        return {.command_line = std::move(command_line), .error_message = {}};
    }

    // Subcomando: app <list|run|add|remove>
    if (first_arg == "app") {
        if (argc < 3) {
            return {.command_line = std::nullopt,
                    .error_message = "o comando 'app' requer uma ação: list, run, add ou remove"};
        }
        const std::string_view action{argv[2]};
        if (action == "list") {
            command_line.mode = CommandMode::AppList;
            return {.command_line = std::move(command_line), .error_message = {}};
        }
        if (action == "run") {
            if (argc < 4) {
                return {.command_line = std::nullopt,
                        .error_message = "o comando 'app run' requer o ID ou nome do aplicativo"};
            }
            command_line.mode = CommandMode::AppRun;
            command_line.app_id = argv[3];
            for (int i = 4; i < argc; ++i) {
                const std::string_view arg{argv[i]};
                if (arg == "--trace" || arg.starts_with("--trace=")) {
                    if (command_line.trace_enabled) {
                        return {.command_line = std::nullopt,
                                .error_message = "a opção --trace foi repetida"};
                    }
                    command_line.trace_enabled = true;
                    if (arg.size() > 7) {
                        const std::string_view list = arg.substr(8);
                        if (!list.empty()) {
                            if (list.front() == ',' || list.back() == ',' || list.find(",,") != std::string_view::npos) {
                                return {.command_line = std::nullopt,
                                        .error_message = "valor inválido para --trace: " + std::string(list)};
                            }
                            std::string_view rem = list;
                            while (!rem.empty()) {
                                const std::size_t comma = rem.find(',');
                                const std::string_view tok = comma == std::string_view::npos ? rem : rem.substr(0, comma);
                                if (tok.empty()) {
                                    return {.command_line = std::nullopt,
                                            .error_message = "valor inválido para --trace: " + std::string(list)};
                                }
                                diagnostics::TraceComponent dummy;
                                if (!diagnostics::trace_component_from_name(tok, dummy)) {
                                    return {.command_line = std::nullopt,
                                            .error_message = "canal de trace desconhecido: " + std::string(tok)};
                                }
                                command_line.trace_channels_raw.emplace_back(std::string{tok});
                                if (comma == std::string_view::npos) break;
                                rem = rem.substr(comma + 1);
                            }
                        }
                    }
                } else if (arg == "--report") {
                    command_line.report_only = true;
                } else {
                    command_line.guest_arguments.emplace_back(arg);
                }
            }
            return {.command_line = std::move(command_line), .error_message = {}};
        }
        if (action == "add") {
            if (argc < 4) {
                return {.command_line = std::nullopt,
                        .error_message = "o comando 'app add' requer o caminho do executável"};
            }
            command_line.mode = CommandMode::AppAdd;
            command_line.executable_path = std::filesystem::path{argv[3]};
            for (int i = 4; i < argc; ++i) {
                const std::string_view arg{argv[i]};
                if (arg == "--name" && i + 1 < argc) {
                    command_line.app_name = argv[++i];
                } else if (arg == "--prefix" && i + 1 < argc) {
                    command_line.custom_prefix = std::filesystem::path{argv[++i]};
                } else if (arg == "--id" && i + 1 < argc) {
                    command_line.app_id = argv[++i];
                }
            }
            return {.command_line = std::move(command_line), .error_message = {}};
        }
        if (action == "remove") {
            if (argc < 4) {
                return {.command_line = std::nullopt,
                        .error_message = "o comando 'app remove' requer o ID do aplicativo"};
            }
            command_line.mode = CommandMode::AppRemove;
            command_line.app_id = argv[3];
            return {.command_line = std::move(command_line), .error_message = {}};
        }
        return {.command_line = std::nullopt,
                .error_message = "ação desconhecida para 'app': " + std::string{action}};
    }

    bool options_ended = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};

        if (command_line.executable_path.has_value()) {
            // Tudo depois do executável pertence ao convidado, inclusive
            // argumentos que começam com '-'.
            command_line.guest_arguments.emplace_back(argument);
            continue;
        }

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

        if (!options_ended && (argument == "--trace" || argument.starts_with("--trace="))) {
            if (command_line.trace_enabled) {
                return {.command_line = std::nullopt,
                        .error_message = "a opção --trace foi repetida"};
            }
            command_line.trace_enabled = true;
            if (argument.size() > 7) { // "--trace=" prefix length 8
                const std::string_view list = argument.substr(8);
                if (!list.empty()) {
                    if (list.front() == ',' || list.back() == ',' || list.find(",,") != std::string_view::npos) {
                        return {.command_line = std::nullopt,
                                .error_message = "valor inválido para --trace: " + std::string(list)};
                    }
                    std::string_view remaining = list;
                    while (!remaining.empty()) {
                        const std::size_t comma = remaining.find(',');
                        const std::string_view token = comma == std::string_view::npos
                                                           ? remaining
                                                           : remaining.substr(0, comma);
                        if (token.empty()) {
                            return {.command_line = std::nullopt,
                                    .error_message = "valor inválido para --trace: " + std::string(list)};
                        }
                        // Valida canal imediatamente (case-insensitive, como Wine)
                        diagnostics::TraceComponent dummy;
                        if (!diagnostics::trace_component_from_name(token, dummy)) {
                            return {.command_line = std::nullopt,
                                    .error_message = "canal de trace desconhecido: " + std::string(token)};
                        }
                        command_line.trace_channels_raw.emplace_back(std::string{token});
                        if (comma == std::string_view::npos) break;
                        remaining = remaining.substr(comma + 1);
                    }
                }
            }
            continue;
        }

        if (!options_ended && argument == "--report") {
            if (command_line.report_only) {
                return {.command_line = std::nullopt,
                        .error_message = "a opção --report foi repetida"};
            }
            command_line.report_only = true;
            continue;
        }

        if (!options_ended && argument == "--trace-json") {
            if (command_line.trace_json_directory.has_value()) {
                return {.command_line = std::nullopt,
                        .error_message = "a opção --trace-json foi repetida"};
            }
            if (index + 1 >= argc) {
                return {.command_line = std::nullopt,
                        .error_message = "a opção --trace-json requer um diretório"};
            }
            command_line.trace_json_directory = std::filesystem::path{argv[++index]};
            continue;
        }

        if (!options_ended && argument == "--timeout") {
            if (command_line.timeout_set) {
                return {.command_line = std::nullopt,
                        .error_message = "a opção --timeout foi repetida"};
            }
            if (index + 1 >= argc) {
                return {.command_line = std::nullopt,
                        .error_message = "a opção --timeout requer um valor em segundos"};
            }
            const std::string_view value{argv[index + 1]};
            if (value.empty() || value.size() > 9) {
                return {.command_line = std::nullopt,
                        .error_message = "valor inválido para --timeout: " + std::string{value}};
            }
            std::uint64_t seconds = 0;
            for (const char digit : value) {
                if (digit < '0' || digit > '9') {
                    return {.command_line = std::nullopt,
                            .error_message = "valor inválido para --timeout: " + std::string{value}};
                }
                seconds = seconds * 10 + static_cast<std::uint64_t>(digit - '0');
            }
            if (seconds > std::numeric_limits<std::uint64_t>::max() / 1000U) {
                return {.command_line = std::nullopt,
                        .error_message = "valor de --timeout muito grande: " + std::string{value}};
            }
            command_line.timeout_ms = seconds * 1000U;
            command_line.timeout_set = true;
            ++index;
            continue;
        }

        if (!options_ended && is_option(argument)) {
            return {.command_line = std::nullopt,
                    .error_message = "opção desconhecida: " + std::string{argument}};
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

void print_help(std::ostream& stream) {
    TL_TRACE_FUNCTION();
    stream << kUsage;
    stream << "\n";
    stream << "Comandos de Gerenciamento da Biblioteca e Instalação:\n";
    stream << "  install <setup.exe> [--name <Nome>] [--prefix <dir>] [--app-exe <caminho>]\n";
    stream << "             instala em prefixo próprio e cadastra um único executável detectado\n";
    stream << "             --app-exe escolhe manualmente um .exe dentro de drive_c\n";
    stream << "  app list   lista todos os aplicativos cadastrados na biblioteca\n";
    stream << "  app run <id_ou_nome> [args...]\n";
    stream << "             executa um aplicativo cadastrado na biblioteca\n";
    stream << "  app add <arquivo.exe> [--name <Nome>] [--prefix <dir>] [--id <id>]\n";
    stream << "             cadastra manualmente um executável na biblioteca\n";
    stream << "  app remove <id>\n";
    stream << "             remove um aplicativo do catálogo da biblioteca\n\n";
    stream << "Opções Gerais de Execução:\n";
    stream << "  --trace[=canais]  escreve diagnóstico estruturado em stderr\n";
    stream << "                    canais: cli,pe,loader,imports,runtime,process,gui,crt,install (ex: --trace=pe,loader)\n";
    stream << "                    sem lista = todos os canais (compatível com WINEDEBUG)\n";
    stream << "  --trace-json <dir> grava um arquivo JSON por evento, durante a execução\n";
    stream << "  --report   relata imports suportados sem executar o arquivo\n";
    stream << "  --timeout <segundos>\n";
    stream << "             limita a execução do convidado; 0 = sem limite (padrão)\n";
    stream << "  --help     mostra esta ajuda\n";
    stream << "  --version  mostra a versão do TradutorLinux\n";
}

}  // namespace tradutorlinux
