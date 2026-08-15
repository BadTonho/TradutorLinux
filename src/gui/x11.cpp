#include "tradutorlinux/gui/x11.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace tradutorlinux::gui {
namespace {

constexpr int kDefaultWidth = 480;
constexpr int kDefaultHeight = 180;
constexpr std::size_t kMaxWindows = 16;
constexpr long kAutocloseDelayMs = 100;

struct WindowState {
    bool used{false};
    Window window{0};
    int screen{0};
    int width{kDefaultWidth};
    int height{kDefaultHeight};
    std::string caption;
    std::chrono::steady_clock::time_point created_at{};
};

std::array<WindowState, kMaxWindows> g_windows{};

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

[[nodiscard]] bool autoclose_enabled() noexcept {
    const char* value = std::getenv("TL_GUI_AUTOCLOSE_MS");
    return value != nullptr && std::strcmp(value, "0") != 0;
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
    XSelectInput(dpy, window, ExposureMask | ButtonPressMask | StructureNotifyMask);
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
    Display* const dpy = display();
    WindowState* state = find_state(window);
    if (dpy == nullptr || state == nullptr || text == nullptr) {
        return;
    }
    XDrawString(dpy, state->window, DefaultGC(dpy, state->screen), x, y, text,
                static_cast<int>(std::strlen(text)));
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

WindowEvent next_window_event(const NativeWindow window) noexcept {
    Display* const dpy = display();
    WindowState* state = find_state(window);
    if (dpy == nullptr || state == nullptr) {
        return {};
    }
    if (autoclose_enabled() &&
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - state->created_at)
                .count() >= kAutocloseDelayMs) {
        return {WindowEventType::CloseRequested, 0, 0};
    }
    if (XPending(dpy) == 0) {
        return {};
    }
    XEvent event{};
    XNextEvent(dpy, &event);
    if (event.xany.window != state->window) {
        return {};
    }
    if (event.type == Expose) {
        const char* const caption = state->caption.c_str();
        XDrawString(dpy, state->window, DefaultGC(dpy, state->screen), 24, 40, caption,
                    static_cast<int>(state->caption.size()));
        XFlush(dpy);
        return {WindowEventType::Redraw, 0, 0};
    }
    if (event.type == ButtonPress) {
        return {WindowEventType::Press, event.xbutton.x, event.xbutton.y};
    }
    if (event.type == ClientMessage) {
        const Atom protocols_atom = XInternAtom(dpy, "WM_PROTOCOLS", False);
        const Atom delete_atom = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
        if (event.xclient.message_type == protocols_atom &&
            event.xclient.data.l[0] == static_cast<long>(delete_atom)) {
            return {WindowEventType::CloseRequested, 0, 0};
        }
    }
    if (event.type == DestroyNotify) {
        return {WindowEventType::CloseRequested, 0, 0};
    }
    return {};
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