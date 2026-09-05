#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace tradutorlinux::util {

// Retorna o SHA-256 em hexadecimal minúsculo do arquivo, ou nullopt quando
// o arquivo não puder ser lido.
[[nodiscard]] std::optional<std::string> sha256_file(
    const std::filesystem::path& path);

[[nodiscard]] std::string sha256_bytes(std::span<const std::byte> bytes);

[[nodiscard]] inline std::string sha256_string(const std::string_view value) {
    const auto* const data = reinterpret_cast<const std::byte*>(value.data());
    return sha256_bytes(std::span<const std::byte>{data, value.size()});
}

}  // namespace tradutorlinux::util
