#include "tradutorlinux/prefix/prefix.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <system_error>

namespace tradutorlinux::prefix {

std::filesystem::path default_prefix_root() {
    const char* custom_prefix = std::getenv("TL_PREFIX");
    if (custom_prefix != nullptr && *custom_prefix != '\0') {
        return std::filesystem::path(custom_prefix);
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".tradutorlinux";
    }
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile != nullptr && *userprofile != '\0') {
        return std::filesystem::path(userprofile) / ".tradutorlinux";
    }
    return std::filesystem::current_path() / ".tradutorlinux";
}

std::filesystem::path managed_prefixes_root() {
    return default_prefix_root() / "prefixes";
}

std::filesystem::path default_app_prefix(const std::string_view app_id) {
    return managed_prefixes_root() / std::filesystem::path(app_id);
}

EnvironmentPaths get_environment_paths(const std::filesystem::path& prefix_root) {
    EnvironmentPaths paths;
    paths.root_dir = prefix_root;
    paths.dosdevices_dir = prefix_root / "dosdevices";
    paths.drive_c = prefix_root / "drive_c";
    paths.program_files = paths.drive_c / "Program Files";
    paths.program_files_x86 = paths.drive_c / "Program Files (x86)";
    paths.app_data_roaming = paths.drive_c / "users" / "guest" / "AppData" / "Roaming";
    paths.app_data_local = paths.drive_c / "users" / "guest" / "AppData" / "Local";
    paths.windows_dir = paths.drive_c / "windows";
    paths.system32_dir = paths.windows_dir / "system32";
    paths.temp_dir = paths.windows_dir / "temp";
    paths.compat_dir = prefix_root / "compat";
    paths.compat_files_dir = paths.compat_dir / "files";
    paths.compat_dlls_dir = paths.compat_dir / "dlls";
    return paths;
}

bool initialize_prefix(const std::filesystem::path& prefix_root) {
    std::error_code ec;
    const EnvironmentPaths paths = get_environment_paths(prefix_root);

    std::filesystem::create_directories(paths.dosdevices_dir, ec);
    if (ec) return false;
    std::filesystem::create_directories(paths.drive_c, ec);
    if (ec) return false;
    std::filesystem::create_directories(paths.program_files, ec);
    if (ec) return false;
    std::filesystem::create_directories(paths.program_files_x86, ec);
    if (ec) return false;
    std::filesystem::create_directories(paths.app_data_roaming, ec);
    if (ec) return false;
    std::filesystem::create_directories(paths.app_data_local, ec);
    if (ec) return false;
    std::filesystem::create_directories(paths.system32_dir, ec);
    if (ec) return false;
    std::filesystem::create_directories(paths.temp_dir, ec);
    if (ec) return false;
    std::filesystem::create_directories(paths.compat_files_dir, ec);
    if (ec) return false;
    std::filesystem::create_directories(paths.compat_dlls_dir, ec);
    if (ec) return false;

    // Criar symlinks de compatibilidade de caixa (Windows / System32)
    const auto win_upper = paths.drive_c / "Windows";
    if (!std::filesystem::exists(win_upper, ec) && !std::filesystem::is_symlink(win_upper, ec)) {
        std::filesystem::create_directory_symlink("windows", win_upper, ec);
    }
    const auto sys32_upper = paths.windows_dir / "System32";
    if (!std::filesystem::exists(sys32_upper, ec) && !std::filesystem::is_symlink(sys32_upper, ec)) {
        std::filesystem::create_directory_symlink("system32", sys32_upper, ec);
    }

    // Criar symlinks no dosdevices: c: -> ../drive_c e z: -> /
    // Não limpar cegamente o ec: um EACCES real (sem permissão para criar o
    // symlink) deve falhar o prefixo em vez de retornar "ok" meio-inicializado.
    const auto symlink_c = paths.dosdevices_dir / "c:";
    if (!std::filesystem::exists(symlink_c, ec) && !std::filesystem::is_symlink(symlink_c, ec)) {
        std::filesystem::create_directory_symlink("../drive_c", symlink_c, ec);
        if (ec) return false;
    }

    const auto symlink_z = paths.dosdevices_dir / "z:";
    if (!std::filesystem::exists(symlink_z, ec) && !std::filesystem::is_symlink(symlink_z, ec)) {
        std::filesystem::create_directory_symlink("/", symlink_z, ec);
        if (ec) return false;
    }

    return true;
}

std::filesystem::path resolve_windows_path(
    std::string_view win_path,
    const std::filesystem::path& prefix_root) {
    if (win_path.empty()) {
        return {};
    }

    std::string normalized;
    normalized.reserve(win_path.size());
    for (const char ch : win_path) {
        if (ch == '\\') {
            normalized.push_back('/');
        } else {
            normalized.push_back(ch);
        }
    }

    std::string_view view = normalized;

    // O prefixo Win32 de caminho estendido não muda o drive lógico. Removê-lo
    // antes da resolução evita tratar "?\\C:" como componentes do drive C.
    // UNC estendido continua fora do subconjunto de drives virtuais suportado;
    // rejeitá-lo é preferível a transformar um caminho de rede em um caminho
    // local ambíguo.
    if (view.starts_with("//?/")) {
        view.remove_prefix(4);
        if (view.starts_with("UNC/")) {
            return {};
        }
    }

    const auto confined_drive_path = [&prefix_root](const std::filesystem::path& candidate) {
        const auto drive = get_environment_paths(prefix_root).drive_c;
        std::error_code ec;
        const auto canonical_candidate = std::filesystem::weakly_canonical(candidate, ec);
        if (ec) {
            return std::filesystem::path{};
        }
        const auto canonical_drive = std::filesystem::weakly_canonical(drive, ec);
        if (ec) {
            return std::filesystem::path{};
        }
        auto candidate_it = canonical_candidate.begin();
        for (auto drive_it = canonical_drive.begin(); drive_it != canonical_drive.end();
             ++drive_it, ++candidate_it) {
            if (candidate_it == canonical_candidate.end() || *candidate_it != *drive_it) {
                return std::filesystem::path{};
            }
        }
        return canonical_candidate;
    };

    // Tratar Named Pipes (ex: "\\.\pipe\NomeDoPipe")
    if (view.starts_with("//./pipe/") || view.starts_with("/./pipe/")) {
        const auto pos = view.rfind('/');
        const std::string_view pipe_name = (pos != std::string_view::npos) ? view.substr(pos + 1) : view;
        const auto pipes_dir = prefix_root / "windows" / "temp";
        std::error_code ec;
        std::filesystem::create_directories(pipes_dir, ec);
        return pipes_dir / (std::string(pipe_name) + ".pipe");
    }

    // Tratar drive com letra (ex: "C:", "Z:", "D:")
    if (view.size() >= 2 && std::isalpha(static_cast<unsigned char>(view[0])) && view[1] == ':') {
        const char drive_letter = static_cast<char>(std::tolower(static_cast<unsigned char>(view[0])));
        view.remove_prefix(2);
        while (!view.empty() && view.front() == '/') {
            view.remove_prefix(1);
        }

        // Tenta resolver via dosdevices
        const std::string drive_link_name = std::string(1, drive_letter) + ":";
        const auto dosdevice_target = prefix_root / "dosdevices" / drive_link_name;
        std::error_code ec;
        if (std::filesystem::exists(dosdevice_target, ec) || std::filesystem::is_symlink(dosdevice_target, ec)) {
            if (drive_letter == 'c') {
                return confined_drive_path(dosdevice_target / std::filesystem::path(view));
            }
            return dosdevice_target / std::filesystem::path(view);
        }

        // Fallback para C:\ caso dosdevices não esteja configurado
        if (drive_letter == 'c') {
            return confined_drive_path(prefix_root / "drive_c" / std::filesystem::path(view));
        }
        if (drive_letter == 'z') {
            return std::filesystem::path("/") / std::filesystem::path(view);
        }
    }

    // Se começa com '/', tratar como relativo à raiz do drive_c
    if (!view.empty() && view.front() == '/') {
        while (!view.empty() && view.front() == '/') {
            view.remove_prefix(1);
        }
        return confined_drive_path(prefix_root / "drive_c" / std::filesystem::path(view));
    }

    // Caso relativo simples: se existir no diretório de trabalho atual (CWD), resolve relativo ao CWD;
    // senão faz fallback para a raiz do drive_c.
    std::error_code ec;
    const auto cwd_path = std::filesystem::current_path(ec) / std::filesystem::path(view);
    if (!ec && std::filesystem::exists(cwd_path, ec)) {
        return cwd_path;
    }
    return prefix_root / "drive_c" / std::filesystem::path(view);
}

std::string to_windows_path(
    const std::filesystem::path& linux_path,
    const std::filesystem::path& prefix_root) {
    const std::filesystem::path drive_c = prefix_root / "drive_c";
    std::error_code ec;
    const std::filesystem::path canonical_target = std::filesystem::weakly_canonical(linux_path, ec);
    const std::filesystem::path canonical_drive = std::filesystem::weakly_canonical(drive_c, ec);

    std::string result;
    auto target_it = canonical_target.begin();
    auto drive_it = canonical_drive.begin();

    bool inside_drive_c = true;
    while (drive_it != canonical_drive.end()) {
        if (target_it == canonical_target.end() || *target_it != *drive_it) {
            inside_drive_c = false;
            break;
        }
        ++target_it;
        ++drive_it;
    }

    if (inside_drive_c) {
        result = "C:";
        for (; target_it != canonical_target.end(); ++target_it) {
            result += "\\";
            result += target_it->string();
        }
    } else {
        const std::filesystem::path external_path =
            canonical_target.empty() ? linux_path : canonical_target;
        result = "Z:";
        for (const std::filesystem::path& part : external_path) {
            const std::string piece = part.string();
            if (piece.empty() || piece == "/") {
                continue;
            }
            result += "\\";
            result += piece;
        }
    }

    return result;
}

bool is_path_within(const std::filesystem::path& candidate,
                    const std::filesystem::path& parent) {
    std::error_code ec;
    const std::filesystem::path canonical_candidate =
        std::filesystem::weakly_canonical(candidate, ec);
    if (ec) {
        return false;
    }
    const std::filesystem::path canonical_parent =
        std::filesystem::weakly_canonical(parent, ec);
    if (ec) {
        return false;
    }

    auto candidate_it = canonical_candidate.begin();
    for (auto parent_it = canonical_parent.begin(); parent_it != canonical_parent.end();
         ++parent_it, ++candidate_it) {
        if (candidate_it == canonical_candidate.end() || *candidate_it != *parent_it) {
            return false;
        }
    }
    return true;
}

}  // namespace tradutorlinux::prefix
