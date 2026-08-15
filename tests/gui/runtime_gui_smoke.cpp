// Driver de integração GUI: executa tl_win.exe num Xvfb próprio e valida o
// message loop de ponta a ponta em dois cenários:
//   1. autoclose  (TL_GUI_AUTOCLOSE_MS != 0): WM_QUIT sem interação.
//   2. fechar     (TL_GUI_AUTOCLOSE_MS == 0): envia WM_DELETE_WINDOW via X11,
//      como o botão de fechar de um window manager faria.
// Em ambos exige exit-code 0, stdout vazio e os eventos esperados no trace.

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr const char* kWindowCaption = "Ola do Windows no Linux!";
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

void require_trace_contains(const std::string& trace, const char* const needle,
                            const std::string& scenario) {
    if (trace.find(needle) == std::string::npos) {
        std::fprintf(stderr,
                     "runtime_gui_smoke: '%s' não encontrado no trace (%s):\n%s\n",
                     needle, scenario.c_str(), trace.c_str());
        std::exit(1);
    }
}

pid_t g_xvfb_pid = -1;

std::string ensure_display() {
    const char* const existing = std::getenv("DISPLAY");
    if (existing != nullptr && existing[0] != '\0') {
        return existing;
    }
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
    fail("falha ao iniciar Xvfb");
}

bool send_wm_delete(const std::string& display) {
    Display* const dpy = XOpenDisplay(display.c_str());
    if (dpy == nullptr) {
        return false;
    }
    Window root_return = 0;
    Window parent_return = 0;
    Window* children = nullptr;
    unsigned int child_count = 0;
    if (XQueryTree(dpy, RootWindow(dpy, DefaultScreen(dpy)), &root_return, &parent_return,
                   &children, &child_count) == 0) {
        XCloseDisplay(dpy);
        return false;
    }
    const Atom protocols_atom = XInternAtom(dpy, "WM_PROTOCOLS", False);
    const Atom delete_atom = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    bool found = false;
    for (unsigned int i = 0; i < child_count; ++i) {
        char* name = nullptr;
        if (XFetchName(dpy, children[i], &name) != 0 && name != nullptr &&
            std::strcmp(name, kWindowCaption) == 0) {
            XEvent event{};
            event.xclient.type = ClientMessage;
            event.xclient.display = dpy;
            event.xclient.window = children[i];
            event.xclient.message_type = protocols_atom;
            event.xclient.format = 32;
            event.xclient.data.l[0] = static_cast<long>(delete_atom);
            event.xclient.data.l[1] = static_cast<long>(CurrentTime);
            XSendEvent(dpy, children[i], False, NoEventMask, &event);
            XFlush(dpy);
            found = true;
        }
        if (name != nullptr) {
            XFree(name);
        }
    }
    if (children != nullptr) {
        XFree(children);
    }
    XCloseDisplay(dpy);
    return found;
}

struct RunOptions {
    bool autoclose;
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

    if (!options.autoclose) {
        bool sent = false;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(kReadyTimeoutMs);
        while (!sent && std::chrono::steady_clock::now() < deadline) {
            sent = send_wm_delete(display);
            if (!sent) {
                ::usleep(kPollDelayUs);
            }
        }
        if (!sent) {
            ::kill(runtime_pid, SIGKILL);
            ::waitpid(runtime_pid, nullptr, 0);
            fail("janela não encontrada para enviar WM_DELETE_WINDOW");
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
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fail("runtime não terminou com exit-code 0");
    }
}

void verify_run(const std::string& work_dir, const std::string& scenario,
                const bool autoclose) {
    const std::string trace = read_file(work_dir + "/trace_" + scenario + ".log");
    const std::string output = read_file(work_dir + "/stdout_" + scenario + ".log");
    if (!output.empty()) {
        fail("stdout não vazio (deve ir tudo para stderr) no cenário " + scenario);
    }
    const std::array<const char*, 5> expected = {
        "RegisterClassExA symbol=\"RegisterClassExA\" class=\"tlwin\" atom=\"1\" "
        "status=\"success\"",
        "CreateWindowExA symbol=\"CreateWindowExA\" class=\"tlwin\" "
        "window=\"Ola do Windows no Linux!\" status=\"success\"",
        "GetMessageA symbol=\"GetMessageA\" message=\"WM_QUIT\" exit-code=\"0\" "
        "result=\"quit\"",
        "ExitProcess symbol=\"ExitProcess\" exit-code=\"0\" status=\"success\" "
        "mechanism=\"guest-transfer\"",
        "exit exit-code=\"0\" explicit=\"sim\"",
    };
    for (const char* const needle : expected) {
        require_trace_contains(trace, needle, scenario + (autoclose ? " (autoclose)" : ""));
    }
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "uso: runtime_gui_smoke <runtime> <input> <work-dir>\n");
        return 2;
    }
    const std::string runtime = argv[1];
    const std::string input = argv[2];
    const std::string work_dir = argv[3];

    const std::string display = ensure_display();

    const RunOptions autoclose_options{
        .autoclose = true,
        .trace_path = work_dir + "/trace_autoclose.log",
        .stdout_path = work_dir + "/stdout_autoclose.log",
    };
    run_runtime(runtime, input, display, autoclose_options);
    verify_run(work_dir, "autoclose", true);

    const RunOptions close_options{
        .autoclose = false,
        .trace_path = work_dir + "/trace_close.log",
        .stdout_path = work_dir + "/stdout_close.log",
    };
    run_runtime(runtime, input, display, close_options);
    verify_run(work_dir, "close", false);

    if (g_xvfb_pid > 0) {
        ::kill(g_xvfb_pid, SIGTERM);
        ::waitpid(g_xvfb_pid, nullptr, 0);
    }
    return 0;
}
