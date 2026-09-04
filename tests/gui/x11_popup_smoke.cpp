#include "tradutorlinux/gui/x11.hpp"

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr int kStartupTimeoutMs = 5000;
constexpr int kScenarioTimeoutMs = 2000;

[[noreturn]] void fail(const std::string& message) {
    std::fprintf(stderr, "x11_popup_smoke: %s\n", message.c_str());
    std::exit(1);
}

[[noreturn]] void skip(const std::string& message) {
    std::fprintf(stderr, "x11_popup_smoke: SKIP: %s\n", message.c_str());
    std::exit(77);
}

pid_t g_xvfb_pid = -1;

void stop_xvfb() noexcept {
    if (g_xvfb_pid > 0) {
        ::kill(g_xvfb_pid, SIGTERM);
        ::waitpid(g_xvfb_pid, nullptr, 0);
        g_xvfb_pid = -1;
    }
}

std::string start_xvfb() {
    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) != 0) {
        skip("pipe do Xvfb falhou");
    }
    const pid_t child = ::fork();
    if (child == 0) {
        ::close(pipe_fds[0]);
        if (::dup2(pipe_fds[1], 3) < 0) {
            ::_exit(127);
        }
        ::close(pipe_fds[1]);
        const int null_fd = ::open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            ::dup2(null_fd, STDOUT_FILENO);
            ::dup2(null_fd, STDERR_FILENO);
            ::close(null_fd);
        }
        ::execlp("Xvfb", "Xvfb", "-displayfd", "3", "-screen", "0", "640x480x24",
                 "-nolisten", "tcp", "-ac", static_cast<char*>(nullptr));
        ::_exit(127);
    }
    ::close(pipe_fds[1]);
    if (child < 0) {
        ::close(pipe_fds[0]);
        skip("fork do Xvfb falhou");
    }

    std::string display_number;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(kStartupTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline && display_number.empty()) {
        struct pollfd descriptor{pipe_fds[0], POLLIN | POLLHUP, 0};
        if (::poll(&descriptor, 1, 250) <= 0) {
            int status = 0;
            if (::waitpid(child, &status, WNOHANG) == child) {
                break;
            }
            continue;
        }
        char value = '\0';
        const ssize_t count = ::read(pipe_fds[0], &value, 1);
        if (count == 1 && value != '\n') {
            display_number.push_back(value);
        } else if (count <= 0) {
            break;
        }
    }
    ::close(pipe_fds[0]);
    if (display_number.empty()) {
        ::kill(child, SIGKILL);
        ::waitpid(child, nullptr, 0);
        skip("Xvfb não iniciou");
    }

    const std::string display = ":" + display_number;
    for (int attempt = 0; attempt < 50; ++attempt) {
        Display* const probe = XOpenDisplay(display.c_str());
        if (probe != nullptr) {
            XCloseDisplay(probe);
            g_xvfb_pid = child;
            return display;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ::kill(child, SIGKILL);
    ::waitpid(child, nullptr, 0);
    skip("display Xvfb não ficou disponível");
}

Window find_popup(Display* const dpy) noexcept {
    Window root_return = 0;
    Window parent_return = 0;
    Window* children = nullptr;
    unsigned int count = 0;
    if (XQueryTree(dpy, RootWindow(dpy, DefaultScreen(dpy)), &root_return, &parent_return,
                   &children, &count) == 0) {
        return 0;
    }
    Window result = 0;
    for (unsigned int index = 0; index < count && result == 0; ++index) {
        char* name = nullptr;
        if (XFetchName(dpy, children[index], &name) != 0 && name != nullptr &&
            std::strcmp(name, "TradutorLinuxPopup") == 0) {
            result = children[index];
        }
        if (name != nullptr) {
            XFree(name);
        }
    }
    if (children != nullptr) {
        XFree(children);
    }
    return result;
}

Window wait_for_popup(Display* const dpy) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(kScenarioTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (const Window popup = find_popup(dpy); popup != 0) {
            return popup;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 0;
}

void send_escape(Display* const dpy, const Window popup) {
    XEvent event{};
    event.xkey.type = KeyPress;
    event.xkey.display = dpy;
    event.xkey.window = popup;
    event.xkey.root = RootWindow(dpy, DefaultScreen(dpy));
    event.xkey.time = CurrentTime;
    event.xkey.keycode = XKeysymToKeycode(dpy, XK_Escape);
    event.xkey.same_screen = True;
    if (event.xkey.keycode == 0 ||
        XSendEvent(dpy, popup, False, KeyPressMask, &event) == 0) {
        fail("não foi possível enviar Escape");
    }
    XFlush(dpy);
}

void send_external_click(Display* const dpy, const Window popup) {
    XEvent event{};
    event.xbutton.type = ButtonPress;
    event.xbutton.display = dpy;
    event.xbutton.window = RootWindow(dpy, DefaultScreen(dpy));
    event.xbutton.root = event.xbutton.window;
    event.xbutton.time = CurrentTime;
    event.xbutton.x = 500;
    event.xbutton.y = 400;
    event.xbutton.x_root = 500;
    event.xbutton.y_root = 400;
    event.xbutton.button = Button1;
    event.xbutton.same_screen = True;
    if (XSendEvent(dpy, popup, False, ButtonPressMask, &event) == 0) {
        fail("não foi possível enviar clique externo");
    }
    XFlush(dpy);
}

void send_destroy(Display* const dpy, const Window popup) {
    XDestroyWindow(dpy, popup);
    XFlush(dpy);
}

enum class Action { Escape, ExternalClick, Destroy, Timeout };

void run_scenario(const std::string& display, const char* const timeout, const Action action) {
    ::setenv("DISPLAY", display.c_str(), 1);
    ::setenv("TL_GUI_POPUP_TIMEOUT_MS", timeout, 1);
    const std::vector<tradutorlinux::gui::PopupMenuItem> items{
        {.command = 17, .text = "Close", .separator = false},
        {.command = 23, .text = "Other", .separator = false},
    };
    std::atomic<std::uint32_t> result{0};
    std::atomic<bool> finished{false};
    const auto started = std::chrono::steady_clock::now();
    std::thread worker([&] {
        result.store(tradutorlinux::gui::track_popup_menu(items, 100, 100),
                     std::memory_order_relaxed);
        finished.store(true, std::memory_order_release);
    });

    Display* const dpy = XOpenDisplay(display.c_str());
    if (dpy == nullptr) {
        worker.join();
        fail("não foi possível abrir o display do cenário");
    }
    const Window popup = wait_for_popup(dpy);
    if (popup == 0) {
        XCloseDisplay(dpy);
        worker.join();
        fail("popup não apareceu no Xvfb");
    }
    switch (action) {
        case Action::Escape: send_escape(dpy, popup); break;
        case Action::ExternalClick: send_external_click(dpy, popup); break;
        case Action::Destroy: send_destroy(dpy, popup); break;
        case Action::Timeout: break;
    }
    XCloseDisplay(dpy);
    worker.join();

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    if (!finished.load(std::memory_order_acquire)) {
        fail("thread do popup não terminou");
    }
    if (result.load(std::memory_order_relaxed) != 0) {
        fail("cenário de cancelamento retornou comando inesperado");
    }
    if (elapsed.count() > kScenarioTimeoutMs) {
        fail("cenário de cancelamento excedeu o limite esperado");
    }
}

void exercise_color_cache(const std::string& display) {
    ::setenv("DISPLAY", display.c_str(), 1);
    tradutorlinux::gui::NativeWindow window =
        tradutorlinux::gui::create_window("Color cache", 320, 240);
    if (window == nullptr || !tradutorlinux::gui::map_window(window)) {
        if (window != nullptr) {
            tradutorlinux::gui::destroy_window(window);
        }
        fail("não foi possível criar a janela de teste de cores");
    }
    for (std::uint32_t index = 0; index < 512; ++index) {
        const std::uint32_t rgb = ((index * 37U) & 0xFFU) << 16U |
                                  ((index * 67U) & 0xFFU) << 8U |
                                  ((index * 97U) & 0xFFU);
        tradutorlinux::gui::fill_rectangle_color(window, static_cast<int>(index % 16U) * 20,
                                                  static_cast<int>(index / 16U) * 6, 16, 5, rgb);
    }
    tradutorlinux::gui::destroy_window(window);
}

}  // namespace

int main() {
    if (XInitThreads() == 0) {
        skip("Xlib não habilitou suporte a threads");
    }
    std::atexit(stop_xvfb);
    const std::string display = start_xvfb();
    exercise_color_cache(display);
    run_scenario(display, "2000", Action::Escape);
    run_scenario(display, "2000", Action::ExternalClick);
    run_scenario(display, "2000", Action::Destroy);
    run_scenario(display, "40", Action::Timeout);
    ::unsetenv("TL_GUI_POPUP_TIMEOUT_MS");
    return 0;
}
