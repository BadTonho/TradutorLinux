#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tradutorlinux::compat {

struct FileMapping {
    std::filesystem::path source;
    std::string target;
};

struct DllMapping {
    std::string module;
    std::filesystem::path source;
};

struct Profile {
    std::uint32_t schema{0};
    std::string app_id;
    std::string app_sha256;
    std::string app_version;
    std::vector<FileMapping> files;
    std::vector<DllMapping> dlls;
};

enum class ProfileStatus {
    Missing,
    Loaded,
    Invalid,
};

struct ProfileLoadResult {
    ProfileStatus status{ProfileStatus::Missing};
    Profile profile;
    std::string error;
};

[[nodiscard]] std::filesystem::path profile_path(
    const std::filesystem::path& prefix_root);
[[nodiscard]] std::filesystem::path files_directory(
    const std::filesystem::path& prefix_root);
[[nodiscard]] std::filesystem::path dlls_directory(
    const std::filesystem::path& prefix_root);

[[nodiscard]] ProfileLoadResult load_profile(
    const std::filesystem::path& prefix_root,
    std::string_view expected_app_id,
    std::string_view expected_app_sha256 = {},
    std::string_view expected_app_version = {});

}  // namespace tradutorlinux::compat
