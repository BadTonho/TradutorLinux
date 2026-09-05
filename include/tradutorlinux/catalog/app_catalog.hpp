#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tradutorlinux::catalog {

struct AppEntry {
    std::string id;                  // Slug único (ex: "notepad_plus_plus")
    std::string name;                // Nome de exibição (ex: "Notepad++")
    std::string executable_path;     // Caminho do executável (Windows ou Linux)
    std::string prefix_path;         // Caminho do prefixo (ex: ~/.tradutorlinux)
    std::string icon_path;           // Caminho para ícone PNG/SVG
    std::string working_directory;   // Diretório de trabalho
    std::vector<std::string> args;   // Argumentos padrão
    std::string created_at;          // Data/hora de cadastro ISO8601
    std::uint64_t cpu_limit_seconds{0}; // Limite padrão; 0 = sem limite
    std::uint64_t memory_limit_mib{0};  // Limite padrão em MiB; 0 = sem limite
};

class AppCatalog {
public:
    AppCatalog() = default;

    [[nodiscard]] static std::filesystem::path default_catalog_path();
    [[nodiscard]] static std::filesystem::path default_desktop_entries_dir();

    [[nodiscard]] bool load_from_file(const std::filesystem::path& path = default_catalog_path());
    [[nodiscard]] bool save_to_file(const std::filesystem::path& path = default_catalog_path()) const;

    [[nodiscard]] bool add_app(const AppEntry& app);
    [[nodiscard]] bool remove_app(std::string_view id);

    [[nodiscard]] std::optional<AppEntry> find_app(std::string_view id_or_name) const;
    [[nodiscard]] const std::vector<AppEntry>& list_apps() const noexcept { return apps_; }

    [[nodiscard]] static std::string generate_id(std::string_view name_or_filename);
    [[nodiscard]] static bool create_desktop_entry(const AppEntry& app, const std::filesystem::path& destination_dir = {});

private:
    std::vector<AppEntry> apps_;
};

}  // namespace tradutorlinux::catalog
