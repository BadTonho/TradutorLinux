#include "tradutorlinux/gui/x11.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <thread>

namespace tradutorlinux::gui {
namespace {

constexpr int kDefaultWidth = 480;
constexpr int kDefaultHeight = 180;
constexpr std::size_t kMaxWindows = 16;

struct WindowState {
    bool used{false};
    Window window{0};
    int screen{0};
    int width{kDefaultWidth};
    int height{kDefaultHeight};
    std::string caption;
    std::chrono::steady_clock::time_point created_at{};
    std::deque<WindowEvent> pending;  // fila por janela, demultiplexada do X11
};

std::array<WindowState, kMaxWindows> g_windows{};

// Cores dos stock brushes do Win32 (24 bits), usadas no preenchimento sólido.
constexpr std::array<unsigned long, 6> kBrushRgb = {
    0xFFFFFFU,  // 0 WHITE_BRUSH
    0xC0C0C0U,  // 1 LTGRAY_BRUSH
    0x808080U,  // 2 GRAY_BRUSH
    0x404040U,  // 3 DKGRAY_BRUSH
    0x000000U,  // 4 BLACK_BRUSH
    0x000000U,  // 5 NULL_BRUSH (sem preenchimento)
};

// GC e pixels de preenchimento, criados sob demanda no display compartilhado.
struct FillState {
    GC gc{};
    std::array<unsigned long, 6> pixels{};
    bool ready{false};
};

FillState& fill_state(Display* const dpy, const int screen) {
    static FillState state;
    if (!state.ready) {
        state.gc = XCreateGC(dpy, RootWindow(dpy, screen), 0, nullptr);
        const Colormap colormap = DefaultColormap(dpy, screen);
        for (std::size_t index = 0; index < kBrushRgb.size(); ++index) {
            XColor color{};
            color.flags = DoRed | DoGreen | DoBlue;
            color.red = static_cast<unsigned short>(((kBrushRgb[index] >> 16) & 0xFFU) * 257U);
            color.green = static_cast<unsigned short>(((kBrushRgb[index] >> 8) & 0xFFU) * 257U);
            color.blue = static_cast<unsigned short>((kBrushRgb[index] & 0xFFU) * 257U);
            if (XAllocColor(dpy, colormap, &color)) {
                state.pixels[index] = color.pixel;
            } else {
                state.pixels[index] = BlackPixel(dpy, screen);
            }
        }
        state.ready = true;
    }
    return state;
}

class DisplayCloser {
public:
    explicit DisplayCloser(Display* const dpy) noexcept : dpy_(dpy) {}
    ~DisplayCloser() {
        if (dpy_ != nullptr) {
            XCloseDisplay(dpy_);
        }
    }
    DisplayCloser(const DisplayCloser&) = delete;
    DisplayCloser& operator=(const DisplayCloser&) = delete;

private:
    Display* dpy_;
};

[[nodiscard]] Display* display() noexcept {
    static Display* const instance = XOpenDisplay(nullptr);
    static DisplayCloser closer{instance};
    return instance;
}

[[nodiscard]] std::chrono::milliseconds autoclose_delay() noexcept {
    const char* value = std::getenv("TL_GUI_AUTOCLOSE_MS");
    if (value == nullptr) {
        return std::chrono::milliseconds{0};
    }
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= 0) {
        return std::chrono::milliseconds{0};
    }
    return std::chrono::milliseconds{parsed};
}

WindowState* find_state(const NativeWindow window) noexcept {
    const auto found = std::find_if(g_windows.begin(), g_windows.end(),
                                    [window](const WindowState& state) {
                                        return state.used && window == &state;
                                    });
    if (found != g_windows.end()) {
        return &*found;
    }
    return nullptr;
}

WindowState* free_state() noexcept {
    const auto found = std::find_if(g_windows.begin(), g_windows.end(),
                                    [](const WindowState& state) { return !state.used; });
    if (found != g_windows.end()) {
        return &*found;
    }
    return nullptr;
}

WindowState* find_state_by_xwindow(const Window window) noexcept {
    const auto found = std::find_if(g_windows.begin(), g_windows.end(),
                                    [window](const WindowState& state) {
                                        return state.used && window == state.window;
                                    });
    if (found != g_windows.end()) {
        return &*found;
    }
    return nullptr;
}

void push_event_for(Display* const dpy, WindowState* const state, XEvent& event) noexcept {
    if (event.type == Expose) {
        const char* const caption = state->caption.c_str();
        XDrawString(dpy, state->window, DefaultGC(dpy, state->screen), 24, 40, caption,
                    static_cast<int>(state->caption.size()));
        XFlush(dpy);
        state->pending.push_back({WindowEventType::Redraw, 0, 0});
        return;
    }
    if (event.type == ButtonPress) {
        if (event.xbutton.button == 1) {
            state->pending.push_back({WindowEventType::Press, event.xbutton.x, event.xbutton.y});
        }
        return;
    }
    if (event.type == ButtonRelease) {
        if (event.xbutton.button == 1) {
            state->pending.push_back({WindowEventType::Release, event.xbutton.x, event.xbutton.y});
        }
        return;
    }
    if (event.type == MotionNotify) {
        state->pending.push_back(
            {WindowEventType::MouseMove, event.xmotion.x, event.xmotion.y});
        return;
    }
    if (event.type == KeyPress || event.type == KeyRelease) {
        char buffer[8];
        KeySym keysym = 0;
        const int length =
            XLookupString(&event.xkey, buffer, sizeof(buffer), &keysym, nullptr);
        if (keysym != NoSymbol) {
            const char character = length > 0 ? buffer[0] : '\0';
            const WindowEventType type =
                event.type == KeyPress ? WindowEventType::KeyDown : WindowEventType::KeyUp;
            state->pending.push_back(
                {type, 0, 0, character, static_cast<unsigned long>(keysym)});
        }
        return;
    }
    if (event.type == ClientMessage) {
        const Atom protocols_atom = XInternAtom(dpy, "WM_PROTOCOLS", False);
        const Atom delete_atom = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
        if (event.xclient.message_type == protocols_atom &&
            event.xclient.data.l[0] == static_cast<long>(delete_atom)) {
            state->pending.push_back({WindowEventType::CloseRequested, 0, 0});
        }
        return;
    }
    if (event.type == DestroyNotify) {
        state->pending.push_back({WindowEventType::CloseRequested, 0, 0});
    }
}

// Demultiplexa todos os eventos X11 pendentes para a fila de cada janela.
// Eventos de janelas desconhecidas são descartados; nada é perdido entre
// janelas conhecidas, independentemente da ordem de consulta do pump.
void drain_events(Display* const dpy) noexcept {
    while (XPending(dpy) > 0) {
        XEvent event{};
        XNextEvent(dpy, &event);
        WindowState* const target = find_state_by_xwindow(event.xany.window);
        if (target != nullptr) {
            push_event_for(dpy, target, event);
        }
    }
}

}  // namespace

NativeWindow create_window(const char* const caption, const int width,  // NOLINT(bugprone-easily-swappable-parameters)
                           const int height) noexcept {
    Display* const dpy = display();
    if (dpy == nullptr) {
        return nullptr;
    }
    WindowState* state = free_state();
    if (state == nullptr) {
        return nullptr;
    }
    const int screen = DefaultScreen(dpy);
    const Window root = RootWindow(dpy, screen);
    const int resolved_width = width > 0 ? width : kDefaultWidth;
    const int resolved_height = height > 0 ? height : kDefaultHeight;
    const Window window =
        XCreateSimpleWindow(dpy, root, 0, 0, static_cast<unsigned int>(resolved_width),
                            static_cast<unsigned int>(resolved_height), 1,
                            BlackPixel(dpy, screen), WhitePixel(dpy, screen));
    if (window == 0) {
        return nullptr;
    }
    XStoreName(dpy, window, caption != nullptr ? caption : "TradutorLinux");
    XSelectInput(dpy, window, ExposureMask | ButtonPressMask | ButtonReleaseMask |
                                 PointerMotionMask | KeyPressMask | KeyReleaseMask |
                                 StructureNotifyMask);
    Atom delete_protocol = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, window, &delete_protocol, 1);
    *state = WindowState{
        .used = true,
        .window = window,
        .screen = screen,
        .width = resolved_width,
        .height = resolved_height,
        .caption = caption != nullptr ? caption : "TradutorLinux",
        .created_at = std::chrono::steady_clock::now(),
        .pending = {},
    };
    return state;
}

void destroy_window(const NativeWindow window) noexcept {
    WindowState* state = find_state(window);
    if (state == nullptr) {
        return;
    }
    if (Display* const dpy = display(); dpy != nullptr) {
        XDestroyWindow(dpy, state->window);
        XFlush(dpy);
    }
    *state = {};
}

bool map_window(const NativeWindow window) noexcept {
    Display* const dpy = display();
    WindowState* state = find_state(window);
    if (dpy == nullptr || state == nullptr) {
        return false;
    }
    XMapWindow(dpy, state->window);
    XFlush(dpy);
    return true;
}

void unmap_window(const NativeWindow window) noexcept {
    Display* const dpy = display();
    WindowState* state = find_state(window);
    if (dpy == nullptr || state == nullptr) {
        return;
    }
    XUnmapWindow(dpy, state->window);
    XFlush(dpy);
}

void flush_window(const NativeWindow window) noexcept {
    Display* const dpy = display();
    WindowState* state = find_state(window);
    if (dpy == nullptr || state == nullptr) {
        return;
    }
    XFlush(dpy);
}

void draw_text(const NativeWindow window, const char* const text, const int x, const int y) noexcept {
    if (text == nullptr) {
        return;
    }
    draw_text_len(window, text, static_cast<int>(std::strlen(text)), x, y);
}

void draw_text_len(const NativeWindow window, const char* const text, const int length, const int x,
                   const int y) noexcept {
    Display* const dpy = display();
    WindowState* state = find_state(window);
    if (dpy == nullptr || state == nullptr || text == nullptr || length <= 0) {
        return;
    }
    XDrawString(dpy, state->window, DefaultGC(dpy, state->screen), x, y, text, length);
}

void draw_rectangle(const NativeWindow window, const int x, const int y, const int width,
                    const int height) noexcept {
    Display* const dpy = display();
    WindowState* state = find_state(window);
    if (dpy == nullptr || state == nullptr) {
        return;
    }
    XDrawRectangle(dpy, state->window, DefaultGC(dpy, state->screen), x, y,
                   static_cast<unsigned int>(width), static_cast<unsigned int>(height));
}

void fill_rectangle(const NativeWindow window, const int x, const int y, const int width,
                    const int height, const int brush_index) noexcept {
    if (brush_index == 5) {  // NULL_BRUSH: nenhum preenchimento
        return;
    }
    Display* const dpy = display();
    WindowState* state = find_state(window);
    if (dpy == nullptr || state == nullptr) {
        return;
    }
    FillState& fill = fill_state(dpy, state->screen);
    XSetForeground(dpy, fill.gc, fill.pixels[static_cast<std::size_t>(brush_index)]);
    XFillRectangle(dpy, state->window, fill.gc, x, y,
                   static_cast<unsigned int>(width), static_cast<unsigned int>(height));
}

WindowEvent next_window_event(const NativeWindow window) noexcept {
    Display* const dpy = display();
    WindowState* state = find_state(window);
    if (dpy == nullptr || state == nullptr) {
        return {};
    }
    const std::chrono::milliseconds delay = autoclose_delay();
    if (delay.count() > 0 &&
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - state->created_at) >= delay) {
        return {WindowEventType::CloseRequested, 0, 0};
    }
    drain_events(dpy);
    if (state->pending.empty()) {
        return {};
    }
    WindowEvent event = state->pending.front();
    state->pending.pop_front();
    return event;
}

std::uint32_t message_box(const char* const text,  // NOLINT(bugprone-easily-swappable-parameters)
                          const char* const caption) noexcept {
    NativeWindow window = create_window(caption, kDefaultWidth, kDefaultHeight);
    if (window == nullptr) {
        return 0;
    }
    map_window(window);
    bool done = false;
    std::uint32_t result = 0;
    while (!done) {
        const WindowEvent event = next_window_event(window);
        switch (event.type) {
            case WindowEventType::Redraw:
                draw_text(window, text != nullptr ? text : "", 24, 54);
                draw_rectangle(window, 195, 110, 90, 32);
                draw_text(window, "OK", 225, 131);
                flush_window(window);
                break;
            case WindowEventType::Press:
                if (event.x >= 195 && event.x <= 285 && event.y >= 110 && event.y <= 142) {
                    result = 1;
                    done = true;
                }
                break;
            case WindowEventType::CloseRequested:
                done = true;
                break;
            case WindowEventType::Idle:
            default:
                break;
        }
        if (!done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    destroy_window(window);
    return result;
}

}  // namespace tradutorlinux::gui