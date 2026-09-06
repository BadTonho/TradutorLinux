#pragma once

#include "tradutorlinux/compat/path_validation.hpp"
#include "tradutorlinux/ffi/rust_validator.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace tradutorlinux::compat::detail {

enum class RustPathValidationResult {
    Accepted,
    InvalidInput,
    InternalError,
};

class RustPathValidationSession final {
public:
    RustPathValidationSession();
    RustPathValidationSession(const RustPathValidationSession&) = delete;
    RustPathValidationSession& operator=(const RustPathValidationSession&) = delete;
    RustPathValidationSession(RustPathValidationSession&& other) noexcept;
    RustPathValidationSession& operator=(RustPathValidationSession&& other) noexcept;
    ~RustPathValidationSession();

    [[nodiscard]] RustPathValidationResult validate_relative_path(
        std::string_view path, std::string& error);
    [[nodiscard]] RustPathValidationResult validate_c_drive_path(
        std::string_view path, std::string& error);

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] const PathValidationMetrics& metrics() const noexcept { return metrics_; }

private:
    [[nodiscard]] RustPathValidationResult validate(
        std::string_view path,
        tl_rust_status_t (*function)(const tl_rust_validator_t*, const std::uint8_t*,
                                     std::uint64_t, char*, std::uint64_t, std::uint64_t*),
        std::string_view role, std::string& error);

    tl_rust_validator_t* handle_{nullptr};
    tl_rust_status_t create_status_{TL_RUST_STATUS_OK};
    PathValidationMetrics metrics_{};
};

}  // namespace tradutorlinux::compat::detail
