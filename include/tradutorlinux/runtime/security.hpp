#pragma once

#include <filesystem>

namespace tradutorlinux::runtime::security {

// Sincroniza o cache do estado virtual quando o prefixo ativo muda.
void reset_prefix_cache() noexcept;

// Hooks do ciclo de vida de arquivos. Eles nunca alteram permissões do host.
void remove_path(const std::filesystem::path& path) noexcept;
void rename_path(const std::filesystem::path& from, const std::filesystem::path& to) noexcept;

// Handles de token são opacos e fechados pelo CloseHandle de KERNEL32.
[[nodiscard]] bool close_token_handle(const void* handle) noexcept;

}  // namespace tradutorlinux::runtime::security
