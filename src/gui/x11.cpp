#include "tradutorlinux/gui/x11.hpp"

#include <X11/Xlib.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace tradutorlinux::gui {
namespace {

constexpr int kWidth = 480;
constexpr int kHeight = 180;

[[nodiscard]] bool should_autoclose() noexcept {
    const char* value = std::getenv("TL_GUI_AUTOCLOSE_MS");
    return value != nullptr && std::strcmp(value, "0") != 0;
}

}  // namespace

std::uint32_t message_box(const char* const text,  // NOLINT(bugprone-easily-swappable-parameters)
                          const char* const caption) noexcept {
    Display* const display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return 0;
    }
    const int screen = DefaultScreen(display);
    const Window root = RootWindow(display, screen);
    const Window window = XCreateSimpleWindow(display, root, 0, 0, kWidth, kHeight, 1,
                                              BlackPixel(display, screen),
                                              WhitePixel(display, screen));
    if (window == 0) {
        XCloseDisplay(display);
        return 0;
    }
    XStoreName(display, window, caption != nullptr ? caption : "TradutorLinux");
    XSelectInput(display, window, ExposureMask | ButtonPressMask | StructureNotifyMask);
    XMapWindow(display, window);
    XFlush(display);

    const auto start = std::chrono::steady_clock::now();
    bool done = false;
    std::uint32_t result = 0;
    while (!done) {
        while (XPending(display) != 0) {
            XEvent event{};
            XNextEvent(display, &event);
            if (event.type == Expose) {
                const char* const message = text != nullptr ? text : "";
                XDrawString(display, window, DefaultGC(display, screen), 24, 54, message,
                            static_cast<int>(std::strlen(message)));
                XDrawRectangle(display, window, DefaultGC(display, screen), 195, 110, 90, 32);
                XDrawString(display, window, DefaultGC(display, screen), 225, 131, "OK", 2);
                XFlush(display);
            } else if (event.type == ButtonPress) {
                if (event.xbutton.x >= 195 && event.xbutton.x <= 285 &&
                    event.xbutton.y >= 110 && event.xbutton.y <= 142) {
                    result = 1;
                    done = true;
                }
            } else if (event.type == DestroyNotify) {
                done = true;
            }
        }
        if (should_autoclose() &&
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start)
                    .count() >= 100) {
            done = true;
        }
        if (!done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return result;
}

}  // namespace tradutorlinux::gui
