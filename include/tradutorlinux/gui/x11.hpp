#pragma once

#include <cstdint>

namespace tradutorlinux::gui {

// Exibe uma caixa modal mínima no X11. Retorna 1 quando o botão OK foi
// acionado, 0 quando a GUI não pôde ser criada ou a janela foi fechada.
[[nodiscard]] std::uint32_t message_box(const char* text, const char* caption) noexcept;

}  // namespace tradutorlinux::gui
