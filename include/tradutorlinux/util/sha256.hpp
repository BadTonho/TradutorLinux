#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace tradutorlinux::util {

// Retorna o SHA-256 em hexadecimal minúsculo do arquivo, ou nullopt quando
// o arquivo não puder ser lido.
[[nodiscard]] std::optional<std::string> sha256_file(
    const std::filesystem::path& path);

}  // namespace tradutorlinux::util
