#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace tradutorlinux::gui {

// Mapeia fontes comuns do Windows para fontes instaladas por padrão em distribuições Linux (X11 / Fontconfig / FreeType).
[[nodiscard]] inline std::string map_windows_font_to_linux(std::string_view win_font_name) noexcept {
    if (win_font_name.empty()) {
        return "sans-serif";
    }

    static const std::unordered_map<std::string_view, std::string_view> kFontAliases = {
        // Fontes Sans-Serif
        {"Segoe UI", "Noto Sans, Liberation Sans, Ubuntu, DejaVu Sans, sans-serif"},
        {"Tahoma", "DejaVu Sans, Liberation Sans, FreeSans, sans-serif"},
        {"MS Sans Serif", "DejaVu Sans, Liberation Sans, sans-serif"},
        {"Microsoft Sans Serif", "DejaVu Sans, Liberation Sans, sans-serif"},
        {"Arial", "Liberation Sans, DejaVu Sans, Arial, sans-serif"},
        {"Calibri", "Carlito, Liberation Sans, sans-serif"},
        {"Verdana", "DejaVu Sans, Liberation Sans, sans-serif"},
        {"Trebuchet MS", "Ubuntu, Liberation Sans, sans-serif"},

        // Fontes Serif
        {"Times New Roman", "Liberation Serif, DejaVu Serif, FreeSerif, serif"},
        {"MS Serif", "DejaVu Serif, Liberation Serif, serif"},
        {"Georgia", "Gelasio, DejaVu Serif, serif"},
        {"Palatino Linotype", "URW Palladio L, DejaVu Serif, serif"},

        // Fontes Monospace / Terminal
        {"Consolas", "Cousine, Liberation Mono, DejaVu Sans Mono, monospace"},
        {"Courier New", "Liberation Mono, Courier, DejaVu Sans Mono, monospace"},
        {"Lucida Console", "DejaVu Sans Mono, Liberation Mono, monospace"},
        {"Fixedsys", "monospace"}
    };

    const auto it = kFontAliases.find(win_font_name);
    if (it != kFontAliases.end()) {
        return std::string(it->second);
    }

    return std::string(win_font_name) + ", sans-serif";
}

}  // namespace tradutorlinux::gui
