#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tradutorlinux::package {

struct AppxApplication {
    std::string id;
    std::string executable;
    std::string display_name;
    std::string entry_point;
};

struct AppxPackageInfo {
    std::string package_name;
    std::string publisher;
    std::string version;
    std::vector<AppxApplication> applications;
    std::optional<std::string> main_executable;
};

// Verifica se o arquivo é um pacote MSIX ou AppX válido (.msix / .appx com assinatura ZIP)
[[nodiscard]] bool is_msix_or_appx_package(const std::filesystem::path& path);

// Parseia o conteúdo de um AppxManifest.xml e extrai os metadados de aplicação
[[nodiscard]] std::optional<AppxPackageInfo> parse_appx_manifest_xml(std::string_view xml_content);

// Inspeciona um pacote MSIX/AppX e extrai informações do AppxManifest.xml com validação de segurança
[[nodiscard]] std::optional<AppxPackageInfo> inspect_msix_package(const std::filesystem::path& package_path);

// Extrai um pacote validado para um diretório novo e retorna o executável
// declarado pelo manifesto. A extração rejeita traversal, links simbólicos,
// colisões de nomes e métodos ZIP fora de stored/deflate.
[[nodiscard]] std::optional<std::filesystem::path> extract_msix_package(
    const std::filesystem::path& package_path,
    const std::filesystem::path& destination);

}  // namespace tradutorlinux::package
