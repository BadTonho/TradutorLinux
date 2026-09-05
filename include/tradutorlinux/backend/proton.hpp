#pragma once

#include "tradutorlinux/compat/profile.hpp"
#include "tradutorlinux/process/isolate.hpp"

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

enum class ProtonRunStatus {
    Completed,
    Unsupported,
    InternalError,
};

struct ProtonRunRequest {
    std::filesystem::path prefix_root;
    std::filesystem::path executable;
    std::filesystem::path working_directory;
    std::string app_id;
    std::vector<std::string> guest_arguments;
    process::ResourceLimits resource_limits;
    std::uint64_t timeout_ms{0};
    bool trace_enabled{false};
};

struct ProtonRunResult {
    ProtonRunStatus status{ProtonRunStatus::InternalError};
    process::GuestOutcome outcome{};
    std::string version;
    std::string error;
    std::filesystem::path proton_data_root;
    std::filesystem::path staged_executable;
};

// Valida o backend solicitado, prepara um prefixo Proton persistente e executa
// o launcher oficial fora do GuestContext do runtime nativo. O processo filho
// recebe stdout inalterado e stderr prefixado pelo adaptador de processo.
[[nodiscard]] ProtonRunResult run_proton_application(
    const ProtonConfig& config,
    const compat::Profile& profile,
    const ProtonRunRequest& request,
    std::ostream& diagnostic_stream);

}  // namespace tradutorlinux::backend
