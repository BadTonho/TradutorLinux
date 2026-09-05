#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace tradutorlinux::backend {

struct ProtonConfig {
    std::filesystem::path root;
    std::string sha256;
};

enum class ConfigStatus {
    Missing,
    Loaded,
    Invalid,
};

struct ProtonConfigResult {
    ConfigStatus status{ConfigStatus::Missing};
    std::optional<ProtonConfig> config;
    std::string error;
};

struct ProtonValidationResult {
    bool valid{false};
    std::string version;
    std::string fingerprint;
    std::string error;
};

[[nodiscard]] std::filesystem::path default_config_path();

[[nodiscard]] ProtonConfigResult load_config_file(
    const std::filesystem::path& path);

[[nodiscard]] ProtonConfigResult load_config();

[[nodiscard]] ProtonValidationResult validate_proton(
    const ProtonConfig& config,
    std::string_view minimum_version = {});

}  // namespace tradutorlinux::backend
