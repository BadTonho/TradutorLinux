#include "rust_path_validator.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace tradutorlinux::compat::detail {
namespace {

constexpr std::uint64_t kMaxPathInputBytes = 1024ULL * 1024U;
constexpr std::uint64_t kDiagnosticCapacity = 256U;

[[nodiscard]] std::string status_name(const tl_rust_status_t status) {
    switch (status) {
        case TL_RUST_STATUS_INVALID_ARGUMENT: return "argumento inválido";
        case TL_RUST_STATUS_BUFFER_TOO_SMALL: return "buffer de diagnóstico insuficiente";
        case TL_RUST_STATUS_INPUT_TOO_LARGE: return "entrada excede o limite";
        case TL_RUST_STATUS_INTERNAL: return "erro interno";
        case TL_RUST_STATUS_INVALID_PATH: return "caminho inválido";
        default: return "status inesperado";
    }
}

}  // namespace

RustPathValidationSession::RustPathValidationSession() {
    metrics_.backend = PathValidationBackend::Rust;
    create_status_ = tl_rust_validator_create(kMaxPathInputBytes, &handle_);
    if (create_status_ == TL_RUST_STATUS_OK && handle_ != nullptr) {
        metrics_.handle_count = 1U;
    } else {
        metrics_.infrastructure_error = true;
    }
}

RustPathValidationSession::RustPathValidationSession(
    RustPathValidationSession&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)),
      create_status_(other.create_status_),
      metrics_(other.metrics_) {
    other.create_status_ = TL_RUST_STATUS_INTERNAL;
    other.metrics_ = PathValidationMetrics{};
}

RustPathValidationSession& RustPathValidationSession::operator=(
    RustPathValidationSession&& other) noexcept {
    if (this == &other) return *this;
    tl_rust_validator_destroy(handle_);
    handle_ = std::exchange(other.handle_, nullptr);
    create_status_ = other.create_status_;
    metrics_ = other.metrics_;
    other.create_status_ = TL_RUST_STATUS_INTERNAL;
    other.metrics_ = PathValidationMetrics{};
    return *this;
}

RustPathValidationSession::~RustPathValidationSession() {
    tl_rust_validator_destroy(handle_);
}

bool RustPathValidationSession::available() const noexcept {
    return handle_ != nullptr && create_status_ == TL_RUST_STATUS_OK;
}

RustPathValidationResult RustPathValidationSession::validate(
    const std::string_view path,
    tl_rust_status_t (*function)(const tl_rust_validator_t*, const std::uint8_t*, std::uint64_t,
                                 char*, std::uint64_t, std::uint64_t*),
    const std::string_view role, std::string& error) {
    ++metrics_.checks;
    const auto started = std::chrono::steady_clock::now();
    const auto finish = [this, started]() noexcept {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started);
        metrics_.duration_us += static_cast<std::uint64_t>(elapsed.count());
    };

    if (!available()) {
        metrics_.infrastructure_error = true;
        error = std::string{role} + " não pôde ser validado pelo Rust: " +
                status_name(create_status_);
        finish();
        return RustPathValidationResult::InternalError;
    }
    if (path.size() > std::numeric_limits<std::uint64_t>::max()) {
        ++metrics_.rejected;
        error = std::string{role} + " excede o tamanho representável";
        finish();
        return RustPathValidationResult::InvalidInput;
    }

    std::array<char, kDiagnosticCapacity> diagnostic{};
    std::uint64_t required = 0;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(path.data());
    const tl_rust_status_t status = function(
        handle_, bytes, static_cast<std::uint64_t>(path.size()), diagnostic.data(),
        static_cast<std::uint64_t>(diagnostic.size()), &required);
    if (status == TL_RUST_STATUS_OK) {
        finish();
        return RustPathValidationResult::Accepted;
    }

    std::string detail = status_name(status);
    if (required > 0 && required <= diagnostic.size() && diagnostic[required - 1U] == '\0') {
        detail.assign(diagnostic.data(), static_cast<std::size_t>(required - 1U));
    }
    error = std::string{role} + " rejeitado pelo validador Rust: " + detail;
    if (status == TL_RUST_STATUS_INVALID_PATH || status == TL_RUST_STATUS_INPUT_TOO_LARGE) {
        ++metrics_.rejected;
        finish();
        return RustPathValidationResult::InvalidInput;
    }

    metrics_.infrastructure_error = true;
    finish();
    return RustPathValidationResult::InternalError;
}

RustPathValidationResult RustPathValidationSession::validate_relative_path(
    const std::string_view path, std::string& error) {
    return validate(path, tl_rust_validator_validate_relative_path, "origem relativa", error);
}

RustPathValidationResult RustPathValidationSession::validate_c_drive_path(
    const std::string_view path, std::string& error) {
    return validate(path, tl_rust_validator_validate_c_drive_path, "destino C", error);
}

}  // namespace tradutorlinux::compat::detail
