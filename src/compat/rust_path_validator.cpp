#include "rust_path_validator.hpp"

#include "tradutorlinux/ffi/rust_validator.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace tradutorlinux::compat::detail {
namespace {

constexpr std::uint64_t kMaxPathInputBytes = 1024ULL * 1024ULL;

using ValidateFunction = tl_rust_status_t (*)(
    const tl_rust_validator_t*, const std::uint8_t*, std::uint64_t, char*, std::uint64_t,
    std::uint64_t*);

class ValidatorHandle final {
public:
    ValidatorHandle() {
        const tl_rust_status_t status =
            tl_rust_validator_create(kMaxPathInputBytes, &handle_);
        if (status != TL_RUST_STATUS_OK) status_ = status;
    }

    ValidatorHandle(const ValidatorHandle&) = delete;
    ValidatorHandle& operator=(const ValidatorHandle&) = delete;

    ~ValidatorHandle() {
        tl_rust_validator_destroy(handle_);
    }

    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && status_ == TL_RUST_STATUS_OK;
    }

    [[nodiscard]] tl_rust_status_t status() const noexcept { return status_; }
    [[nodiscard]] const tl_rust_validator_t* get() const noexcept { return handle_; }

private:
    tl_rust_validator_t* handle_{nullptr};
    tl_rust_status_t status_{TL_RUST_STATUS_OK};
};

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

[[nodiscard]] bool validate_with_rust(const std::string_view path,
                                      const ValidateFunction function,
                                      const std::string_view role,
                                      std::string& error) {
    ValidatorHandle validator;
    if (!validator.valid()) {
        error = std::string{role} + " não pôde ser validado pelo Rust: " +
                status_name(validator.status());
        return false;
    }
    if (path.size() > std::numeric_limits<std::uint64_t>::max()) {
        error = std::string{role} + " excede o tamanho representável";
        return false;
    }

    std::array<char, 256> diagnostic{};
    std::uint64_t required = 0;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(path.data());
    const tl_rust_status_t status = function(
        validator.get(), bytes, static_cast<std::uint64_t>(path.size()), diagnostic.data(),
        static_cast<std::uint64_t>(diagnostic.size()), &required);
    if (status == TL_RUST_STATUS_OK) return true;

    std::string detail = status_name(status);
    if (required > 0 && required <= diagnostic.size() && diagnostic[required - 1] == '\0') {
        detail.assign(diagnostic.data(), static_cast<std::size_t>(required - 1));
    }
    error = std::string{role} + " rejeitado pelo validador Rust: " + detail;
    return false;
}

}  // namespace

bool validate_relative_path_with_rust(const std::string_view path, std::string& error) {
    return validate_with_rust(path, tl_rust_validator_validate_relative_path,
                              "origem relativa", error);
}

bool validate_c_drive_path_with_rust(const std::string_view path, std::string& error) {
    return validate_with_rust(path, tl_rust_validator_validate_c_drive_path,
                              "destino C", error);
}

}  // namespace tradutorlinux::compat::detail
