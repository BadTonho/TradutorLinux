#include "path_rules.hpp"

namespace tradutorlinux::compat::path_rules {

bool is_relative_source(const std::filesystem::path& source) noexcept {
    if (source.empty() || source.is_absolute() || source.has_root_name() ||
        source.has_root_directory()) {
        return false;
    }
    for (const auto& component : source) {
        if (component == "." || component == "..") return false;
    }
    return source.native().find('\\') == std::string::npos;
}

bool is_c_drive_target(const std::string_view target) noexcept {
    return target.size() >= 3U &&
           (target[0] == 'C' || target[0] == 'c') && target[1] == ':' &&
           (target[2] == '\\' || target[2] == '/');
}

bool has_target_filename(const std::string_view target) noexcept {
    return !target.empty() && target.back() != '\\' && target.back() != '/';
}

bool is_c_drive_path_lexically_confined(const std::string_view target) noexcept {
    if (!is_c_drive_target(target) || !has_target_filename(target)) return false;

    std::size_t depth = 0;
    std::size_t component_start = 3U;
    while (component_start <= target.size()) {
        const std::size_t separator = target.find_first_of("/\\", component_start);
        const std::size_t component_end =
            separator == std::string_view::npos ? target.size() : separator;
        const std::string_view component =
            target.substr(component_start, component_end - component_start);
        if (!component.empty() && component != ".") {
            if (component == "..") {
                if (depth == 0U) return false;
                --depth;
            } else {
                ++depth;
            }
        }
        if (separator == std::string_view::npos) break;
        component_start = separator + 1U;
    }
    return true;
}

}  // namespace tradutorlinux::compat::path_rules
