#include "tradutorlinux/backend/proton.hpp"

#include "tradutorlinux/compat/materializer.hpp"
#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/util/sha256.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace tradutorlinux::backend {
namespace {

struct CreatedPath {
    std::filesystem::path path;
    bool directory{false};
    std::uint64_t device{0};
    std::uint64_t inode{0};
};

struct SourceEntry {
    std::filesystem::path relative;
    std::filesystem::path source;
    bool directory{false};
};

struct StagedApplication {
    std::filesystem::path source_root;
    std::filesystem::path target_root;
    std::filesystem::path executable;
    std::vector<SourceEntry> entries;
    std::vector<CreatedPath> created;
};

void write_proton_trace(const ProtonRunRequest& request, std::ostream& stream,
                        const diagnostics::TraceLevel level, const std::string_view event,
                        const std::initializer_list<diagnostics::TraceField> fields) {
    if (!request.trace_enabled) return;
    diagnostics::write_trace(stream, diagnostics::TraceComponent::Proton, level, event,
                             std::span<const diagnostics::TraceField>{fields.begin(), fields.size()});
}

[[nodiscard]] std::string errno_message(const std::string_view operation, const int error) {
    return std::string{operation} + ": " + std::strerror(error);
}

[[nodiscard]] bool same_identity(const struct stat& status,
                                 const CreatedPath& created) noexcept {
    return static_cast<std::uint64_t>(status.st_dev) == created.device &&
           static_cast<std::uint64_t>(status.st_ino) == created.inode;
}

[[nodiscard]] bool read_lstat(const std::filesystem::path& path, struct stat& status,
                              bool& exists, std::string& error) {
    if (::lstat(path.c_str(), &status) == 0) {
        exists = true;
        return true;
    }
    if (errno == ENOENT) {
        exists = false;
        return true;
    }
    error = errno_message("não foi possível consultar " + path.string(), errno);
    return false;
}

[[nodiscard]] bool relative_path(const std::filesystem::path& root,
                                 const std::filesystem::path& candidate,
                                 std::filesystem::path& relative,
                                 std::string& error) {
    relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute() || relative == ".") {
        if (relative == ".") return true;
        error = "caminho do aplicativo fora do prefixo Proton";
        return false;
    }
    for (const auto& component : relative) {
        if (component == ".." || component.empty()) {
            error = "caminho do aplicativo fora do prefixo Proton";
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool validate_path_without_symlinks(
    const std::filesystem::path& root, const std::filesystem::path& candidate,
    const bool final_must_be_directory, std::string& error) {
    struct stat root_status{};
    bool root_exists = false;
    if (!read_lstat(root, root_status, root_exists, error) || !root_exists ||
        !S_ISDIR(root_status.st_mode)) {
        if (error.empty()) error = "raiz de staging não é um diretório";
        return false;
    }

    std::filesystem::path relative;
    if (!relative_path(root, candidate, relative, error)) return false;
    if (relative == ".") return !final_must_be_directory || S_ISDIR(root_status.st_mode);

    std::filesystem::path current = root;
    std::size_t index = 0;
    const std::size_t component_count = static_cast<std::size_t>(std::distance(relative.begin(), relative.end()));
    for (const auto& component : relative) {
        current /= component;
        struct stat status{};
        bool exists = false;
        if (!read_lstat(current, status, exists, error)) return false;
        const bool final_component = index + 1U == component_count;
        if (exists) {
            if (S_ISLNK(status.st_mode)) {
                error = "caminho de staging contém symlink";
                return false;
            }
            if (!final_component && !S_ISDIR(status.st_mode)) {
                error = "componente intermediário do staging não é diretório";
                return false;
            }
            if (final_component && final_must_be_directory && !S_ISDIR(status.st_mode)) {
                error = "destino de staging não é diretório";
                return false;
            }
        }
        ++index;
    }
    return true;
}

[[nodiscard]] bool ensure_directory_path(const std::filesystem::path& root,
                                          const std::filesystem::path& target,
                                          std::vector<CreatedPath>& created,
                                          std::string& error) {
    if (!validate_path_without_symlinks(root, target, true, error)) {
        // A missing final directory is valid: the function creates it below.
        if (error != "destino de staging não é diretório") return false;
        error.clear();
    }

    std::filesystem::path relative;
    if (!relative_path(root, target, relative, error)) return false;
    if (relative == ".") return true;

    std::filesystem::path current = root;
    for (const auto& component : relative) {
        current /= component;
        struct stat status{};
        bool exists = false;
        if (!read_lstat(current, status, exists, error)) return false;
        if (exists) {
            if (S_ISLNK(status.st_mode) || !S_ISDIR(status.st_mode)) {
                error = "diretório de staging inválido";
                return false;
            }
            continue;
        }
        if (::mkdir(current.c_str(), 0755) != 0) {
            if (errno == EEXIST) {
                continue;
            }
            error = errno_message("não foi possível criar diretório de staging", errno);
            return false;
        }
        if (::lstat(current.c_str(), &status) != 0 || !S_ISDIR(status.st_mode)) {
            error = errno_message("diretório de staging criado com tipo inválido", errno);
            return false;
        }
        static_cast<void>(::chmod(current.c_str(), 0755));
        created.push_back(CreatedPath{current, true,
                                      static_cast<std::uint64_t>(status.st_dev),
                                      static_cast<std::uint64_t>(status.st_ino)});
    }
    return true;
}

[[nodiscard]] bool collect_source_entries(const std::filesystem::path& source_root,
                                          std::vector<SourceEntry>& entries,
                                          std::string& error) {
    struct stat root_status{};
    bool root_exists = false;
    if (!read_lstat(source_root, root_status, root_exists, error) || !root_exists) {
        if (error.empty()) error = "diretório do aplicativo ausente";
        return false;
    }
    if (S_ISLNK(root_status.st_mode) || !S_ISDIR(root_status.st_mode)) {
        error = "diretório do aplicativo deve ser regular e sem symlink";
        return false;
    }

    std::error_code ec;
    std::filesystem::recursive_directory_iterator iterator{source_root, ec};
    if (ec) {
        error = "não foi possível percorrer o aplicativo: " + ec.message();
        return false;
    }
    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(ec)) {
        if (ec) {
            error = "não foi possível percorrer o aplicativo: " + ec.message();
            return false;
        }
        const auto& entry = *iterator;
        const auto source = entry.path();
        const auto relative = source.lexically_relative(source_root);
        if (relative.empty() || relative == ".") {
            error = "caminho relativo inválido no aplicativo";
            return false;
        }
        struct stat status{};
        bool exists = false;
        if (!read_lstat(source, status, exists, error) || !exists) {
            if (error.empty()) error = "entrada do aplicativo desapareceu durante o staging";
            return false;
        }
        if (S_ISLNK(status.st_mode)) {
            error = "o staging do Proton rejeita symlinks no aplicativo";
            return false;
        }
        if (S_ISDIR(status.st_mode)) {
            entries.push_back(SourceEntry{relative, source, true});
        } else if (S_ISREG(status.st_mode)) {
            entries.push_back(SourceEntry{relative, source, false});
        } else {
            error = "o staging do Proton aceita somente arquivos e diretórios regulares";
            return false;
        }
    }
    if (ec) {
        error = "não foi possível finalizar o staging do aplicativo: " + ec.message();
        return false;
    }
    std::sort(entries.begin(), entries.end(), [](const SourceEntry& first, const SourceEntry& second) {
        return first.relative.generic_string() < second.relative.generic_string();
    });
    return true;
}

[[nodiscard]] bool files_equal(const std::filesystem::path& source,
                               const std::filesystem::path& target,
                               std::string& error) {
    std::error_code ec;
    const auto source_size = std::filesystem::file_size(source, ec);
    if (ec) {
        error = "não foi possível consultar o tamanho da origem: " + ec.message();
        return false;
    }
    const auto target_size = std::filesystem::file_size(target, ec);
    if (ec) {
        error = "não foi possível consultar o tamanho do destino: " + ec.message();
        return false;
    }
    if (source_size != target_size) return false;

    std::ifstream source_stream(source, std::ios::binary);
    std::ifstream target_stream(target, std::ios::binary);
    if (!source_stream || !target_stream) {
        error = "não foi possível abrir arquivos para comparação de staging";
        return false;
    }
    std::array<char, 64U * 1024U> source_buffer{};
    std::array<char, 64U * 1024U> target_buffer{};
    while (source_stream && target_stream) {
        source_stream.read(source_buffer.data(), static_cast<std::streamsize>(source_buffer.size()));
        target_stream.read(target_buffer.data(), static_cast<std::streamsize>(target_buffer.size()));
        const auto source_count = source_stream.gcount();
        const auto target_count = target_stream.gcount();
        if (source_count != target_count ||
            !std::equal(source_buffer.begin(), source_buffer.begin() + source_count,
                        target_buffer.begin())) {
            return false;
        }
        if (source_count == 0) break;
    }
    if (source_stream.bad() || target_stream.bad()) {
        error = "não foi possível ler arquivos para comparação de staging";
        return false;
    }
    return true;
}

[[nodiscard]] bool preflight_entries(const std::filesystem::path& target_drive,
                                     const std::filesystem::path& target_root,
                                     const std::vector<SourceEntry>& entries,
                                     std::string& error) {
    if (!validate_path_without_symlinks(target_drive, target_root, true, error)) {
        if (error != "destino de staging não é diretório") return false;
        error.clear();
    }
    for (const SourceEntry& entry : entries) {
        const std::filesystem::path target = target_root / entry.relative;
        if (!validate_path_without_symlinks(target_drive, target, entry.directory, error)) {
            if (error == "destino de staging não é diretório" && !entry.directory) {
                error = "colisão de staging: destino não é arquivo regular";
            }
            return false;
        }
        struct stat status{};
        bool exists = false;
        if (!read_lstat(target, status, exists, error)) return false;
        if (!exists) continue;
        if (S_ISLNK(status.st_mode)) {
            error = "colisão de staging com symlink";
            return false;
        }
        if (entry.directory) {
            if (!S_ISDIR(status.st_mode)) {
                error = "colisão de staging: diretório esperado";
                return false;
            }
        } else {
            if (!S_ISREG(status.st_mode)) {
                error = "colisão de staging: arquivo regular esperado";
                return false;
            }
            std::string comparison_error;
            if (!files_equal(entry.source, target, comparison_error)) {
                if (!comparison_error.empty()) {
                    error = std::move(comparison_error);
                } else {
                    error = "conflito de staging: arquivo existente foi modificado";
                }
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool copy_regular_file(const SourceEntry& entry,
                                      const std::filesystem::path& target,
                                      std::vector<CreatedPath>& created,
                                      std::string& error) {
    const int source_fd = ::open(entry.source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (source_fd < 0) {
        error = errno_message("não foi possível abrir a origem do staging", errno);
        return false;
    }
    struct stat source_status{};
    if (::fstat(source_fd, &source_status) != 0 || !S_ISREG(source_status.st_mode)) {
        const int saved_errno = errno;
        ::close(source_fd);
        error = errno_message("origem do staging não é arquivo regular", saved_errno);
        return false;
    }

    const int target_fd = ::open(target.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                                 0644);
    if (target_fd < 0) {
        const int saved_errno = errno;
        ::close(source_fd);
        error = saved_errno == EEXIST ? "conflito de staging: destino apareceu durante a cópia"
                                      : errno_message("não foi possível criar arquivo de staging", saved_errno);
        return false;
    }
    struct stat target_status{};
    if (::fstat(target_fd, &target_status) != 0 || !S_ISREG(target_status.st_mode) ||
        ::fchmod(target_fd, 0644) != 0) {
        const int saved_errno = errno;
        ::close(source_fd);
        ::close(target_fd);
        static_cast<void>(::unlink(target.c_str()));
        error = errno_message("arquivo de staging criado com tipo inválido", saved_errno);
        return false;
    }
    created.push_back(CreatedPath{target, false,
                                  static_cast<std::uint64_t>(target_status.st_dev),
                                  static_cast<std::uint64_t>(target_status.st_ino)});

    std::array<char, 64U * 1024U> buffer{};
    bool success = true;
    while (true) {
        const ssize_t read_count = ::read(source_fd, buffer.data(), buffer.size());
        if (read_count > 0) {
            std::size_t written = 0;
            while (written < static_cast<std::size_t>(read_count)) {
                const ssize_t write_count = ::write(target_fd, buffer.data() + written,
                                                    static_cast<std::size_t>(read_count) - written);
                if (write_count > 0) {
                    written += static_cast<std::size_t>(write_count);
                } else if (write_count < 0 && errno == EINTR) {
                    continue;
                } else {
                    error = errno_message("não foi possível gravar arquivo de staging", errno);
                    success = false;
                    break;
                }
            }
            if (!success) break;
            continue;
        }
        if (read_count == 0) break;
        if (errno == EINTR) continue;
        error = errno_message("não foi possível ler arquivo de staging", errno);
        success = false;
        break;
    }
    if (::close(source_fd) != 0) success = false;
    if (::close(target_fd) != 0) success = false;
    if (!success && error.empty()) error = "não foi possível finalizar arquivo de staging";
    return success;
}

[[nodiscard]] bool copy_entries(StagedApplication& staged, const std::filesystem::path& target_drive,
                                std::string& error) {
    if (!ensure_directory_path(target_drive, staged.target_root, staged.created, error)) {
        return false;
    }
    for (const SourceEntry& entry : staged.entries) {
        const std::filesystem::path target = staged.target_root / entry.relative;
        if (entry.directory) {
            if (!ensure_directory_path(target_drive, target, staged.created, error)) return false;
            continue;
        }
        struct stat status{};
        bool exists = false;
        if (!read_lstat(target, status, exists, error)) return false;
        if (exists) continue;  // preflight already verified byte identity.
        if (!copy_regular_file(entry, target, staged.created, error)) return false;
    }
    return true;
}

[[nodiscard]] bool rollback_paths(std::vector<CreatedPath>& created, std::string& error) noexcept {
    bool success = true;
    for (auto iterator = created.rbegin(); iterator != created.rend(); ++iterator) {
        struct stat status{};
        if (::lstat(iterator->path.c_str(), &status) != 0) {
            if (errno != ENOENT) success = false;
            continue;
        }
        if (!same_identity(status, *iterator)) {
            success = false;
            continue;
        }
        const int result = iterator->directory ? ::rmdir(iterator->path.c_str())
                                               : ::unlink(iterator->path.c_str());
        if (result != 0 && errno != ENOENT) success = false;
    }
    if (!success && error.empty()) error = "não foi possível desfazer staging parcial";
    return success;
}

[[nodiscard]] std::string json_escape(const std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8U);
    for (const char character : value) {
        switch (character) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += character; break;
        }
    }
    return result;
}

[[nodiscard]] bool write_manifest(const std::filesystem::path& manifest,
                                  const StagedApplication& staged, const std::string_view app_id,
                                  const std::string_view proton_version,
                                  std::string& error) {
    std::string contents;
    contents += "{\n  \"schema\": 1,\n  \"app_id\": \"";
    contents += json_escape(app_id);
    contents += "\",\n  \"proton_version\": \"";
    contents += json_escape(proton_version);
    contents += "\",\n  \"source_root\": \"";
    contents += json_escape(staged.source_root.string());
    contents += "\",\n  \"target_root\": \"";
    contents += json_escape(staged.target_root.string());
    contents += "\",\n  \"files\": [\n";
    bool first = true;
    for (const SourceEntry& entry : staged.entries) {
        if (entry.directory) continue;
        const auto digest = util::sha256_file(entry.source);
        if (!digest.has_value()) {
            error = "não foi possível calcular hash do arquivo no manifesto Proton";
            return false;
        }
        if (!first) contents += ",\n";
        first = false;
        contents += "    {\"path\": \"";
        contents += json_escape(entry.relative.generic_string());
        contents += "\", \"sha256\": \"";
        contents += *digest;
        contents += "\"}";
    }
    contents += "\n  ]\n}\n";

    const std::filesystem::path temporary =
        manifest.parent_path() / (manifest.filename().string() + ".tmp-" +
                                  std::to_string(static_cast<long long>(::getpid())));
    std::error_code ec;
    if (std::filesystem::exists(temporary, ec) || ec) {
        error = ec ? "não foi possível consultar manifesto temporário Proton: " + ec.message()
                   : "manifesto temporário Proton já existe";
        return false;
    }
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "não foi possível criar manifesto do prefixo Proton";
            return false;
        }
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!output) {
            static_cast<void>(std::filesystem::remove(temporary, ec));
            error = "não foi possível gravar manifesto do prefixo Proton";
            return false;
        }
    }
    std::filesystem::rename(temporary, manifest, ec);
    if (ec) {
        static_cast<void>(std::filesystem::remove(temporary, ec));
        error = "não foi possível publicar manifesto do prefixo Proton: " + ec.message();
        return false;
    }
    return true;
}

[[nodiscard]] bool prepare_directories(const std::filesystem::path& prefix_root,
                                       const std::filesystem::path& proton_root,
                                       const std::filesystem::path& data_root,
                                       const std::filesystem::path& pfx_root,
                                       const std::filesystem::path& client_root,
                                       std::vector<CreatedPath>& created, std::string& error) {
    for (const auto& directory : {proton_root, data_root, pfx_root, pfx_root / "drive_c",
                                  client_root}) {
        if (!ensure_directory_path(prefix_root, directory, created, error)) return false;
    }
    return true;
}

}  // namespace

ProtonRunResult run_proton_application(const ProtonConfig& config,
                                       const compat::Profile& profile,
                                       const ProtonRunRequest& request,
                                       std::ostream& diagnostic_stream) {
    ProtonRunResult result;
    if (profile.backend.kind != compat::BackendKind::Proton) {
        result.error = "o perfil não selecionou o backend Proton";
        return result;
    }
    if (!profile.dlls.empty()) {
        result.status = ProtonRunStatus::Unsupported;
        result.error = "o backend Proton não aceita dlls[] do perfil";
        write_proton_trace(request, diagnostic_stream, diagnostics::TraceLevel::Warning,
                           "provider-rejected", { {"reason", "profile-dlls"} });
        return result;
    }

    const ProtonValidationResult validation = validate_proton(config, profile.backend.min_version);
    if (!validation.valid) {
        result.status = ProtonRunStatus::Unsupported;
        result.error = validation.error;
        write_proton_trace(request, diagnostic_stream, diagnostics::TraceLevel::Warning,
                           "provider-rejected", { {"reason", validation.error} });
        return result;
    }
    result.version = validation.version;
    write_proton_trace(request, diagnostic_stream, diagnostics::TraceLevel::Info,
                       "provider-selected", { {"kind", "proton"}, {"version", result.version} });

    std::error_code ec;
    const std::filesystem::path prefix_root =
        std::filesystem::weakly_canonical(request.prefix_root, ec);
    if (ec || prefix_root.empty()) {
        result.status = ProtonRunStatus::InternalError;
        result.error = "não foi possível normalizar o prefixo do aplicativo";
        return result;
    }
    const auto paths = prefix::get_environment_paths(prefix_root);
    const std::filesystem::path executable =
        std::filesystem::weakly_canonical(request.executable, ec);
    const std::filesystem::path drive_c = std::filesystem::weakly_canonical(paths.drive_c, ec);
    if (ec || executable.empty() || drive_c.empty() || !prefix::is_path_within(executable, drive_c)) {
        result.status = ProtonRunStatus::Unsupported;
        result.error = "o executável do Proton deve estar dentro de drive_c";
        write_proton_trace(request, diagnostic_stream, diagnostics::TraceLevel::Warning,
                           "staging-rejected", { {"reason", result.error} });
        return result;
    }

    const std::filesystem::path source_root = executable.parent_path();
    std::filesystem::path source_relative;
    std::string error;
    if (!relative_path(drive_c, source_root, source_relative, error) ||
        !validate_path_without_symlinks(drive_c, source_root, true, error)) {
        result.status = ProtonRunStatus::InternalError;
        result.error = error.empty() ? "diretório do aplicativo inválido" : error;
        return result;
    }

    std::vector<CreatedPath> infrastructure_created;
    const std::filesystem::path proton_root = prefix_root / "proton";
    const std::filesystem::path data_root = proton_root / "compatdata";
    const std::filesystem::path pfx_root = data_root / "pfx";
    const std::filesystem::path client_root = proton_root / "client";
    if (!prepare_directories(prefix_root, proton_root, data_root, pfx_root, client_root,
                             infrastructure_created, error)) {
        static_cast<void>(rollback_paths(infrastructure_created, error));
        result.status = ProtonRunStatus::InternalError;
        result.error = std::move(error);
        return result;
    }
    const std::filesystem::path target_drive = pfx_root / "drive_c";
    const std::filesystem::path target_root = target_drive / source_relative;

    StagedApplication staged{source_root, target_root, target_root / executable.filename(), {}, {}};
    if (!collect_source_entries(source_root, staged.entries, error) ||
        !preflight_entries(target_drive, target_root, staged.entries, error) ||
        !copy_entries(staged, target_drive, error)) {
        static_cast<void>(rollback_paths(staged.created, error));
        static_cast<void>(rollback_paths(infrastructure_created, error));
        result.status = ProtonRunStatus::InternalError;
        result.error = std::move(error);
        write_proton_trace(request, diagnostic_stream, diagnostics::TraceLevel::Error,
                           "staging-rejected", { {"reason", result.error} });
        return result;
    }
    write_proton_trace(request, diagnostic_stream, diagnostics::TraceLevel::Info,
                       "staging-complete", { {"source", source_root.string()},
                                              {"target", target_root.string()} });

    compat::FileExposure exposure =
        compat::FileExposure::materialize_into(prefix_root, pfx_root, profile);
    if (!exposure.applied()) {
        error = exposure.error().empty() ? "não foi possível materializar files[] no Proton"
                                         : std::string{exposure.error()};
        static_cast<void>(exposure.cleanup());
        static_cast<void>(rollback_paths(staged.created, error));
        static_cast<void>(rollback_paths(infrastructure_created, error));
        result.status = ProtonRunStatus::InternalError;
        result.error = std::move(error);
        write_proton_trace(request, diagnostic_stream, diagnostics::TraceLevel::Error,
                           "files-rejected", { {"reason", result.error} });
        return result;
    }

    const std::filesystem::path manifest = proton_root / "application-manifest.json";
    if (!write_manifest(manifest, staged, request.app_id, validation.version, error)) {
        static_cast<void>(exposure.cleanup());
        static_cast<void>(rollback_paths(staged.created, error));
        static_cast<void>(rollback_paths(infrastructure_created, error));
        result.status = ProtonRunStatus::InternalError;
        result.error = std::move(error);
        return result;
    }

    result.proton_data_root = data_root;
    result.staged_executable = staged.executable;
    std::vector<std::string> argv;
    argv.reserve(3U + request.guest_arguments.size());
    argv.push_back((config.root / "proton").string());
    // `run` hands the command to Proton's Steam-compatible steam.exe shim.
    // The adapter runs arbitrary applications outside the Steam client, so use
    // Proton's direct launcher mode instead.
    argv.emplace_back("runinprefix");
    argv.push_back(staged.executable.string());
    argv.insert(argv.end(), request.guest_arguments.begin(), request.guest_arguments.end());
    const std::vector<process::ExternalEnvironmentVariable> environment{
        {"STEAM_COMPAT_DATA_PATH", data_root.string()},
        {"WINEPREFIX", pfx_root.string()},
        {"STEAM_COMPAT_CLIENT_INSTALL_PATH", client_root.string()},
        {"STEAM_COMPAT_INSTALL_PATH", proton_root.string()},
    };
    write_proton_trace(request, diagnostic_stream, diagnostics::TraceLevel::Info,
                       "launch", { {"launcher", argv.front()}, {"working-directory", target_root.string()} });
    result.outcome = process::run_external_isolated(
        argv, environment, request.timeout_ms, request.resource_limits, target_root,
        diagnostic_stream, "[tl][proton] ");
    result.status = ProtonRunStatus::Completed;

    const bool cleanup_ok = exposure.cleanup();
    const auto& cleanup = exposure.cleanup_summary();
    write_proton_trace(request, diagnostic_stream,
                       cleanup_ok ? diagnostics::TraceLevel::Info : diagnostics::TraceLevel::Warning,
                       cleanup_ok ? "files-cleanup" : "files-cleanup-failed",
                       { {"removed-files", std::to_string(cleanup.files_removed)},
                         {"removed-directories", std::to_string(cleanup.directories_removed)},
                         {"retained", std::to_string(cleanup.paths_retained)} });
    if (!cleanup_ok) {
        diagnostic_stream << "aviso: não foi possível limpar completamente os arquivos de compatibilidade do Proton\n";
    }
    if (result.outcome.kind == process::GuestOutcomeKind::Exited) {
        write_proton_trace(request, diagnostic_stream, diagnostics::TraceLevel::Info,
                           "exit", { {"exit-code", std::to_string(result.outcome.exit_code)} });
    } else {
        write_proton_trace(request, diagnostic_stream, diagnostics::TraceLevel::Error,
                           "terminated", { {"kind", std::to_string(static_cast<int>(result.outcome.kind))} });
    }
    return result;
}

}  // namespace tradutorlinux::backend
