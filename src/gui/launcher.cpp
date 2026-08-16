#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <array>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr int kWidth = 1040;
constexpr int kHeight = 760;
constexpr int kMargin = 28;
constexpr int kInputY = 190;
constexpr int kInputHeight = 38;
constexpr int kButtonY = 246;
constexpr int kButtonHeight = 38;
constexpr int kOutputY = 330;
constexpr int kLineHeight = 18;

struct Palette { unsigned long background, header, card, border, text, muted, primary, success, danger, white; };
struct Button { int left, right; const char* label; unsigned long color; };

std::string ascii_fallback(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned char first = static_cast<unsigned char>(value[i]);
        if (first == 0xC3U && i + 1 < value.size()) {
            const unsigned char second = static_cast<unsigned char>(value[++i]);
            if (second == 0xA1U || second == 0xA2U || second == 0xA3U || second == 0xA4U || second == 0xA5U) result += 'a';
            else if (second == 0xA9U) result += 'e';
            else if (second == 0xADU) result += 'i';
            else if (second >= 0xB3U && second <= 0xB6U) result += 'o';
            else if (second >= 0xBAU && second <= 0xBDU) result += 'u';
            else if (second >= 0x81U && second <= 0x85U) result += 'A';
            else if (second == 0x89U) result += 'E';
            else if (second == 0x8DU) result += 'I';
            else if (second >= 0x93U && second <= 0x96U) result += 'O';
            else if (second >= 0x9AU && second <= 0x9DU) result += 'U';
            else result += '?';
        } else if (first == 0xC2U && i + 1 < value.size()) { ++i; result += '?';
        } else if (first < 0x80U) result += static_cast<char>(first);
        else result += '?';
    }
    return result;
}

unsigned long color(Display* display, Colormap map, const char* name, unsigned long fallback) {
    XColor actual{}, exact{};
    return XAllocNamedColor(display, map, name, &actual, &exact) != 0 ? actual.pixel : fallback;
}

void draw_text(Display* display, Window window, GC gc, XFontSet fonts, int x, int y, const std::string& value) {
    if (fonts != nullptr) Xutf8DrawString(display, window, fonts, gc, x, y, value.c_str(), static_cast<int>(value.size()));
    else { const std::string plain = ascii_fallback(value); XDrawString(display, window, gc, x, y, plain.c_str(), static_cast<int>(plain.size())); }
}

std::string read_pipe(int fd) {
    std::string result;
    std::array<char, 4096> buffer{};
    for (;;) {
        const ssize_t count = ::read(fd, buffer.data(), buffer.size());
        if (count > 0) result.append(buffer.data(), static_cast<std::size_t>(count));
        else if (count < 0 && errno == EINTR) continue;
        else break;
    }
    return result;
}

std::string run_runtime(const std::string& runtime, const std::string& executable, bool report) {
    int pipes[2] = {-1, -1};
    if (::pipe(pipes) != 0) return "Erro: nao foi possivel criar o pipe de diagnostico.\n";
    const pid_t child = ::fork();
    if (child < 0) { ::close(pipes[0]); ::close(pipes[1]); return "Erro: nao foi possivel iniciar o runtime.\n"; }
    if (child == 0) {
        ::close(pipes[0]); (void)::dup2(pipes[1], STDOUT_FILENO); (void)::dup2(pipes[1], STDERR_FILENO); ::close(pipes[1]);
        if (report) ::execl(runtime.c_str(), runtime.c_str(), "--trace", "--report", executable.c_str(), static_cast<char*>(nullptr));
        else ::execl(runtime.c_str(), runtime.c_str(), "--trace", executable.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }
    ::close(pipes[1]); const std::string output = read_pipe(pipes[0]); ::close(pipes[0]);
    int status = 0;
    if (::waitpid(child, &status, 0) < 0) return output + "\nErro: falha ao aguardar o processo convidado.\n";
    const int code = WIFEXITED(status) != 0 ? WEXITSTATUS(status) : 128;
    return output + "\n[launcher] codigo de saida: " + std::to_string(code) + "\n";
}

std::string choose_file() {
    int pipes[2] = {-1, -1};
    if (::pipe(pipes) != 0) return {};
    const pid_t child = ::fork();
    if (child == 0) {
        ::close(pipes[0]); (void)::dup2(pipes[1], STDOUT_FILENO); ::close(pipes[1]);
        ::execlp("zenity", "zenity", "--file-selection", "--title=Selecionar executavel Windows", static_cast<char*>(nullptr)); ::_exit(127);
    }
    if (child < 0) { ::close(pipes[0]); ::close(pipes[1]); return {}; }
    ::close(pipes[1]); std::string path = read_pipe(pipes[0]); ::close(pipes[0]); (void)::waitpid(child, nullptr, 0);
    while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
    return path;
}

std::string runtime_path(const char* argv0) {
    char resolved[4096]{};
    const ssize_t length = ::readlink("/proc/self/exe", resolved, sizeof(resolved) - 1U);
    if (length > 0) { resolved[length] = '\0'; return (std::filesystem::path(resolved).parent_path() / "tradutorlinux").string(); }
    return (std::filesystem::absolute(argv0).parent_path() / "tradutorlinux").string();
}

void redraw(Display* display, Window window, GC gc, XFontSet fonts, const Palette& p,
            const std::string& path, const std::string& status, const std::string& output) {
    XSetForeground(display, gc, p.background); XClearWindow(display, window);
    XSetForeground(display, gc, p.header); XFillRectangle(display, window, gc, 0, 0, kWidth, 92);
    XSetForeground(display, gc, p.white); draw_text(display, window, gc, fonts, kMargin, 38, "TradutorLinux");
    draw_text(display, window, gc, fonts, kMargin, 65, "Runtime Win32 para Linux x86-64");
    XSetForeground(display, gc, p.text); draw_text(display, window, gc, fonts, kMargin, 132, "Executavel PE32+ x86-64");
    XSetForeground(display, gc, p.muted); draw_text(display, window, gc, fonts, kMargin, 154, "Informe um arquivo .exe proprio ou selecione-o no disco.");

    XSetForeground(display, gc, p.card); XFillRectangle(display, window, gc, kMargin, kInputY, 824, kInputHeight);
    XSetForeground(display, gc, p.border); XDrawRectangle(display, window, gc, kMargin, kInputY, 824, kInputHeight);
    XSetForeground(display, gc, p.text); draw_text(display, window, gc, fonts, kMargin + 12, kInputY + 25, path.empty() ? "Nenhum arquivo selecionado" : path);
    const std::array<Button, 5> buttons{{
        {kMargin + 840, kMargin + 980, "Escolher...", p.muted}, {kMargin, kMargin + 130, "Analisar", p.primary},
        {kMargin + 142, kMargin + 272, "Executar", p.success}, {kMargin + 284, kMargin + 414, "Limpar", p.muted},
        {kMargin + 426, kMargin + 536, "Sair", p.danger},
    }};
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        const Button& button = buttons[i]; const int y = i == 0U ? kInputY : kButtonY;
        XSetForeground(display, gc, button.color); XFillRectangle(display, window, gc, button.left, y, static_cast<unsigned int>(button.right - button.left), kButtonHeight);
        XSetForeground(display, gc, p.white); draw_text(display, window, gc, fonts, button.left + 12, y + 25, button.label);
    }

    XSetForeground(display, gc, p.text); draw_text(display, window, gc, fonts, kMargin, 310, "Diagnostico e saida");
    XSetForeground(display, gc, p.primary); XFillRectangle(display, window, gc, kMargin, 318, 4, 22);
    XSetForeground(display, gc, p.card); XFillRectangle(display, window, gc, kMargin + 16, 318, 968, 34);
    XSetForeground(display, gc, p.muted); draw_text(display, window, gc, fonts, kMargin + 30, 341, "Status:");
    XSetForeground(display, gc, p.text); draw_text(display, window, gc, fonts, kMargin + 88, 341, status);
    XSetForeground(display, gc, p.card); XFillRectangle(display, window, gc, kMargin, kOutputY, 984, 390);
    XSetForeground(display, gc, p.border); XDrawRectangle(display, window, gc, kMargin, kOutputY, 984, 390);
    XSetForeground(display, gc, p.text);
    int y = kOutputY + 28; std::string line;
    const auto flush = [&]() { if (y < kOutputY + 370) { draw_text(display, window, gc, fonts, kMargin + 14, y, line); y += kLineHeight; } line.clear(); };
    for (const char character : output) { if (character == '\n') flush(); else if (character != '\r') { line += character; if (line.size() >= 124U) flush(); } }
    if (!line.empty()) flush();
    XFlush(display);
}

}  // namespace

int main(int argc, char** argv) {
    Display* const display = XOpenDisplay(nullptr);
    if (display == nullptr) { std::fprintf(stderr, "tradutorlinux_gui: nao foi possivel abrir o display X11\n"); return 2; }
    const int screen = DefaultScreen(display); const Colormap map = DefaultColormap(display, screen);
    const Palette p{
        color(display, map, "#f4f6f8", WhitePixel(display, screen)), color(display, map, "#1f2937", BlackPixel(display, screen)),
        color(display, map, "#ffffff", WhitePixel(display, screen)), color(display, map, "#cbd5e1", BlackPixel(display, screen)),
        color(display, map, "#111827", BlackPixel(display, screen)), color(display, map, "#64748b", BlackPixel(display, screen)),
        color(display, map, "#2563eb", BlackPixel(display, screen)), color(display, map, "#16a34a", BlackPixel(display, screen)),
        color(display, map, "#dc2626", BlackPixel(display, screen)), color(display, map, "#ffffff", WhitePixel(display, screen)),
    };
    const Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 80, 80, kWidth, kHeight, 0, p.border, p.background);
    XStoreName(display, window, "TradutorLinux"); XSelectInput(display, window, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask); XMapWindow(display, window);
    char** missing = nullptr; int missing_count = 0; char* default_string = nullptr;
    XFontSet fonts = XCreateFontSet(display, "-*-dejavu sans-*-r-*-*-14-*-*-*-*-*-iso10646-1", &missing, &missing_count, &default_string);
    if (missing != nullptr) XFreeStringList(missing);
    if (fonts == nullptr) { XFontStruct* const fallback = XLoadQueryFont(display, "9x15"); if (fallback != nullptr) XSetFont(display, DefaultGC(display, screen), fallback->fid); }
    const std::string runtime = runtime_path(argc > 0 ? argv[0] : "tradutorlinux_gui");
    std::string path; std::string status = "Pronto para analisar";
    std::string output = "Selecione um executavel e escolha Analisar para ver imports, secoes e compatibilidade.";
    bool running = true;
    while (running) {
        XEvent event{}; XNextEvent(display, &event); const GC gc = DefaultGC(display, screen);
        if (event.type == Expose) redraw(display, window, gc, fonts, p, path, status, output);
        else if (event.type == KeyPress) {
            char buffer[32]{}; KeySym keysym{}; const int length = XLookupString(&event.xkey, buffer, sizeof(buffer), &keysym, nullptr);
            if (keysym == XK_BackSpace && !path.empty()) path.pop_back();
            else if (keysym == XK_Return && !path.empty()) { status = "Analisando..."; redraw(display, window, gc, fonts, p, path, status, output); output = run_runtime(runtime, path, true); status = "Analise concluida"; }
            else if (length > 0 && std::isprint(static_cast<unsigned char>(buffer[0])) != 0 && path.size() < 4096U) path.append(buffer, static_cast<std::size_t>(length));
            redraw(display, window, gc, fonts, p, path, status, output);
        } else if (event.type == ButtonPress) {
            const int x = event.xbutton.x; const int y = event.xbutton.y;
            if (x >= kMargin + 840 && x <= kMargin + 980 && y >= kInputY && y <= kInputY + kInputHeight) {
                const std::string selected = choose_file(); if (!selected.empty()) { path = selected; status = "Arquivo selecionado"; }
            } else if (y >= kButtonY && y <= kButtonY + kButtonHeight) {
                if (x >= kMargin && x <= kMargin + 130 && !path.empty()) { status = "Analisando..."; redraw(display, window, gc, fonts, p, path, status, output); output = run_runtime(runtime, path, true); status = "Analise concluida"; }
                else if (x >= kMargin + 142 && x <= kMargin + 272 && !path.empty()) { status = "Executando..."; redraw(display, window, gc, fonts, p, path, status, output); output = run_runtime(runtime, path, false); status = "Execucao concluida"; }
                else if (x >= kMargin + 284 && x <= kMargin + 414) { path.clear(); status = "Pronto para analisar"; output = "Selecione um executavel e escolha Analisar para ver imports, secoes e compatibilidade."; }
                else if (x >= kMargin + 426 && x <= kMargin + 536) running = false;
            }
            redraw(display, window, gc, fonts, p, path, status, output);
        } else if (event.type == ClientMessage || event.type == DestroyNotify) running = false;
    }
    XDestroyWindow(display, window); if (fonts != nullptr) XFreeFontSet(display, fonts); XCloseDisplay(display); return 0;
}
