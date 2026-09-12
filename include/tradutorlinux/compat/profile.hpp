#pragma once

#include "tradutorlinux/compat/path_validation.hpp"

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

enum class BackendKind {
    Native,
    Proton,
};

struct BackendSelection {
    BackendKind kind{BackendKind::Native};
    std::string min_version;
};

struct Profile {
    std::uint32_t schema{0};
    std::string app_id;
    std::string app_sha256;
    std::string app_version;
    std::string extension;
    std::vector<FileMapping> files;
    std::vector<DllMapping> dlls;
    BackendSelection backend;
    bool backend_declared{false};
    bool extension_declared{false};
};

enum class ProfileStatus {
    Missing,
    Loaded,
    Invalid,
    InternalError,
};

enum class ProfileParserBackend {
    NotUsed,
    Rust,
};

enum class ProfileParserStatus {
    NotAttempted,
    Success,
    Malformed,
    UnsupportedFormat,
    InvalidArgument,
    BufferTooSmall,
    InputTooLarge,
    OutputTooLarge,
    Internal,
};

struct ProfileParserDiagnostics {
    bool attempted{false};
    ProfileParserBackend backend{ProfileParserBackend::NotUsed};
    ProfileParserStatus status{ProfileParserStatus::NotAttempted};
    std::uint32_t code{0};
    std::uint32_t phase{0};
    std::uint64_t input_offset{0};
    std::uint64_t detail_value{0};
};

struct ProfileLoadResult {
    ProfileStatus status{ProfileStatus::Missing};
    Profile profile;
    std::string error;
    PathValidationMetrics path_validation;
    ProfileParserDiagnostics parser;
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
