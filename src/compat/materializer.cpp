#include "tradutorlinux/compat/materializer.hpp"

#include "tradutorlinux/prefix/prefix.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <sys/stat.h>
#include <unistd.h>

namespace tradutorlinux::compat {
namespace {

struct PendingMapping {
    ExposedFile exposed;
};

[[nodiscard]] std::string system_error_message(const std::string_view operation,
                                               const int error) {
    return std::string{operation} + ": " + std::strerror(error);
}

[[nodiscard]] bool missing_status(const std::filesystem::file_status& status) noexcept {
    return status.type() == std::filesystem::file_type::not_found ||
           status.type() == std::filesystem::file_type::none;
}

[[nodiscard]] bool read_status(const std::filesystem::path& path,
                               std::filesystem::file_status& status,
                               std::string& error) {
    std::error_code ec;
    status = std::filesystem::symlink_status(path, ec);
    if (!ec) return true;
    if (ec == std::errc::no_such_file_or_directory) {
        status = std::filesystem::file_status{std::filesystem::file_type::not_found};
        return true;
    }
    error = system_error_message("não foi possível consultar " + path.string(), ec.value());
    return false;
}

[[nodiscard]] bool path_is_within(const std::filesystem::path& candidate,
                                  const std::filesystem::path& parent) {
    return prefix::is_path_within(candidate, parent);
}

[[nodiscard]] bool same_identity(const struct stat& status,
                                  const std::uint64_t device,
                                  const std::uint64_t inode) noexcept {
    return static_cast<std::uint64_t>(status.st_dev) == device &&
           static_cast<std::uint64_t>(status.st_ino) == inode;
}

[[nodiscard]] bool validate_path_components(const std::filesystem::path& root,
                                            const std::filesystem::path& candidate,
                                            const std::string_view role,
                                            std::string& error) {
    const std::filesystem::path relative = candidate.lexically_relative(root);
    if (relative.empty() || relative == "." || relative == ".." ||
        relative.string().starts_with("../")) {
        error = std::string{role} + " fora do diretório permitido";
        return false;
    }

    std::filesystem::path current = root;
    for (const auto& component : relative) {
        if (component.empty() || component == ".") continue;
        if (component == "..") {
            error = std::string{role} + " fora do diretório permitido";
            return false;
        }
        current /= component;
        std::filesystem::file_status status;
        if (!read_status(current, status, error)) return false;
        if (std::filesystem::is_symlink(status)) {
            error = std::string{role} + " não pode conter symlink";
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::filesystem::path> target_path(
    const std::filesystem::path& prefix_root,
    const std::string_view target,
    std::string& error) {
    if (target.size() < 3U ||
        (target[0] != 'C' && target[0] != 'c') || target[1] != ':' ||
        (target[2] != '\\' && target[2] != '/')) {
        error = "destino não começa por C:\\";
        return std::nullopt;
    }

    const auto paths = prefix::get_environment_paths(prefix_root);
    std::string relative{target.substr(3)};
    std::replace(relative.begin(), relative.end(), '\\', '/');
    const std::filesystem::path lexical_target =
        (paths.drive_c / std::filesystem::path{relative}).lexically_normal();
    if (!path_is_within(lexical_target, paths.drive_c)) {
        error = "destino de arquivo fora de drive_c";
        return std::nullopt;
    }
    if (!validate_path_components(paths.drive_c, lexical_target,
                                  "destino de arquivo", error)) {
        return std::nullopt;
    }

    const std::filesystem::path resolved =
        prefix::resolve_windows_path(target, prefix_root);
    if (resolved.empty() || !path_is_within(resolved, paths.drive_c)) {
        error = "destino de arquivo fora de drive_c";
        return std::nullopt;
    }

    std::filesystem::file_status lexical_status;
    if (!read_status(lexical_target, lexical_status, error)) return std::nullopt;
    if (std::filesystem::is_symlink(lexical_status)) {
        error = "destino de arquivo não pode ser um symlink";
        return std::nullopt;
    }
    if (!missing_status(lexical_status)) {
        error = "destino de arquivo já existe";
        return std::nullopt;
    }

    std::filesystem::file_status resolved_status;
    if (!read_status(resolved, resolved_status, error)) return std::nullopt;
    if (std::filesystem::is_symlink(resolved_status)) {
        error = "destino de arquivo não pode ser um symlink";
        return std::nullopt;
    }
    if (!missing_status(resolved_status)) {
        error = "destino de arquivo já existe";
        return std::nullopt;
    }

    return resolved;
}

[[nodiscard]] bool collect_parent_directories(
    const std::filesystem::path& drive_c,
    const std::filesystem::path& target,
    std::vector<std::filesystem::path>& required,
    std::string& error) {
    const std::filesystem::path relative =
        target.parent_path().lexically_relative(drive_c);
    if (relative.empty() || relative == ".") return true;
    if (relative == ".." || relative.string().starts_with("../")) {
        error = "diretório-pai do destino escaparia de drive_c";
        return false;
    }

    std::filesystem::path current = drive_c;
    for (const auto& component : relative) {
        if (component.empty() || component == ".") continue;
        if (component == "..") {
            error = "diretório-pai do destino escaparia de drive_c";
            return false;
        }
        current /= component;
        std::filesystem::file_status status;
        if (!read_status(current, status, error)) return false;
        if (std::filesystem::is_symlink(status)) {
            error = "diretório-pai do destino não pode ser um symlink";
            return false;
        }
        if (!missing_status(status)) {
            if (!std::filesystem::is_directory(status)) {
                error = "diretório-pai do destino não é um diretório";
                return false;
            }
            continue;
        }
        if (std::find(required.begin(), required.end(), current) == required.end()) {
            required.push_back(current);
        }
    }
    return true;
}

[[nodiscard]] bool regular_source(const std::filesystem::path& source,
                                  const std::filesystem::path& files_dir,
                                  std::string& error) {
    if (!path_is_within(source, files_dir) ||
        !validate_path_components(files_dir, source, "origem de arquivo", error)) {
        if (error.empty()) error = "origem de arquivo ausente ou fora de compat/files";
        return false;
    }
    std::filesystem::file_status status;
    if (!read_status(source, status, error)) return false;
    if (std::filesystem::is_symlink(status)) {
        error = "origem de arquivo não pode ser um symlink";
        return false;
    }
    if (!std::filesystem::is_regular_file(status)) {
        error = "origem de arquivo ausente ou fora de compat/files";
        return false;
    }
    return true;
}

[[nodiscard]] bool write_all(const int fd, const char* data, const std::size_t size,
                             std::string& error) {
    std::size_t written = 0;
    while (written < size) {
        const ssize_t result = ::write(fd, data + written, size - written);
        if (result > 0) {
            written += static_cast<std::size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR) continue;
        error = system_error_message("não foi possível gravar arquivo de compatibilidade", errno);
        return false;
    }
    return true;
}

}  // namespace

bool FileExposure::copy_file_exclusive(const ExposedFile& exposed,
                                       std::string& error) {
    const int source_fd = ::open(exposed.source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (source_fd < 0) {
        error = system_error_message("não foi possível abrir a origem " + exposed.source.string(), errno);
        return false;
    }

    struct stat source_status{};
    if (::fstat(source_fd, &source_status) != 0) {
        const int saved_errno = errno;
        ::close(source_fd);
        error = system_error_message("origem de arquivo não é regular", saved_errno);
        return false;
    }
    if (!S_ISREG(source_status.st_mode)) {
        ::close(source_fd);
        error = "origem de arquivo não é regular";
        return false;
    }

    const int target_fd = ::open(exposed.target.c_str(),
                                 O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                                 0644);
    if (target_fd < 0) {
        const int saved_errno = errno;
        ::close(source_fd);
        if (saved_errno == EEXIST) {
            error = "destino de arquivo já existe";
        } else {
            error = system_error_message("não foi possível criar o destino " + exposed.target.string(),
                                         saved_errno);
        }
        return false;
    }

    struct stat target_status{};
    if (::fstat(target_fd, &target_status) != 0) {
        const int saved_errno = errno;
        ::close(source_fd);
        ::close(target_fd);
        ::unlink(exposed.target.c_str());
        error = system_error_message("destino de arquivo criado com tipo inválido", saved_errno);
        return false;
    }
    if (!S_ISREG(target_status.st_mode)) {
        ::close(source_fd);
        ::close(target_fd);
        ::unlink(exposed.target.c_str());
        error = "destino de arquivo criado com tipo inválido";
        return false;
    }

    StagedFile staged{exposed,
                      target_fd,
                      static_cast<std::uint64_t>(target_status.st_dev),
                      static_cast<std::uint64_t>(target_status.st_ino)};
    staged_files_.push_back(std::move(staged));

    std::array<char, 64U * 1024U> buffer{};
    bool success = true;
    while (true) {
        const ssize_t read_count = ::read(source_fd, buffer.data(), buffer.size());
        if (read_count > 0) {
            if (!write_all(target_fd, buffer.data(), static_cast<std::size_t>(read_count), error)) {
                success = false;
                break;
            }
            continue;
        }
        if (read_count == 0) break;
        if (errno == EINTR) continue;
        error = system_error_message("não foi possível ler a origem " + exposed.source.string(), errno);
        success = false;
        break;
    }

    const int source_close = ::close(source_fd);
    if (success && source_close != 0) {
        error = system_error_message("não foi possível fechar arquivo de compatibilidade", errno);
        success = false;
    }
    return success;
}

FileExposure FileExposure::materialize(const std::filesystem::path& prefix_root,
                                       const Profile& profile) {
    FileExposure result;
    try {
        const prefix::EnvironmentPaths paths = prefix::get_environment_paths(prefix_root);
        std::filesystem::file_status drive_status;
        if (!read_status(paths.drive_c, drive_status, result.error_)) {
            return result;
        }
        if (std::filesystem::is_symlink(drive_status) ||
            !std::filesystem::is_directory(drive_status)) {
            result.error_ = "drive_c não é um diretório regular";
            return result;
        }

        std::vector<PendingMapping> pending;
        pending.reserve(profile.files.size());
        std::vector<std::filesystem::path> required_directories;
        const std::filesystem::path canonical_drive =
            std::filesystem::weakly_canonical(paths.drive_c);

        for (const FileMapping& mapping : profile.files) {
            const std::filesystem::path source = paths.compat_files_dir / mapping.source;
            if (!regular_source(source, paths.compat_files_dir, result.error_)) return result;

            std::string target_error;
            const auto target = target_path(prefix_root, mapping.target, target_error);
            if (!target.has_value()) {
                result.error_ = std::move(target_error);
                return result;
            }
            if (!path_is_within(*target, canonical_drive)) {
                result.error_ = "destino de arquivo fora de drive_c";
                return result;
            }
            if (!collect_parent_directories(canonical_drive, *target,
                                            required_directories, result.error_)) {
                return result;
            }
            pending.push_back(PendingMapping{ExposedFile{source, *target}});
        }
        result.files_.reserve(pending.size());
        result.staged_files_.reserve(pending.size());
        result.staged_directories_.reserve(required_directories.size());

        for (const std::filesystem::path& directory : required_directories) {
            if (::mkdir(directory.c_str(), 0755) == 0) {
                struct stat status{};
                if (::lstat(directory.c_str(), &status) != 0 || !S_ISDIR(status.st_mode)) {
                    result.error_ = system_error_message(
                        "diretório-pai criado com tipo inválido", errno);
                    if (!result.cleanup()) result.status_ = FileExposureStatus::RollbackFailed;
                    return result;
                }
                result.staged_directories_.push_back(
                    StagedDirectory{directory, static_cast<std::uint64_t>(status.st_dev),
                                    static_cast<std::uint64_t>(status.st_ino)});
                continue;
            }
            if (errno == EEXIST) {
                std::filesystem::file_status status;
                if (!read_status(directory, status, result.error_) ||
                    std::filesystem::is_symlink(status) ||
                    !std::filesystem::is_directory(status)) {
                    if (result.error_.empty()) {
                        result.error_ = "diretório-pai do destino não é um diretório regular";
                    }
                    if (!result.cleanup()) result.status_ = FileExposureStatus::RollbackFailed;
                    return result;
                }
                continue;
            }
            result.error_ = system_error_message(
                "não foi possível criar diretório-pai " + directory.string(), errno);
            if (!result.cleanup()) result.status_ = FileExposureStatus::RollbackFailed;
            return result;
        }

        for (const PendingMapping& mapping : pending) {
            if (!result.copy_file_exclusive(mapping.exposed, result.error_)) {
                if (!result.cleanup()) {
                    result.status_ = FileExposureStatus::RollbackFailed;
                }
                return result;
            }
            result.files_.push_back(mapping.exposed);
        }

        result.status_ = FileExposureStatus::Applied;
        return result;
    } catch (const std::exception& exception) {
        result.error_ = std::string{"falha interna ao materializar perfil: "} + exception.what();
        if (!result.cleanup()) result.status_ = FileExposureStatus::RollbackFailed;
        return result;
    } catch (...) {
        result.error_ = "falha interna ao materializar perfil";
        if (!result.cleanup()) result.status_ = FileExposureStatus::RollbackFailed;
        return result;
    }
}

FileExposure::FileExposure(FileExposure&& other) noexcept
    : status_(other.status_),
      error_(std::move(other.error_)),
      files_(std::move(other.files_)),
      staged_files_(std::move(other.staged_files_)),
      staged_directories_(std::move(other.staged_directories_)),
      cleanup_summary_(other.cleanup_summary_),
      cleanup_done_(other.cleanup_done_) {
    other.cleanup_done_ = true;
}

FileExposure& FileExposure::operator=(FileExposure&& other) noexcept {
    if (this == &other) return *this;
    (void)cleanup();
    status_ = other.status_;
    error_ = std::move(other.error_);
    files_ = std::move(other.files_);
    staged_files_ = std::move(other.staged_files_);
    staged_directories_ = std::move(other.staged_directories_);
    cleanup_summary_ = other.cleanup_summary_;
    cleanup_done_ = other.cleanup_done_;
    other.cleanup_done_ = true;
    return *this;
}

FileExposure::~FileExposure() {
    (void)cleanup();
}

bool FileExposure::cleanup() noexcept {
    if (cleanup_done_) return !cleanup_summary_.failed;
    cleanup_done_ = true;

    for (auto iterator = staged_files_.rbegin(); iterator != staged_files_.rend(); ++iterator) {
        struct stat status{};
        if (::lstat(iterator->exposed.target.c_str(), &status) == 0) {
            if (!same_identity(status, iterator->device, iterator->inode)) {
                ++cleanup_summary_.paths_retained;
            } else if (::unlink(iterator->exposed.target.c_str()) == 0 || errno == ENOENT) {
                ++cleanup_summary_.files_removed;
            } else {
                cleanup_summary_.failed = true;
            }
        } else if (errno != ENOENT) {
            cleanup_summary_.failed = true;
        }
        if (iterator->descriptor >= 0 && ::close(iterator->descriptor) != 0) {
            cleanup_summary_.failed = true;
        }
        iterator->descriptor = -1;
    }

    for (auto iterator = staged_directories_.rbegin();
         iterator != staged_directories_.rend(); ++iterator) {
        struct stat status{};
        if (::lstat(iterator->path.c_str(), &status) != 0) {
            if (errno == ENOENT) continue;
            cleanup_summary_.failed = true;
            continue;
        }
        if (!same_identity(status, iterator->device, iterator->inode)) {
            ++cleanup_summary_.paths_retained;
            continue;
        }
        if (::rmdir(iterator->path.c_str()) == 0 || errno == ENOENT) {
            ++cleanup_summary_.directories_removed;
        } else if (errno == ENOTEMPTY || errno == EEXIST) {
            ++cleanup_summary_.paths_retained;
        } else {
            cleanup_summary_.failed = true;
        }
    }

    return !cleanup_summary_.failed;
}

}  // namespace tradutorlinux::compat
