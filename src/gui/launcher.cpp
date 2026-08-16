#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <X11/keysym.h>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr int kWidth = 900;
constexpr int kHeight = 700;
constexpr int kInputX = 24;
constexpr int kInputY = 74;
constexpr int kInputWidth = 852;
constexpr int kButtonY = 104;
constexpr int kOutputY = 184;
constexpr int kOutputBottom = kHeight - 24;
constexpr int kLineHeight = 17;

struct Button {
    int left;
    int right;
    const char* label;
};

constexpr std::array<Button, 4> kButtons{{
    {24, 154, "Analisar"},
    {166, 296, "Executar"},
    {308, 438, "Limpar"},
    {450, 560, "Sair"},
}};

bool inside(const Button& button, const int x, const int y) {
    return x >= button.left && x <= button.right && y >= kButtonY && y <= kButtonY + 32;
}

void draw_text(Display* display, const Window window, const GC gc, const int x, const int y,
               const std::string& text) {
    XDrawString(display, window, gc, x, y, text.c_str(), static_cast<int>(text.size()));
}

std::string read_pipe(const int fd) {
    std::string output;
    std::array<char, 4096> buffer{};
    for (;;) {
        const ssize_t count = ::read(fd, buffer.data(), buffer.size());
        if (count > 0) {
            output.append(buffer.data(), static_cast<std::size_t>(count));
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    return output;
}

std::string run_runtime(const std::string& runtime, const std::string& executable,
                        const bool report) {
    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) != 0) {
        return "Erro: não foi possível criar o pipe de diagnóstico\n";
    }
    const pid_t child = ::fork();
    if (child < 0) {
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        return "Erro: não foi possível iniciar o TradutorLinux\n";
    }
    if (child == 0) {
        ::close(pipe_fds[0]);
        ::dup2(pipe_fds[1], STDOUT_FILENO);
        ::dup2(pipe_fds[1], STDERR_FILENO);
        ::close(pipe_fds[1]);
        if (report) {
            ::execl(runtime.c_str(), runtime.c_str(), "--trace", "--report", executable.c_str(),
                    static_cast<char*>(nullptr));
        } else {
            ::execl(runtime.c_str(), runtime.c_str(), "--trace", executable.c_str(),
                    static_cast<char*>(nullptr));
        }
        ::_exit(127);
    }
    ::close(pipe_fds[1]);
    const std::string output = read_pipe(pipe_fds[0]);
    ::close(pipe_fds[0]);
    int status = 0;
    if (::waitpid(child, &status, 0) < 0) {
        return output + "\nErro: falha ao aguardar o processo convidado\n";
    }
    return output + "\n[launcher] código de saída: " +
           std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : 128) + "\n";
}

void redraw(Display* display, const Window window, const GC gc, const std::string& path,
            const std::string& output) {
    XClearWindow(display, window);
    draw_text(display, window, gc, 24, 28, "TradutorLinux - Launcher de aplicativos Win32");
    draw_text(display, window, gc, 24, 56, "Arquivo PE32+ x86-64:");
    XDrawRectangle(display, window, gc, kInputX, kInputY - 24, kInputWidth, 28);
    draw_text(display, window, gc, kInputX + 8, kInputY - 5, path);
    for (const Button& button : kButtons) {
        XDrawRectangle(display, window, gc, button.left, kButtonY,
                       static_cast<unsigned int>(button.right - button.left), 32U);
        draw_text(display, window, gc, button.left + 10, kButtonY + 21, button.label);
    }
    draw_text(display, window, gc, 24, 140, "Diagnóstico e saída:");
    XDrawRectangle(display, window, gc, 20, 154, kWidth - 40,
                   static_cast<unsigned int>(kOutputBottom - 154));
    int y = kOutputY;
    std::string line;
    constexpr std::size_t kMaxCharactersPerLine = 112;
    const auto flush_line = [&]() {
        if (y <= kOutputBottom - kLineHeight) {
            draw_text(display, window, gc, 24, y, line);
            y += kLineHeight;
        }
        line.clear();
    };
    for (const char character : output) {
        if (character == '\n') {
            flush_line();
        } else if (character != '\r') {
            line.push_back(character);
            if (line.size() >= kMaxCharactersPerLine) {
                flush_line();
            }
        }
    }
    if (!line.empty() && y <= kOutputBottom - kLineHeight) {
        draw_text(display, window, gc, 24, y, line);
    }
    XFlush(display);
}

}  // namespace

int main() {
    Display* const display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        std::fprintf(stderr, "tradutorlinux_gui: não foi possível abrir o display X11\n");
        return 2;
    }
    const int screen = DefaultScreen(display);
    const Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 80, 80,
                                              kWidth, kHeight, 1, BlackPixel(display, screen),
                                              WhitePixel(display, screen));
    XStoreName(display, window, "TradutorLinux");
    XSelectInput(display, window, ExposureMask | KeyPressMask | ButtonPressMask |
                                     StructureNotifyMask);
    XMapWindow(display, window);
    const GC gc = DefaultGC(display, screen);
    XFontStruct* const font = XLoadQueryFont(display, "9x15");
    if (font != nullptr) {
        XSetFont(display, gc, font->fid);
    }
    std::string path;
    std::string output = "Digite o caminho de um .exe e escolha uma operação.";
    bool running = true;
    while (running) {
        XEvent event{};
        XNextEvent(display, &event);
        if (event.type == Expose) {
            redraw(display, window, gc, path, output);
        } else if (event.type == KeyPress) {
            char buffer[32]{};
            KeySym keysym{};
            const int length = XLookupString(&event.xkey, buffer, sizeof(buffer), &keysym, nullptr);
            if (keysym == XK_BackSpace && !path.empty()) {
                path.pop_back();
            } else if (keysym == XK_Return && !path.empty()) {
                output = run_runtime("./build/debug/src/tradutorlinux", path, true);
            } else if (length > 0 && std::isprint(static_cast<unsigned char>(buffer[0])) != 0 &&
                       path.size() < 240) {
                path.append(buffer, static_cast<std::size_t>(length));
            }
            redraw(display, window, gc, path, output);
        } else if (event.type == ButtonPress) {
            for (const Button& button : kButtons) {
                if (!inside(button, event.xbutton.x, event.xbutton.y)) {
                    continue;
                }
                if (button.label == std::string_view{"Sair"}) {
                    running = false;
                } else if (button.label == std::string_view{"Limpar"}) {
                    path.clear();
                    output = "Digite o caminho de um .exe e escolha uma operação.";
                } else if (!path.empty() && button.label == std::string_view{"Analisar"}) {
                    output = run_runtime("./build/debug/src/tradutorlinux", path, true);
                } else if (!path.empty() && button.label == std::string_view{"Executar"}) {
                    output = run_runtime("./build/debug/src/tradutorlinux", path, false);
                }
                redraw(display, window, gc, path, output);
                break;
            }
        } else if (event.type == ClientMessage || event.type == DestroyNotify) {
            running = false;
        }
    }
    XDestroyWindow(display, window);
    if (font != nullptr) {
        XFreeFont(display, font);
    }
    XCloseDisplay(display);
    return 0;
}
