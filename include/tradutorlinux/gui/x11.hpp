#pragma once

#include <cstdint>

namespace tradutorlinux::gui {

// Evento de janela já traduzido do X11 para o subconjunto Win32 suportado.
enum class WindowEventType {
    Idle,           // nenhum evento pendente para a janela
    Redraw,         // redesenho disponível (equivale a WM_PAINT)
    Press,          // clique primário (equivale a WM_LBUTTONDOWN)
    CloseRequested, // WM_DELETE_WINDOW do gerenciador de janelas (equivale a WM_CLOSE)
};

struct WindowEvent {
    WindowEventType type{WindowEventType::Idle};
    int x{};
    int y{};
};

// Handle opaco de janela persistente. Válido somente para as funções abaixo.
using NativeWindow = void*;

// Cria uma janela persistente no X11. Retorna um token opaco ou nullptr quando
// o display não está disponível ou não há slot livre.
[[nodiscard]] NativeWindow create_window(const char* caption, int width, int height) noexcept;
void destroy_window(NativeWindow window) noexcept;
bool map_window(NativeWindow window) noexcept;
void unmap_window(NativeWindow window) noexcept;
void flush_window(NativeWindow window) noexcept;

// Desenha texto e um retângulo de contorno no client area da janela.
void draw_text(NativeWindow window, const char* text, int x, int y) noexcept;
void draw_rectangle(NativeWindow window, int x, int y, int width, int height) noexcept;

// Retorna o próximo evento pendente da janela, drenando um evento X por chamada.
// Eventos não relevantes para a janela informada são descartados.
[[nodiscard]] WindowEvent next_window_event(NativeWindow window) noexcept;

// Exibe uma caixa modal mínima no X11. Retorna 1 quando o botão OK foi
// acionado, 0 quando a GUI não pôde ser criada ou a janela foi fechada.
[[nodiscard]] std::uint32_t message_box(const char* text, const char* caption) noexcept;

}  // namespace tradutorlinux::gui