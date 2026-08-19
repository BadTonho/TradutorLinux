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

    // Criar symlinks no dosdevices: c: -> ../drive_c e z: -> /
    const auto symlink_c = paths.dosdevices_dir / "c:";
    if (!std::filesystem::exists(symlink_c, ec) && !std::filesystem::is_symlink(symlink_c, ec)) {
        std::filesystem::create_directory_symlink("../drive_c", symlink_c, ec);
        ec.clear();
    }

    const auto symlink_z = paths.dosdevices_dir / "z:";
    if (!std::filesystem::exists(symlink_z, ec) && !std::filesystem::is_symlink(symlink_z, ec)) {
        std::filesystem::create_directory_symlink("/", symlink_z, ec);
        ec.clear();
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
            return dosdevice_target / std::filesystem::path(view);
        }

        // Fallback para C:\ caso dosdevices não esteja configurado
        if (drive_letter == 'c') {
            return prefix_root / "drive_c" / std::filesystem::path(view);
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
        return prefix_root / "drive_c" / std::filesystem::path(view);
    }

    // Caso relativo simples
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
        result = linux_path.string();
        std::replace(result.begin(), result.end(), '/', '\\');
    }

    return result;
}

}  // namespace tradutorlinux::prefix
