#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace tradutorlinux {

enum class ExitCode : int {
    Success = 0,
    Usage = 2,
    InputUnavailable = 3,
    MalformedPe = 4,
    Unsupported = 5,
    // O setup terminou, mas nenhum executável final pôde ser registrado sem
    // escolha do usuário. Não representa falha do setup.
    InstallPending = 6,
    InternalError = 70,
    GuestFault = 71,
    GuestTimeout = 72,
};

enum class CommandMode {
    DirectRun,   // Execução tradicional de um arquivo .exe
    Install,     // Instalação de aplicativo no prefixo virtual com auto-cadastro
    AppList,     // Listar biblioteca de aplicativos cadastrados
    AppRun,      // Executar aplicativo da biblioteca
    AppAdd,      // Cadastrar aplicativo manualmente na biblioteca
    AppRemove,   // Remover aplicativo do catálogo da biblioteca
};

struct CommandLine {
    CommandMode mode{CommandMode::DirectRun};
    bool show_help{false};
    bool show_version{false};
    bool trace_enabled{false};
    std::vector<std::string> trace_channels_raw;
    bool report_only{false};
    // Tempo máximo de execução do convidado, em milissegundos; 0 = sem limite.
    std::uint64_t timeout_ms{0};
    bool timeout_set{false};
    std::optional<std::filesystem::path> executable_path;
    // Executável final de uma instalação, relativo a C:\\ ou absoluto dentro
    // do drive_c do prefixo escolhido.
    std::optional<std::filesystem::path> installed_executable_path;
    std::optional<std::filesystem::path> custom_prefix;
    // Contexto interno do catálogo; não é uma opção da execução direta.
    std::optional<std::filesystem::path> guest_working_directory;
    std::string app_id;
    std::string app_name;
    // Argumentos encaminhados ao programa convidado (argv[1..]), na ordem em
    // que foram informados depois do executável. argv[0] é o caminho do
    // executável informado na linha de comando.
    std::vector<std::string> guest_arguments;
};

struct ParseResult {
    std::optional<CommandLine> command_line;
    std::string error_message;
};

[[nodiscard]] ParseResult parse_command_line(int argc, const char* const argv[]);
[[nodiscard]] ExitCode run_command(const CommandLine& command_line, std::ostream& stdout_stream,
                                   std::ostream& stderr_stream);
void print_help(std::ostream& stream);

}  // namespace tradutorlinux
