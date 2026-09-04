// Driver de integração GUI: executa as fixtures num Xvfb próprio e valida o
// message loop de ponta a ponta. Cenários:
//   1. autoclose  (TL_GUI_AUTOCLOSE_MS != 0): WM_QUIT sem interação.
//   2. fechar     (TL_GUI_AUTOCLOSE_MS == 0): envia WM_DELETE_WINDOW via X11,
//      como o botão de fechar de um window manager faria.
//   3. teclado    (TL_GUI_AUTOCLOSE_MS == 0): envia um KeyPress sintético 'q'
//      via X11; a fixture encerra quando recebe o WM_CHAR('q').
//   4. janelas    (opcional, com <input2>): duas janelas simultâneas, cada uma
//      com WNDPROC próprio; o driver envia 'q' à janela A e 'k' à janela B.
//   5. mouse      (opcional, com <input6>): envia MotionNotify, ButtonPress e
//      ButtonRelease sintéticos e depois 'q'; a fixture acumula flags de mouse.
//   6. dialog     (opcional, com <input7>): localiza o diálogo, envia Tab e
//      Enter e valida o retorno modal sem WM_QUIT.
// Exit codes: tl_win: 1 = WM_CREATE; 3 = WM_CREATE + WM_CHAR('q').
// tl_win2: 15 = create A + 'q' A + create B + 'k' B (flags 1+2+4+8).
// tl_paint: 127 = create+paint+down+up+move+click+q (flags 1+2+4+8+16+32+64).
// Cada cenário exige o exit code esperado, stdout vazio e os eventos do trace.

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>

namespace {

constexpr const char* kWindowCaption = "Ola do Windows no Linux!";
constexpr const char* kCaptionA = "Janela A";
constexpr const char* kCaptionB = "Janela B";
constexpr const char* kPaintCaption = "Pinte e Clique";
constexpr const char* kDialogCaption = "TL Dialog";
constexpr int kReadyTimeoutMs = 15000;
constexpr unsigned int kPollDelayUs = 100000;

pid_t g_xvfb_pid = -1;

void stop_xvfb() noexcept {
    if (g_xvfb_pid > 0) {
        ::kill(g_xvfb_pid, SIGTERM);
        ::waitpid(g_xvfb_pid, nullptr, 0);
        g_xvfb_pid = -1;
    }
}

[[noreturn]] void fail(const std::string& message) {
    stop_xvfb();
    std::fprintf(stderr, "runtime_gui_smoke: %s\n", message.c_str());
    std::exit(1);
}

[[noreturn]] void skip(const std::string& message) {
    std::fprintf(stderr, "runtime_gui_smoke: SKIP: %s\n", message.c_str());
    std::exit(77);
}

std::string read_file(const std::string& path) {
    std::ifstream stream(path);
    std::string contents;
    if (stream) {
        std::string line;
        while (std::getline(stream, line)) {
            contents += line;
            contents += '\n';
        }
    }
    return contents;
}

void require_trace_contains(const std::string& trace, const std::string& needle,
                            const std::string& scenario) {
    if (trace.find(needle) == std::string::npos) {
        std::fprintf(stderr,
                     "runtime_gui_smoke: '%s' não encontrado no trace (%s):\n%s\n",
                     needle.c_str(), scenario.c_str(), trace.c_str());
        std::exit(1);
    }
}

// O teste roda sempre num Xvfb próprio, sem window manager: a janela é filha
// direta da root (find_window_by_caption a encontra) e os eventos sintéticos
// chegam ao runtime. Num display com WM (ex.: sessão mutter) a janela é
// reparentada e o KeyPress iria para o frame, não para o cliente.
std::string start_xvfb() {
    int display_pipe[2] = {-1, -1};
    if (::pipe(display_pipe) != 0) {
        skip("não foi possível criar o pipe do Xvfb");
    }
    const pid_t server_pid = ::fork();
    if (server_pid == 0) {
        ::close(display_pipe[0]);
        if (::dup2(display_pipe[1], 3) < 0) {
            ::_exit(127);
        }
        ::close(display_pipe[1]);
        const int null_fd = ::open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            ::dup2(null_fd, STDERR_FILENO);
            ::close(null_fd);
        }
        ::execlp("Xvfb", "Xvfb", "-displayfd", "3", "-screen", "0", "640x480x24",
                 "-nolisten", "tcp", "-ac", static_cast<char*>(nullptr));
        ::_exit(127);
    }
    ::close(display_pipe[1]);
    if (server_pid < 0) {
        ::close(display_pipe[0]);
        skip("fork falhou ao iniciar Xvfb");
    }

    std::string display_number;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(kReadyTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline && display_number.empty()) {
        struct pollfd descriptor{display_pipe[0], POLLIN | POLLHUP, 0};
        const int remaining = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count());
        if (::poll(&descriptor, 1, std::max(1, std::min(remaining, 250))) <= 0) {
            int status = 0;
            if (::waitpid(server_pid, &status, WNOHANG) == server_pid) {
                break;
            }
            continue;
        }
        char character = '\0';
        const ::ssize_t count = ::read(display_pipe[0], &character, 1);
        if (count == 1) {
            if (character == '\n') {
                break;
            }
            display_number.push_back(character);
        } else if (count == 0) {
            break;
        }
    }
    ::close(display_pipe[0]);
    if (!display_number.empty()) {
        const std::string display = ":" + display_number;
        for (int attempt = 0; attempt < 50; ++attempt) {
            Display* const probe = XOpenDisplay(display.c_str());
            if (probe != nullptr) {
                XCloseDisplay(probe);
                g_xvfb_pid = server_pid;
                return display;
            }
            ::usleep(kPollDelayUs);
        }
    }
    ::kill(server_pid, SIGKILL);
    ::waitpid(server_pid, nullptr, 0);
    skip("falha ao iniciar Xvfb (instale o pacote xvfb ou verifique o acesso a /tmp/.X11-unix)");
}

Window find_window_by_caption(Display* const dpy, const char* const caption) noexcept {
    Window root_return = 0;
    Window parent_return = 0;
    Window* children = nullptr;
    unsigned int child_count = 0;
    if (XQueryTree(dpy, RootWindow(dpy, DefaultScreen(dpy)), &root_return, &parent_return,
                   &children, &child_count) == 0) {
        return 0;
    }
    Window found = 0;
    for (unsigned int i = 0; i < child_count && found == 0; ++i) {
        char* name = nullptr;
        if (XFetchName(dpy, children[i], &name) != 0 && name != nullptr &&
            std::strcmp(name, caption) == 0) {
            found = children[i];
        }
        if (name != nullptr) {
            XFree(name);
        }
    }
    if (children != nullptr) {
        XFree(children);
    }
    return found;
}

bool send_wm_delete(const std::string& display) {
    Display* const dpy = XOpenDisplay(display.c_str());
    if (dpy == nullptr) {
        return false;
    }
    const Window window = find_window_by_caption(dpy, kWindowCaption);
    if (window == 0) {
        XCloseDisplay(dpy);
        return false;
    }
    const Atom protocols_atom = XInternAtom(dpy, "WM_PROTOCOLS", False);
    const Atom delete_atom = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.display = dpy;
    event.xclient.window = window;
    event.xclient.message_type = protocols_atom;
    event.xclient.format = 32;
    event.xclient.data.l[0] = static_cast<long>(delete_atom);
    event.xclient.data.l[1] = static_cast<long>(CurrentTime);
    XSendEvent(dpy, window, False, NoEventMask, &event);
    XFlush(dpy);
    XCloseDisplay(dpy);
    return true;
}

bool send_key_state(const std::string& display, const char* const caption,  // NOLINT(bugprone-easily-swappable-parameters)
                    const char* const keysym_name, const int type,  // NOLINT(bugprone-easily-swappable-parameters)
                    const unsigned int state) {
    Display* const dpy = XOpenDisplay(display.c_str());
    if (dpy == nullptr) {
        return false;
    }
    const Window window = find_window_by_caption(dpy, caption);
    if (window == 0) {
        XCloseDisplay(dpy);
        return false;
    }
    const KeyCode keycode = XKeysymToKeycode(dpy, XStringToKeysym(keysym_name));
    if (keycode == 0) {
        XCloseDisplay(dpy);
        return false;
    }
    XEvent event{};
    event.xkey.type = type;
    event.xkey.display = dpy;
    event.xkey.window = window;
    event.xkey.root = RootWindow(dpy, DefaultScreen(dpy));
    event.xkey.time = CurrentTime;
    event.xkey.x = 10;
    event.xkey.y = 10;
    event.xkey.x_root = 10;
    event.xkey.y_root = 10;
    event.xkey.state = static_cast<unsigned int>(state);
    event.xkey.keycode = keycode;
    event.xkey.same_screen = True;
    const long mask = type == KeyPress ? KeyPressMask : KeyReleaseMask;
    XSendEvent(dpy, window, False, mask, &event);
    XFlush(dpy);
    XCloseDisplay(dpy);
    return true;
}

bool send_key(const std::string& display, const char* const caption,  // NOLINT(bugprone-easily-swappable-parameters)
              const char* const keysym_name) {
    return send_key_state(display, caption, keysym_name, KeyPress, 0);
}

// Envia KeyPress + KeyRelease da mesma tecla (estado opcional de Shift),
// como uma digitação real; o runtime deve produzir WM_KEYDOWN, WM_CHAR
// (quando há caractere) e WM_KEYUP.
bool send_key_pair(const std::string& display, const char* const caption,  // NOLINT(bugprone-easily-swappable-parameters)
                   const char* const keysym_name, const bool shift) {
    const unsigned int state = shift ? static_cast<unsigned int>(ShiftMask) : 0;
    return send_key_state(display, caption, keysym_name, KeyPress, state) &&
           send_key_state(display, caption, keysym_name, KeyRelease, state);
}

// Envia um evento sintético de mouse (ButtonPress, ButtonRelease ou
// MotionNotify do botão 1) para a janela com o título informado nas
// coordenadas de cliente dadas; o runtime deve produzir WM_LBUTTONDOWN,
// WM_LBUTTONUP ou WM_MOUSEMOVE.
bool send_mouse_state(const std::string& display, const char* const caption,  // NOLINT(bugprone-easily-swappable-parameters)
                      const int x, const int y, const int type, const long mask) {
    Display* const dpy = XOpenDisplay(display.c_str());
    if (dpy == nullptr) {
        return false;
    }
    const Window window = find_window_by_caption(dpy, caption);
    if (window == 0) {
        XCloseDisplay(dpy);
        return false;
    }
    XEvent event{};
    event.xbutton.type = type;
    event.xbutton.display = dpy;
    event.xbutton.window = window;
    event.xbutton.root = RootWindow(dpy, DefaultScreen(dpy));
    event.xbutton.time = CurrentTime;
    event.xbutton.x = x;
    event.xbutton.y = y;
    event.xbutton.x_root = x;
    event.xbutton.y_root = y;
    event.xbutton.button = 1;
    event.xbutton.state = 0;
    event.xbutton.same_screen = True;
    XSendEvent(dpy, window, False, mask, &event);
    XFlush(dpy);
    XCloseDisplay(dpy);
    return true;
}

bool send_motion(const std::string& display, const char* const caption, const int x, const int y) {
    return send_mouse_state(display, caption, x, y, MotionNotify, PointerMotionMask);
}

bool send_button(const std::string& display, const char* const caption, const int x, const int y,
                 const int type) {
    const long mask = type == ButtonPress ? ButtonPressMask : ButtonReleaseMask;
    return send_mouse_state(display, caption, x, y, type, mask);
}

enum class Trigger : unsigned char {
    Nothing,
    CloseRequest,
    KeyQ,
    TwoKeys,
    KeyPairs,
    Mouse,
    Dialog,
};

struct RunOptions {
    bool autoclose;
    Trigger trigger;
    int expected_exit;
    std::string trace_path;
    std::string stdout_path;
};

void run_runtime(const std::string& runtime, const std::string& input,
                 const std::string& display, const RunOptions& options) {
    const pid_t runtime_pid = ::fork();
    if (runtime_pid == 0) {
        if (::setenv("DISPLAY", display.c_str(), 1) != 0 ||
            ::setenv("TL_GUI_AUTOCLOSE_MS", options.autoclose ? "1" : "0", 1) != 0) {
            ::_exit(127);
        }
        const int out_fd = ::open(options.stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        const int err_fd = ::open(options.trace_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd < 0 || err_fd < 0) {
            ::_exit(127);
        }
        ::dup2(out_fd, STDOUT_FILENO);
        ::dup2(err_fd, STDERR_FILENO);
        ::execl(runtime.c_str(), runtime.c_str(), "--trace", input.c_str(),
                static_cast<char*>(nullptr));
        ::_exit(127);
    }
    if (runtime_pid < 0) {
        fail("fork falhou ao lançar o runtime");
    }

    if (options.trigger != Trigger::Nothing) {
        bool sent_a = false;
        bool sent_b = false;
        bool sent_c = false;
        bool done = false;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(kReadyTimeoutMs);
        while (!done && std::chrono::steady_clock::now() < deadline) {
            if (options.trigger == Trigger::CloseRequest) {
                done = send_wm_delete(display);
            } else if (options.trigger == Trigger::KeyQ) {
                done = send_key(display, kWindowCaption, "q");
            } else if (options.trigger == Trigger::KeyPairs) {
                // Ordem importa: Shift+q, Return, Left. A fixture encerra após
                // o WM_KEYUP de Left.
                if (!sent_a) {
                    sent_a = send_key_pair(display, kWindowCaption, "q", true);
                } else if (!sent_b) {
                    sent_b = send_key_pair(display, kWindowCaption, "Return", false);
                } else {
                    done = send_key_pair(display, kWindowCaption, "Left", false);
                }
            } else if (options.trigger == Trigger::Mouse) {
                // Ordem importa: movimento, pressionar, soltar no botao, 'q'.
                // As coordenadas (50,50)/(200,100) estão no client area da
                // janela 480x260 da fixture tl_paint; o botao fica em
                // (170,90)-(310,130).
                if (!sent_a) {
                    sent_a = send_motion(display, kPaintCaption, 50, 50);
                } else if (!sent_b) {
                    sent_b = send_button(display, kPaintCaption, 200, 100, ButtonPress);
                } else if (!sent_c) {
                    sent_c = send_button(display, kPaintCaption, 200, 100, ButtonRelease);
                } else {
                    done = send_key(display, kPaintCaption, "q");
                }
            } else if (options.trigger == Trigger::Dialog) {
                if (!sent_a) {
                    sent_a = send_key(display, kDialogCaption, "Tab");
                } else {
                    done = send_key(display, kDialogCaption, "Return");
                }
            } else {
                if (!sent_a) {
                    sent_a = send_key(display, kCaptionA, "q");
                }
                if (!sent_b) {
                    sent_b = send_key(display, kCaptionB, "k");
                }
                done = sent_a && sent_b;
            }
            if (!done) {
                ::usleep(kPollDelayUs);
            }
        }
        if (!done) {
            ::kill(runtime_pid, SIGKILL);
            ::waitpid(runtime_pid, nullptr, 0);
            fail("janela(s) não encontrada(s) para enviar o(s) evento(s) do cenário");
        }
    }

    int status = 0;
    pid_t result = 0;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(kReadyTimeoutMs);
    while ((result = ::waitpid(runtime_pid, &status, WNOHANG)) == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        ::usleep(kPollDelayUs);
    }
    if (result < 0) {
        ::kill(runtime_pid, SIGKILL);
        ::waitpid(runtime_pid, &status, 0);
        fail("waitpid falhou ao aguardar o runtime");
    }
    if (result == 0) {
        ::kill(runtime_pid, SIGKILL);
        ::waitpid(runtime_pid, &status, 0);
        fail("timeout aguardando o runtime encerrar");
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != options.expected_exit) {
        fail("runtime não terminou com o exit-code esperado " +
             std::to_string(options.expected_exit));
    }
}

struct RunExpectations {
    int expected_exit;
    std::vector<std::string> registers;  // needles do RegisterClassExA
    std::vector<std::string> creates;    // needles do CreateWindowExA
    std::vector<std::string> chars;      // needles do TranslateMessage WM_CHAR (opcional)
    std::vector<std::string> extra;      // needles adicionais de runtime (opcional)
};

void verify_run(const std::string& work_dir, const std::string& scenario,
                const RunExpectations& expected) {
    const std::string trace = read_file(work_dir + "/trace_" + scenario + ".log");
    const std::string output = read_file(work_dir + "/stdout_" + scenario + ".log");
    const bool dialog_scenario = scenario == "dialog";
    const std::string expected_stdout = dialog_scenario ? "dialog\n" : std::string{};
    if (output != expected_stdout) {
        fail("stdout inesperado no cenário " + scenario + ": '" + output + "'");
    }
    const std::string exit_code = std::to_string(expected.expected_exit);
    const std::array<std::string, 3> base = {
        "GetMessageA symbol=\"GetMessageA\" message=\"WM_QUIT\" exit-code=\"" + exit_code +
            "\" result=\"quit\"",
        "ExitProcess symbol=\"ExitProcess\" exit-code=\"" + exit_code +
            "\" status=\"success\" mechanism=\"guest-transfer\"",
        "exit exit-code=\"" + exit_code + "\" explicit=\"sim\"",
    };
    if (!dialog_scenario) {
        for (const std::string& needle : base) {
            require_trace_contains(trace, needle, scenario);
        }
    }
    for (const std::string& needle : expected.registers) {
        require_trace_contains(trace, needle, scenario);
    }
    for (const std::string& needle : expected.creates) {
        require_trace_contains(trace, needle, scenario);
    }
    for (const std::string& needle : expected.chars) {
        require_trace_contains(trace, needle, scenario);
    }
    for (const std::string& needle : expected.extra) {
        require_trace_contains(trace, needle, scenario);
    }
}

const std::string kWinRegisters[] = {
    "RegisterClassExA symbol=\"RegisterClassExA\" class=\"tlwin\" atom=\"1\" "
    "status=\"success\"",
};
const std::string kWinCreates[] = {
    "CreateWindowExA symbol=\"CreateWindowExA\" class=\"tlwin\" "
    "window=\"Ola do Windows no Linux!\" status=\"success\"",
};
const std::string kWinKeyChar = "TranslateMessage symbol=\"TranslateMessage\" message=\"WM_CHAR\" "
                                "wparam=\"113\" status=\"translated\"";

}  // namespace

int main(const int argc, char** argv) {
    if (argc < 4 || argc > 10) {
        std::fprintf(stderr,
                     "uso: runtime_gui_smoke <runtime> <input> <work-dir> [input2] [input3] "
                     "[input4] [input5] [input6] [input7]\n");
        return 2;
    }
    const std::string runtime = argv[1];
    const std::string input = argv[2];
    const std::string work_dir = argv[3];
    const std::string input2 = argc >= 5 ? argv[4] : std::string{};
    const std::string input3 = argc >= 6 ? argv[5] : std::string{};
    const std::string input4 = argc >= 7 ? argv[6] : std::string{};
    const std::string input5 = argc >= 8 ? argv[7] : std::string{};
    const std::string input6 = argc >= 9 ? argv[8] : std::string{};
    const std::string input7 = argc >= 10 ? argv[9] : std::string{};

    std::error_code error;
    std::filesystem::create_directories(work_dir, error);
    if (error) {
        fail("falha ao criar o diretório de trabalho");
    }

    const std::string display = start_xvfb();

    const RunOptions autoclose_options{
        .autoclose = true,
        .trigger = Trigger::Nothing,
        .expected_exit = 1,
        .trace_path = work_dir + "/trace_autoclose.log",
        .stdout_path = work_dir + "/stdout_autoclose.log",
    };
    run_runtime(runtime, input, display, autoclose_options);
    verify_run(work_dir, "autoclose",
               RunExpectations{1, {kWinRegisters[0]}, {kWinCreates[0]}, {}, {}});

    const RunOptions close_options{
        .autoclose = false,
        .trigger = Trigger::CloseRequest,
        .expected_exit = 1,
        .trace_path = work_dir + "/trace_close.log",
        .stdout_path = work_dir + "/stdout_close.log",
    };
    run_runtime(runtime, input, display, close_options);
    verify_run(work_dir, "close",
               RunExpectations{1, {kWinRegisters[0]}, {kWinCreates[0]}, {}, {}});

    const RunOptions key_options{
        .autoclose = false,
        .trigger = Trigger::KeyQ,
        .expected_exit = 3,
        .trace_path = work_dir + "/trace_key.log",
        .stdout_path = work_dir + "/stdout_key.log",
    };
    run_runtime(runtime, input, display, key_options);
    verify_run(work_dir, "key",
               RunExpectations{3, {kWinRegisters[0]}, {kWinCreates[0]}, {kWinKeyChar}, {}});

    if (!input2.empty()) {
        const RunOptions windows_options{
            .autoclose = false,
            .trigger = Trigger::TwoKeys,
            .expected_exit = 15,
            .trace_path = work_dir + "/trace_windows.log",
            .stdout_path = work_dir + "/stdout_windows.log",
        };
        run_runtime(runtime, input2, display, windows_options);
        verify_run(
            work_dir, "windows",
            RunExpectations{
                15,
                {"RegisterClassExA symbol=\"RegisterClassExA\" class=\"tlwin2a\" "
                 "atom=\"1\" status=\"success\"",
                 "RegisterClassExA symbol=\"RegisterClassExA\" class=\"tlwin2b\" "
                 "atom=\"2\" status=\"success\""},
                {"CreateWindowExA symbol=\"CreateWindowExA\" class=\"tlwin2a\" "
                 "window=\"Janela A\" status=\"success\"",
                 "CreateWindowExA symbol=\"CreateWindowExA\" class=\"tlwin2b\" "
                 "window=\"Janela B\" status=\"success\""},
                 {"TranslateMessage symbol=\"TranslateMessage\" message=\"WM_CHAR\" "
                  "wparam=\"113\" status=\"translated\"",
                  "TranslateMessage symbol=\"TranslateMessage\" message=\"WM_CHAR\" "
                  "wparam=\"107\" status=\"translated\""},
                {}});
    }

    if (!input3.empty()) {
        const RunOptions keys_options{
            .autoclose = false,
            .trigger = Trigger::KeyPairs,
            .expected_exit = 7,
            .trace_path = work_dir + "/trace_keys.log",
            .stdout_path = work_dir + "/stdout_keys.log",
        };
        run_runtime(runtime, input3, display, keys_options);
        verify_run(
            work_dir, "keys",
            RunExpectations{
                7,
                {"RegisterClassExA symbol=\"RegisterClassExA\" class=\"tlkey\" "
                 "atom=\"1\" status=\"success\""},
                {"CreateWindowExA symbol=\"CreateWindowExA\" class=\"tlkey\" "
                 "window=\"Ola do Windows no Linux!\" status=\"success\""},
                {"TranslateMessage symbol=\"TranslateMessage\" message=\"WM_CHAR\" "
                 "wparam=\"81\" status=\"translated\""},
                {}});
    }

    if (!input4.empty()) {
        const RunOptions timer_options{
            .autoclose = false,
            .trigger = Trigger::Nothing,
            .expected_exit = 7,
            .trace_path = work_dir + "/trace_timer.log",
            .stdout_path = work_dir + "/stdout_timer.log",
        };
        run_runtime(runtime, input4, display, timer_options);
        verify_run(
            work_dir, "timer",
            RunExpectations{
                7,
                {"RegisterClassExA symbol=\"RegisterClassExA\" class=\"tltimer\" "
                 "atom=\"1\" status=\"success\""},
                {"CreateWindowExA symbol=\"CreateWindowExA\" class=\"tltimer\" "
                 "window=\"Ola do Windows no Linux!\" status=\"success\""},
                {},
                 {"SetTimer symbol=\"SetTimer\" id=\"1\" elapsed-ms=\"200\" status=\"success\"",
                  "GetMessageA symbol=\"GetMessageA\" message=\"WM_TIMER\" id=\"1\" "
                  "status=\"delivered\"",
                  "KillTimer symbol=\"KillTimer\" id=\"1\" status=\"success\""},
            });
    }

    if (!input5.empty()) {
        const RunOptions gdi_options{
            .autoclose = false,
            .trigger = Trigger::Nothing,
            .expected_exit = 3,
            .trace_path = work_dir + "/trace_gdi.log",
            .stdout_path = work_dir + "/stdout_gdi.log",
        };
        run_runtime(runtime, input5, display, gdi_options);
        verify_run(
            work_dir, "gdi",
            RunExpectations{
                3,
                {"RegisterClassExA symbol=\"RegisterClassExA\" class=\"tlgdi\" "
                 "atom=\"1\" status=\"success\""},
                {"CreateWindowExA symbol=\"CreateWindowExA\" class=\"tlgdi\" "
                 "window=\"Ola do Windows no Linux!\" status=\"success\""},
                {},
                {"GetStockObject symbol=\"GetStockObject\" object=\"0\" status=\"success\"",
                 "BeginPaint symbol=\"BeginPaint\" status=\"success\"",
                 "TextOut symbol=\"TextOut\" x=\"10\" y=\"10\" length=\"17\"",
                 "EndPaint symbol=\"EndPaint\" status=\"success\""},
            });
    }

    if (!input6.empty()) {
        const RunOptions paint_options{
            .autoclose = false,
            .trigger = Trigger::Mouse,
            .expected_exit = 127,
            .trace_path = work_dir + "/trace_paint.log",
            .stdout_path = work_dir + "/stdout_paint.log",
        };
        run_runtime(runtime, input6, display, paint_options);
        verify_run(
            work_dir, "paint",
            RunExpectations{
                127,
                {"RegisterClassExA symbol=\"RegisterClassExA\" class=\"tlpaint\" "
                 "atom=\"1\" status=\"success\""},
                {"CreateWindowExA symbol=\"CreateWindowExA\" class=\"tlpaint\" "
                 "window=\"Pinte e Clique\" status=\"success\""},
                {"TranslateMessage symbol=\"TranslateMessage\" message=\"WM_CHAR\" "
                 "wparam=\"113\" status=\"translated\""},
                {"GetStockObject symbol=\"GetStockObject\" object=\"0\" status=\"success\"",
                 "FillRect symbol=\"FillRect\" brush=\"1\"",
                 "Rectangle symbol=\"Rectangle\"",
                 "BeginPaint symbol=\"BeginPaint\" status=\"success\"",
                 "EndPaint symbol=\"EndPaint\" status=\"success\""},
            });
    }

    if (!input7.empty()) {
        const RunOptions dialog_options{
            .autoclose = false,
            .trigger = Trigger::Dialog,
            .expected_exit = 0,
            .trace_path = work_dir + "/trace_dialog.log",
            .stdout_path = work_dir + "/stdout_dialog.log",
        };
        run_runtime(runtime, input7, display, dialog_options);
        verify_run(work_dir, "dialog",
                   RunExpectations{0, {}, {}, {},
                                   {"DialogBoxParamW symbol=\"DialogBoxParamW\" template=\"101\"",
                                    "IsDialogMessageW symbol=\"IsDialogMessageW\" action=\"tab\"",
                                    "IsDialogMessageW symbol=\"IsDialogMessageW\" action=\"enter\"",
                                    "EndDialog symbol=\"EndDialog\" result=\"42\" status=\"success\"",
                                    "DialogBoxParamW symbol=\"DialogBoxParamW\" result=\"42\" status=\"returned\""}});
    }

    stop_xvfb();
    return 0;
}
