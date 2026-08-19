#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tradutorlinux::gui {

// Evento de janela já traduzido do X11 para o subconjunto Win32 suportado.
enum class WindowEventType {
    Idle,           // nenhum evento pendente para a janela
    Redraw,         // redesenho disponível (equivale a WM_PAINT)
    Press,          // botão primário pressionado (equivale a WM_LBUTTONDOWN)
    Release,        // botão primário liberado (equivale a WM_LBUTTONUP)
    RightPress,     // botão secundário pressionado (bandeja emulada)
    MouseMove,      // ponteiro movido (equivale a WM_MOUSEMOVE)
    KeyDown,        // tecla pressionada (equivale a WM_KEYDOWN)
    KeyUp,          // tecla liberada (equivale a WM_KEYUP)
    CloseRequested, // WM_DELETE_WINDOW do gerenciador de janelas (equivale a WM_CLOSE)
};

struct WindowEvent {
    WindowEventType type{WindowEventType::Idle};
    int x{};
    int y{};
    char character{};        // primeiro caractere traduzido da tecla (pode ser '\0')
    unsigned long keysym{0}; // keysym X11 da tecla (0 quando ausente)
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

// Desenha texto, um retângulo de contorno e um preenchimento sólido no client
// area da janela. `brush_index` segue os stock brushes do Win32: 0 WHITE,
// 1 LTGRAY, 2 GRAY, 3 DKGRAY, 4 BLACK, 5 NULL (sem preenchimento).
void draw_text(NativeWindow window, const char* text, int x, int y) noexcept;
void draw_text_len(NativeWindow window, const char* text, int length, int x, int y) noexcept;
void draw_text_color(NativeWindow window, const char* text, int x, int y, std::uint32_t rgb,
                     bool bold = false) noexcept;
void draw_text_len_color(NativeWindow window, const char* text, int length, int x, int y,
                         std::uint32_t rgb, bool bold = false) noexcept;
void draw_rectangle(NativeWindow window, int x, int y, int width, int height) noexcept;
void draw_rectangle_color(NativeWindow window, int x, int y, int width, int height,
                          std::uint32_t rgb) noexcept;
void fill_rectangle(NativeWindow window, int x, int y, int width, int height,
                    int brush_index) noexcept;
void fill_rectangle_color(NativeWindow window, int x, int y, int width, int height,
                          std::uint32_t rgb) noexcept;

// Retorna o próximo evento pendente da janela, drenando um evento X por chamada.
// Eventos não relevantes para a janela informada são descartados.
[[nodiscard]] WindowEvent next_window_event(NativeWindow window) noexcept;

// Exibe uma caixa modal mínima no X11. Retorna 1 quando o botão OK foi
// acionado, 0 quando a GUI não pôde ser criada ou a janela foi fechada.
[[nodiscard]] std::uint32_t message_box(const char* text, const char* caption) noexcept;

struct PopupMenuItem {
    std::uint32_t command{};
    std::string text;
    bool separator{false};
};

[[nodiscard]] std::uint32_t track_popup_menu(const std::vector<PopupMenuItem>& items, int x,
                                             int y) noexcept;

}  // namespace tradutorlinux::gui
