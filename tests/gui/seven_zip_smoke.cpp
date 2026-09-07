#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>

namespace {

using namespace std::chrono_literals;

struct XvfbProcess {
    pid_t pid{-1};
    std::string display;
};

enum class SmokeResult { Passed, Failed, Skipped };

[[nodiscard]] bool wait_for_exit(const pid_t pid, const std::chrono::milliseconds timeout,
                                  int* const exit_status = nullptr) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        if (::waitpid(pid, &status, WNOHANG) == pid) {
            if (exit_status != nullptr) {
                *exit_status = status;
            }
            return true;
        }
        std::this_thread::sleep_for(20ms);
    }
    return false;
}

void stop_process(const pid_t pid) {
    if (pid <= 0) {
        return;
    }
    if (wait_for_exit(pid, 100ms)) {
        return;
    }
    ::kill(pid, SIGKILL);
    (void)::waitpid(pid, nullptr, 0);
}

[[nodiscard]] XvfbProcess start_xvfb() {
    int display_pipe[2]{};
    if (::pipe(display_pipe) != 0) {
        return {};
    }
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::close(display_pipe[0]);
        const int null_fd = ::open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            (void)::dup2(null_fd, STDERR_FILENO);
            ::close(null_fd);
        }
        if (::dup2(display_pipe[1], 3) < 0) {
            ::_exit(127);
        }
        ::close(display_pipe[1]);
        ::execlp("Xvfb", "Xvfb", "-displayfd", "3", "-screen", "0", "800x600x24",
                 "-nolisten", "tcp", static_cast<char*>(nullptr));
        ::_exit(127);
    }
    ::close(display_pipe[1]);
    if (pid < 0) {
        ::close(display_pipe[0]);
        return {};
    }

    std::string display_number;
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline && display_number.empty()) {
        struct pollfd descriptor{display_pipe[0], POLLIN | POLLHUP, 0};
        if (::poll(&descriptor, 1, 100) <= 0) {
            continue;
        }
        char character = '\0';
        const ::ssize_t count = ::read(display_pipe[0], &character, 1);
        if (count == 1 && character != '\n') {
            display_number.push_back(character);
        } else if (count <= 0) {
            break;
        }
    }
    ::close(display_pipe[0]);
    if (display_number.empty()) {
        stop_process(pid);
        return {};
    }
    return XvfbProcess{pid, ":" + display_number};
}

void stop_runtime_process(const pid_t pid) {
    if (pid <= 0 || wait_for_exit(pid, 100ms)) {
        return;
    }
    if (::getpgid(pid) == pid) {
        ::kill(-pid, SIGKILL);
    } else {
        ::kill(pid, SIGKILL);
    }
    (void)::waitpid(pid, nullptr, 0);
}

[[nodiscard]] Window find_window_by_name(Display* const display, const Window root,
                                          const std::string& name) {
    char* window_name = nullptr;
    if (::XFetchName(display, root, &window_name) != 0 && window_name != nullptr) {
        const bool matches = name == window_name;
        ::XFree(window_name);
        if (matches) {
            return root;
        }
    }

    Window returned_root = 0;
    Window returned_parent = 0;
    Window* children = nullptr;
    unsigned int child_count = 0;
    if (::XQueryTree(display, root, &returned_root, &returned_parent, &children, &child_count) == 0) {
        return 0;
    }
    Window result = 0;
    for (unsigned int index = 0; index < child_count && result == 0; ++index) {
        result = find_window_by_name(display, children[index], name);
    }
    if (children != nullptr) {
        ::XFree(children);
    }
    return result;
}

[[nodiscard]] bool send_button(Display* const display, const Window window, const int type,
                                const int x, const int y) {
    XEvent event{};
    event.xbutton.type = type;
    event.xbutton.display = display;
    event.xbutton.window = window;
    event.xbutton.root = DefaultRootWindow(display);
    event.xbutton.subwindow = 0;
    event.xbutton.time = CurrentTime;
    event.xbutton.x = x;
    event.xbutton.y = y;
    event.xbutton.x_root = x;
    event.xbutton.y_root = y;
    event.xbutton.state = 0;
    event.xbutton.button = Button1;
    event.xbutton.same_screen = True;
    const long mask = type == ButtonPress ? ButtonPressMask : ButtonReleaseMask;
    const bool sent = ::XSendEvent(display, window, False, mask, &event) != 0;
    (void)::XFlush(display);
    return sent;
}

[[nodiscard]] bool send_close(Display* const display, const Window window) {
    const Atom protocols = ::XInternAtom(display, "WM_PROTOCOLS", False);
    const Atom delete_window = ::XInternAtom(display, "WM_DELETE_WINDOW", False);
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.display = display;
    event.xclient.window = window;
    event.xclient.message_type = protocols;
    event.xclient.format = 32;
    event.xclient.data.l[0] = static_cast<long>(delete_window);
    event.xclient.data.l[1] = CurrentTime;
    const bool sent = ::XSendEvent(display, window, False, NoEventMask, &event) != 0;
    (void)::XFlush(display);
    return sent;
}

[[nodiscard]] bool copy_runtime_sample(const std::filesystem::path& target,
                                        const std::filesystem::path& staging) {
    std::error_code error;
    std::filesystem::create_directories(staging / "output", error);
    if (error) {
        return false;
    }
    if (!std::filesystem::copy_file(target, staging / "7zFM.exe",
                                    std::filesystem::copy_options::overwrite_existing, error) ||
        error) {
        return false;
    }
    const std::filesystem::path sibling = target.parent_path() / "7z.dll";
    if (!std::filesystem::is_regular_file(sibling, error) || error ||
        !std::filesystem::copy_file(sibling, staging / "7z.dll",
                                    std::filesystem::copy_options::overwrite_existing, error) ||
        error) {
        return false;
    }
    std::ofstream input(staging / "input.txt");
    input << "7-Zip external smoke\n";
    return input.good();
}

[[nodiscard]] SmokeResult run_smoke(const std::filesystem::path& runtime,
                                    const std::filesystem::path& target) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path staging = std::filesystem::temp_directory_path() /
                                           ("tradutorlinux-7zfm-smoke-" +
                                            std::to_string(::getpid()) + "-" +
                                            std::to_string(stamp));
    std::error_code error;
    if (!copy_runtime_sample(target, staging)) {
        std::cerr << "falha ao preparar a amostra 7-Zip em " << staging << '\n';
        std::filesystem::remove_all(staging, error);
        return SmokeResult::Failed;
    }

    const XvfbProcess xvfb = start_xvfb();
    if (xvfb.pid <= 0 || xvfb.display.empty()) {
        std::cerr << "falha ao iniciar Xvfb\n";
        std::filesystem::remove_all(staging, error);
        std::cerr << "smoke do 7-Zip GUI ignorado: Xvfb indisponível\n";
        return SmokeResult::Skipped;
    }

    const std::filesystem::path trace_path = staging / "trace.log";
    const int trace_fd = ::open(trace_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    const int output_fd = ::open((staging / "stdout.log").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    const pid_t runtime_pid = trace_fd < 0 || output_fd < 0 ? -1 : ::fork();
    if (runtime_pid == 0) {
        (void)::setpgid(0, 0);
        (void)::setenv("DISPLAY", xvfb.display.c_str(), 1);
        (void)::setenv("TL_7ZFM_COPY_DESTINATION", (staging / "output").c_str(), 1);
        (void)::dup2(trace_fd, STDERR_FILENO);
        (void)::dup2(output_fd, STDOUT_FILENO);
        ::close(trace_fd);
        ::close(output_fd);
        ::execl(runtime.c_str(), runtime.c_str(), "--trace=runtime,gui,process",
                (staging / "7zFM.exe").c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }
    if (trace_fd >= 0) {
        ::close(trace_fd);
    }
    if (output_fd >= 0) {
        ::close(output_fd);
    }
    if (runtime_pid < 0) {
        stop_process(xvfb.pid);
        std::cerr << "falha ao iniciar o runtime\n";
        std::filesystem::remove_all(staging, error);
        return SmokeResult::Failed;
    }
    (void)::setpgid(runtime_pid, runtime_pid);

    Display* const display = ::XOpenDisplay(xvfb.display.c_str());
    Window window = 0;
    if (display != nullptr) {
        for (int attempt = 0; attempt < 100 && window == 0; ++attempt) {
            window = find_window_by_name(display, DefaultRootWindow(display), "7-Zip");
            if (window == 0) {
                std::this_thread::sleep_for(100ms);
            }
        }
    }
    bool passed = display != nullptr && window != 0;
    if (passed) {
        // A janela é criada antes de o 7-Zip terminar de montar os controles.
        std::this_thread::sleep_for(2000ms);
        // output/ is row 1; the three files are sorted after it, making input.txt row 4.
        passed = send_button(display, window, ButtonPress, 260, 235);
        passed = passed && send_button(display, window, ButtonRelease, 260, 235);
        std::this_thread::sleep_for(100ms);
        // Add, Extract and Test precede Copy in the real toolbar command order.
        passed = passed && send_button(display, window, ButtonPress, 264, 50);
        passed = passed && send_button(display, window, ButtonRelease, 264, 50);
    }

    const std::filesystem::path copied = staging / "output" / "input.txt";
    for (int attempt = 0; passed && attempt < 50 && !std::filesystem::is_regular_file(copied, error);
         ++attempt) {
        std::this_thread::sleep_for(100ms);
    }
    std::ifstream copied_input(copied);
    const std::string copied_contents{std::istreambuf_iterator<char>{copied_input}, {}};
    passed = passed && copied_contents == "7-Zip external smoke\n";
    if (display != nullptr && window != 0) {
        (void)send_close(display, window);
        ::XCloseDisplay(display);
    }
    int runtime_status = 0;
    const bool runtime_exited = wait_for_exit(runtime_pid, 3000ms, &runtime_status);
    if (!runtime_exited) {
        stop_runtime_process(runtime_pid);
    }
    passed = passed && runtime_exited && WIFEXITED(runtime_status) && WEXITSTATUS(runtime_status) == 0;
    stop_process(xvfb.pid);

    std::ifstream trace_input(trace_path);
    const std::string trace{std::istreambuf_iterator<char>{trace_input}, {}};
    passed = passed && trace.find("SevenZipOperation operation=\"copy\" status=\"success\"") !=
                        std::string::npos &&
             trace.find("source-name=\"input.txt\"") != std::string::npos;
    if (!passed) {
        std::cerr << "smoke do 7-Zip não confirmou cópia e trace; window=" << window
                  << " copied=" << std::filesystem::is_regular_file(copied, error) << '\n';
        const std::size_t operation = trace.find("SevenZipOperation");
        if (operation != std::string::npos) {
            const std::size_t line_start = trace.rfind('\n', operation);
            const std::size_t line_end = trace.find('\n', operation);
            std::cerr << trace.substr(line_start == std::string::npos ? 0 : line_start + 1,
                                      line_end == std::string::npos ? std::string::npos
                                                                    : line_end - line_start - 1)
                      << '\n';
        }
    }
    std::filesystem::remove_all(staging, error);
    return passed ? SmokeResult::Passed : SmokeResult::Failed;
}

}  // namespace

int main(const int argc, char** const argv) {
    if (argc != 3) {
        std::cerr << "uso: seven_zip_smoke <runtime> <7zFM.exe>\n";
        return 2;
    }
    const std::filesystem::path runtime = argv[1];
    const std::filesystem::path target = argv[2];
    std::error_code error;
    if (!std::filesystem::is_regular_file(runtime, error) || error ||
        !std::filesystem::is_regular_file(target, error) || error) {
        std::cerr << "runtime ou amostra 7-Zip inexistente\n";
        return 2;
    }
    const SmokeResult result = run_smoke(runtime, target);
    return result == SmokeResult::Passed ? 0 : result == SmokeResult::Skipped ? 77 : 1;
}
