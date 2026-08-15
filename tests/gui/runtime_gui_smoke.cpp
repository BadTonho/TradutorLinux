// Driver de integração GUI: executa as fixtures num Xvfb próprio e valida o
// message loop de ponta a ponta. Cenários:
//   1. autoclose  (TL_GUI_AUTOCLOSE_MS != 0): WM_QUIT sem interação.
//   2. fechar     (TL_GUI_AUTOCLOSE_MS == 0): envia WM_DELETE_WINDOW via X11,
//      como o botão de fechar de um window manager faria.
//   3. teclado    (TL_GUI_AUTOCLOSE_MS == 0): envia um KeyPress sintético 'q'
//      via X11; a fixture encerra quando recebe o WM_CHAR('q').
//   4. janelas    (opcional, com <input2>): duas janelas simultâneas, cada uma
//      com WNDPROC próprio; o driver envia 'q' à janela A e 'k' à janela B.
// Exit codes: tl_win: 1 = WM_CREATE; 3 = WM_CREATE + WM_CHAR('q').
// tl_win2: 15 = create A + 'q' A + create B + 'k' B (flags 1+2+4+8).
// Cada cenário exige o exit code esperado, stdout vazio e os eventos do trace.

#include <X11/Xatom.h>
#include <X11/Xlib.h>

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

namespace {

constexpr const char* kWindowCaption = "Ola do Windows no Linux!";
constexpr const char* kCaptionA = "Janela A";
constexpr const char* kCaptionB = "Janela B";
constexpr int kReadyTimeoutMs = 15000;
constexpr unsigned int kPollDelayUs = 100000;

[[noreturn]] void fail(const std::string& message) {
    std::fprintf(stderr, "runtime_gui_smoke: %s\n", message.c_str());
    std::exit(1);
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

pid_t g_xvfb_pid = -1;

// O teste roda sempre num Xvfb próprio, sem window manager: a janela é filha
// direta da root (find_window_by_caption a encontra) e os eventos sintéticos
// chegam ao runtime. Num display com WM (ex.: sessão mutter) a janela é
// reparentada e o KeyPress iria para o frame, não para o cliente.
std::string start_xvfb() {
    for (int number = 90; number < 120; ++number) {
        const std::string socket = "/tmp/.X11-unix/X" + std::to_string(number);
        if (::access(socket.c_str(), F_OK) == 0) {
            continue;
        }
        const pid_t server_pid = ::fork();
        if (server_pid == 0) {
            const std::string display_arg = ":" + std::to_string(number);
            ::execlp("Xvfb", "Xvfb", display_arg.c_str(), "-screen", "0", "640x480x24",
                     "-nolisten", "tcp", "-ac", static_cast<char*>(nullptr));
            ::_exit(127);
        }
        if (server_pid > 0) {
            const std::string display = ":" + std::to_string(number);
            for (int attempt = 0; attempt < 50; ++attempt) {
                Display* const probe = XOpenDisplay(display.c_str());
                if (probe != nullptr) {
                    XCloseDisplay(probe);
                    g_xvfb_pid = server_pid;
                    return display;
                }
                ::usleep(kPollDelayUs);
            }
            ::kill(server_pid, SIGKILL);
            ::waitpid(server_pid, nullptr, 0);
        }
    }
    fail("falha ao iniciar Xvfb (instale o pacote xvfb)");
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

bool send_key(const std::string& display, const char* const caption,  // NOLINT(bugprone-easily-swappable-parameters)
              const char* const keysym_name) {
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
    event.xkey.type = KeyPress;
    event.xkey.display = dpy;
    event.xkey.window = window;
    event.xkey.root = RootWindow(dpy, DefaultScreen(dpy));
    event.xkey.time = CurrentTime;
    event.xkey.x = 10;
    event.xkey.y = 10;
    event.xkey.x_root = 10;
    event.xkey.y_root = 10;
    event.xkey.state = 0;
    event.xkey.keycode = keycode;
    event.xkey.same_screen = True;
    XSendEvent(dpy, window, False, KeyPressMask, &event);
    XFlush(dpy);
    XCloseDisplay(dpy);
    return true;
}

enum class Trigger : unsigned char {
    Nothing,
    CloseRequest,
    KeyQ,
    TwoKeys,
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
        bool done = false;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(kReadyTimeoutMs);
        while (!done && std::chrono::steady_clock::now() < deadline) {
            if (options.trigger == Trigger::CloseRequest) {
                done = send_wm_delete(display);
            } else if (options.trigger == Trigger::KeyQ) {
                done = send_key(display, kWindowCaption, "q");
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
};

void verify_run(const std::string& work_dir, const std::string& scenario,
                const RunExpectations& expected) {
    const std::string trace = read_file(work_dir + "/trace_" + scenario + ".log");
    const std::string output = read_file(work_dir + "/stdout_" + scenario + ".log");
    if (!output.empty()) {
        fail("stdout não vazio (deve ir tudo para stderr) no cenário " + scenario);
    }
    const std::string exit_code = std::to_string(expected.expected_exit);
    const std::array<std::string, 3> base = {
        "GetMessageA symbol=\"GetMessageA\" message=\"WM_QUIT\" exit-code=\"" + exit_code +
            "\" result=\"quit\"",
        "ExitProcess symbol=\"ExitProcess\" exit-code=\"" + exit_code +
            "\" status=\"success\" mechanism=\"guest-transfer\"",
        "exit exit-code=\"" + exit_code + "\" explicit=\"sim\"",
    };
    for (const std::string& needle : expected.registers) {
        require_trace_contains(trace, needle, scenario);
    }
    for (const std::string& needle : expected.creates) {
        require_trace_contains(trace, needle, scenario);
    }
    for (const std::string& needle : base) {
        require_trace_contains(trace, needle, scenario);
    }
    for (const std::string& needle : expected.chars) {
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
    if (argc != 4 && argc != 5) {
        std::fprintf(stderr,
                     "uso: runtime_gui_smoke <runtime> <input> <work-dir> [input2]\n");
        return 2;
    }
    const std::string runtime = argv[1];
    const std::string input = argv[2];
    const std::string work_dir = argv[3];
    const std::string input2 = argc == 5 ? argv[4] : std::string{};

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
               RunExpectations{1, {kWinRegisters[0]}, {kWinCreates[0]}, {}});

    const RunOptions close_options{
        .autoclose = false,
        .trigger = Trigger::CloseRequest,
        .expected_exit = 1,
        .trace_path = work_dir + "/trace_close.log",
        .stdout_path = work_dir + "/stdout_close.log",
    };
    run_runtime(runtime, input, display, close_options);
    verify_run(work_dir, "close",
               RunExpectations{1, {kWinRegisters[0]}, {kWinCreates[0]}, {}});

    const RunOptions key_options{
        .autoclose = false,
        .trigger = Trigger::KeyQ,
        .expected_exit = 3,
        .trace_path = work_dir + "/trace_key.log",
        .stdout_path = work_dir + "/stdout_key.log",
    };
    run_runtime(runtime, input, display, key_options);
    verify_run(work_dir, "key",
               RunExpectations{3, {kWinRegisters[0]}, {kWinCreates[0]}, {kWinKeyChar}});

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
            });
    }

    if (g_xvfb_pid > 0) {
        ::kill(g_xvfb_pid, SIGTERM);
        ::waitpid(g_xvfb_pid, nullptr, 0);
    }
    return 0;
}
