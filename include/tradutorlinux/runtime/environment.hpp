#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tradutorlinux::runtime {

// Ambiente Win32 pertencente ao processo convidado.  Ele começa como uma
// cópia do ambiente do hospedeiro, mas nunca usa putenv/setenv: mudanças do
// convidado ficam inteiramente no runtime.
void initialize_guest_environment(const std::filesystem::path& prefix_root);
void clear_guest_environment() noexcept;
[[nodiscard]] bool guest_environment_is_initialized() noexcept;

[[nodiscard]] std::optional<std::string> guest_environment_value(std::string_view name);
[[nodiscard]] const char* guest_environment_cstring(std::string_view name) noexcept;
[[nodiscard]] bool set_guest_environment_value(std::string_view name,
                                                const std::optional<std::string>& value) noexcept;
[[nodiscard]] std::vector<std::u16string> guest_environment_entries_w();
[[nodiscard]] char** guest_environment_block_a() noexcept;

// Blocos retornados por GetEnvironmentStringsW pertencem ao runtime e só
// podem ser liberados por FreeEnvironmentStringsW.
[[nodiscard]] std::uint16_t* allocate_environment_block_w() noexcept;
[[nodiscard]] bool free_environment_block_w(std::uint16_t* block) noexcept;

}  // namespace tradutorlinux::runtime
