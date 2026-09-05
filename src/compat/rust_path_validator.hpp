#pragma once

#include <string>
#include <string_view>

namespace tradutorlinux::compat::detail {

[[nodiscard]] bool validate_relative_path_with_rust(std::string_view path,
                                                    std::string& error);
[[nodiscard]] bool validate_c_drive_path_with_rust(std::string_view path,
                                                  std::string& error);

}  // namespace tradutorlinux::compat::detail
