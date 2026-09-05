#pragma once

#include "tradutorlinux/compat/profile.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tradutorlinux::compat {

enum class FileExposureStatus {
    Rejected,
    Applied,
    RollbackFailed,
};

struct ExposedFile {
    std::filesystem::path source;
    std::filesystem::path target;
};

struct FileCleanupSummary {
    std::size_t files_removed{0};
    std::size_t directories_removed{0};
    std::size_t paths_retained{0};
    bool failed{false};
};

class FileExposure final {
public:
    [[nodiscard]] static FileExposure materialize(
        const std::filesystem::path& prefix_root,
        const Profile& profile);

    [[nodiscard]] static FileExposure materialize_into(
        const std::filesystem::path& source_prefix_root,
        const std::filesystem::path& target_prefix_root,
        const Profile& profile);

    FileExposure() = default;
    FileExposure(const FileExposure&) = delete;
    FileExposure& operator=(const FileExposure&) = delete;
    FileExposure(FileExposure&& other) noexcept;
    FileExposure& operator=(FileExposure&& other) noexcept;
    ~FileExposure();

    [[nodiscard]] FileExposureStatus status() const noexcept { return status_; }
    [[nodiscard]] bool applied() const noexcept {
        return status_ == FileExposureStatus::Applied;
    }
    [[nodiscard]] bool rollback_failed() const noexcept {
        return status_ == FileExposureStatus::RollbackFailed;
    }
    [[nodiscard]] std::string_view error() const noexcept { return error_; }
    [[nodiscard]] const std::vector<ExposedFile>& files() const noexcept { return files_; }
    [[nodiscard]] const FileCleanupSummary& cleanup_summary() const noexcept {
        return cleanup_summary_;
    }

    // Idempotente. Retorna false somente quando uma remoção falha por erro de
    // sistema; arquivos substituídos e diretórios que ficaram não vazios são
    // preservados e contabilizados como retidos.
    [[nodiscard]] bool cleanup() noexcept;

private:
    struct StagedFile {
        ExposedFile exposed;
        int descriptor{-1};
        std::uint64_t device{0};
        std::uint64_t inode{0};
    };

    struct StagedDirectory {
        std::filesystem::path path;
        std::uint64_t device{0};
        std::uint64_t inode{0};
    };

    [[nodiscard]] bool copy_file_exclusive(const ExposedFile& exposed,
                                           std::string& error);

    FileExposureStatus status_{FileExposureStatus::Rejected};
    std::string error_;
    std::vector<ExposedFile> files_;
    std::vector<StagedFile> staged_files_;
    std::vector<StagedDirectory> staged_directories_;
    FileCleanupSummary cleanup_summary_{};
    bool cleanup_done_{false};
};

}  // namespace tradutorlinux::compat
