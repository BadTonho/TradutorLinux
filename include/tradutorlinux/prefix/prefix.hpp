#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace tradutorlinux::prefix {

struct EnvironmentPaths {
    std::filesystem::path root_dir;
    std::filesystem::path dosdevices_dir;
    std::filesystem::path drive_c;
    std::filesystem::path program_files;
    std::filesystem::path program_files_x86;
    std::filesystem::path app_data_roaming;
    std::filesystem::path app_data_local;
    std::filesystem::path windows_dir;
    std::filesystem::path system32_dir;
    std::filesystem::path temp_dir;
    std::filesystem::path compat_dir;
    std::filesystem::path compat_files_dir;
};

// Retorna o caminho padrão do prefixo do TradutorLinux (~/.tradutorlinux)
[[nodiscard]] std::filesystem::path default_prefix_root();

// Diretório que agrupa os prefixos exclusivos de aplicativos cadastrados.
[[nodiscard]] std::filesystem::path managed_prefixes_root();

// Prefixo padrão de uma nova instalação ou cadastro persistente.
[[nodiscard]] std::filesystem::path default_app_prefix(std::string_view app_id);

// Inicializa a árvore de diretórios do prefixo (drive_c, dosdevices, Program Files, AppData, etc.) de forma idempotente.
[[nodiscard]] bool initialize_prefix(const std::filesystem::path& prefix_root);

// Obtém os caminhos resolvidos do prefixo.
[[nodiscard]] EnvironmentPaths get_environment_paths(const std::filesystem::path& prefix_root);

// Converte um caminho no formato Windows (ex: "C:\\Program Files\\App\\app.exe" ou "Z:\\usr\\bin")
// em um caminho absoluto resolvido no sistema de arquivos Linux usando dosdevices ou drive_c.
[[nodiscard]] std::filesystem::path resolve_windows_path(
    std::string_view win_path,
    const std::filesystem::path& prefix_root = default_prefix_root());

// Normaliza um caminho Linux para o formato Windows relativo ou com drive virtual C:\ se estiver dentro do prefixo.
[[nodiscard]] std::string to_windows_path(
    const std::filesystem::path& linux_path,
    const std::filesystem::path& prefix_root = default_prefix_root());

// Retorna true somente quando candidate está contido em parent após a
// normalização disponível no sistema de arquivos. Usado para não deixar uma
// entrada de catálogo apontar para fora do próprio prefixo por engano.
[[nodiscard]] bool is_path_within(const std::filesystem::path& candidate,
                                  const std::filesystem::path& parent);

}  // namespace tradutorlinux::prefix
