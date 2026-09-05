#pragma once

#include <filesystem>
#include <string_view>

namespace tradutorlinux::compat::path_rules {

[[nodiscard]] bool is_relative_source(const std::filesystem::path& source) noexcept;
[[nodiscard]] bool is_c_drive_target(std::string_view target) noexcept;
[[nodiscard]] bool has_target_filename(std::string_view target) noexcept;
[[nodiscard]] bool is_c_drive_path_lexically_confined(std::string_view target) noexcept;

}  // namespace tradutorlinux::compat::path_rules
