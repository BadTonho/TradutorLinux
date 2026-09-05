#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <X11/Xlib.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kWaitAttempts = 1800;
constexpr auto kEventDelay = std::chrono::milliseconds(100);

int ignore_x_error(Display* const, XErrorEvent* const) noexcept { return 0; }

Window find_window(Display* const display, const char* const caption) noexcept {
    const Window parent = RootWindow(display, DefaultScreen(display));
    Window root = 0;
    Window parent_return = 0;
    Window* children = nullptr;
    unsigned int child_count = 0;
    if (XQueryTree(display, parent, &root, &parent_return, &children, &child_count) == 0) {
        return 0;
    }

    Window found = 0;
    for (unsigned int index = 0; index < child_count && found == 0; ++index) {
        char* name = nullptr;
        if (XFetchName(display, children[index], &name) != 0 && name != nullptr &&
            (std::strcmp(name, caption) == 0 || std::strstr(name, caption) != nullptr)) {
            found = children[index];
        }
        if (name != nullptr) XFree(name);
    }
    if (children != nullptr) XFree(children);
    return found;
}

void collect_descendants(Display* const display, const Window parent,
                         std::vector<Window>* const windows) noexcept {
    Window root = 0;
    Window parent_return = 0;
    Window* children = nullptr;
    unsigned int child_count = 0;
    if (XQueryTree(display, parent, &root, &parent_return, &children, &child_count) == 0) {
        return;
    }
    for (unsigned int index = 0; index < child_count; ++index) {
        windows->push_back(children[index]);
        collect_descendants(display, children[index], windows);
    }
    if (children != nullptr) XFree(children);
}

bool send_motion(Display* const display, const std::vector<Window>& windows) noexcept {
    XEvent event{};
    event.xmotion.type = MotionNotify;
    event.xmotion.display = display;
    event.xmotion.root = RootWindow(display, DefaultScreen(display));
    event.xmotion.time = CurrentTime;
    event.xmotion.x = 40;
    event.xmotion.y = 40;
    event.xmotion.x_root = 40;
    event.xmotion.y_root = 40;
    event.xmotion.same_screen = True;
    bool sent = false;
    for (const Window window : windows) {
        event.xmotion.window = window;
        sent = XSendEvent(display, window, False, PointerMotionMask, &event) != 0 || sent;
    }
    const bool device = XTestFakeMotionEvent(display, DefaultScreen(display), 40, 40, 0) != 0;
    return sent || device;
}

bool send_button(Display* const display, const std::vector<Window>& windows, const int type) noexcept {
    XEvent event{};
    event.xbutton.type = type;
    event.xbutton.display = display;
    event.xbutton.root = RootWindow(display, DefaultScreen(display));
    event.xbutton.time = CurrentTime;
    event.xbutton.x = 40;
    event.xbutton.y = 40;
    event.xbutton.x_root = 40;
    event.xbutton.y_root = 40;
    event.xbutton.button = Button1;
    event.xbutton.same_screen = True;
    const long mask = type == ButtonPress ? ButtonPressMask : ButtonReleaseMask;
    bool sent = false;
    for (const Window window : windows) {
        event.xbutton.window = window;
        sent = XSendEvent(display, window, False, mask, &event) != 0 || sent;
    }
    const bool device = XTestFakeButtonEvent(display, Button1, type == ButtonPress, 0) != 0;
    return sent || device;
}

bool send_key(Display* const display, const std::vector<Window>& windows, const int type,
              const KeyCode keycode) noexcept {
    XEvent event{};
    event.xkey.type = type;
    event.xkey.display = display;
    event.xkey.root = RootWindow(display, DefaultScreen(display));
    event.xkey.time = CurrentTime;
    event.xkey.x = 40;
    event.xkey.y = 40;
    event.xkey.x_root = 40;
    event.xkey.y_root = 40;
    event.xkey.keycode = keycode;
    event.xkey.same_screen = True;
    const long mask = type == KeyPress ? KeyPressMask : KeyReleaseMask;
    bool sent = false;
    for (const Window window : windows) {
        event.xkey.window = window;
        sent = XSendEvent(display, window, False, mask, &event) != 0 || sent;
    }
    const bool device = XTestFakeKeyEvent(display, keycode, type == KeyPress, 0) != 0;
    return sent || device;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: proton_input_driver DISPLAY WINDOW_CAPTION\n");
        return 2;
    }

    Display* const display = XOpenDisplay(argv[1]);
    if (display == nullptr) {
        std::fprintf(stderr, "cannot open display %s\n", argv[1]);
        return 3;
    }
    XSetErrorHandler(ignore_x_error);

    int event_base = 0;
    int error_base = 0;
    int major_version = 0;
    int minor_version = 0;
    if (!XTestQueryExtension(display, &event_base, &error_base, &major_version,
                             &minor_version)) {
        std::fprintf(stderr, "XTest extension is unavailable\n");
        XCloseDisplay(display);
        return 6;
    }

    Window window = 0;
    for (int attempt = 0; attempt < kWaitAttempts && window == 0; ++attempt) {
        window = find_window(display, argv[2]);
        XSync(display, False);
        if (window == 0) std::this_thread::sleep_for(kEventDelay);
    }
    if (window == 0) {
        std::fprintf(stderr, "window not found: %s\n", argv[2]);
        XCloseDisplay(display);
        return 4;
    }

    std::vector<Window> windows{window};
    collect_descendants(display, window, &windows);
    XMapWindow(display, window);
    XRaiseWindow(display, window);
    XSetInputFocus(display, window, RevertToParent, CurrentTime);
    XSync(display, False);
    const KeyCode keycode = XKeysymToKeycode(display, XK_q);
    const bool sent = keycode != 0 && send_motion(display, windows);
    XFlush(display);
    std::this_thread::sleep_for(kEventDelay);
    const bool sent_down = sent && send_button(display, windows, ButtonPress);
    XFlush(display);
    std::this_thread::sleep_for(kEventDelay);
    const bool sent_up = sent_down && send_button(display, windows, ButtonRelease);
    XFlush(display);
    std::this_thread::sleep_for(kEventDelay);
    const bool sent_key_down = sent_up && send_key(display, windows, KeyPress, keycode);
    XFlush(display);
    std::this_thread::sleep_for(kEventDelay);
    const bool sent_key_up = sent_key_down && send_key(display, windows, KeyRelease, keycode);
    XFlush(display);
    XCloseDisplay(display);
    if (!sent_key_up) {
        std::fprintf(stderr, "failed to inject input events\n");
        return 5;
    }
    return 0;
}
