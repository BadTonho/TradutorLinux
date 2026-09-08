#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <string>
#include <thread>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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
    if (pid <= 0 || wait_for_exit(pid, 100ms)) {
        return;
    }
    ::kill(pid, SIGKILL);
    (void)::waitpid(pid, nullptr, 0);
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

[[nodiscard]] SmokeResult run_smoke(const std::filesystem::path& runtime,
                                    const std::filesystem::path& target) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path staging = std::filesystem::temp_directory_path() /
                                          ("tradutorlinux-notepadpp-smoke-" +
                                           std::to_string(::getpid()) + "-" +
                                           std::to_string(stamp));
    std::error_code error;
    std::filesystem::create_directories(staging, error);
    if (error) {
        return SmokeResult::Failed;
    }

    const XvfbProcess xvfb = start_xvfb();
    if (xvfb.pid <= 0 || xvfb.display.empty()) {
        std::filesystem::remove_all(staging, error);
        std::cerr << "smoke do Notepad++: Xvfb indisponível; cenário ignorado\n";
        return SmokeResult::Skipped;
    }

    const std::filesystem::path trace_path = staging / "trace.log";
    const int trace_fd = ::open(trace_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    const pid_t runtime_pid = trace_fd < 0 ? -1 : ::fork();
    if (runtime_pid == 0) {
        (void)::setpgid(0, 0);
        (void)::setenv("DISPLAY", xvfb.display.c_str(), 1);
        (void)::chdir(target.parent_path().c_str());
        (void)::dup2(trace_fd, STDERR_FILENO);
        ::close(trace_fd);
        ::execl(runtime.c_str(), runtime.c_str(), "--trace", "--timeout",
                "8", "--cpu", "8", "--memory", "512", target.c_str(),
                static_cast<char*>(nullptr));
        ::_exit(127);
    }
    if (trace_fd >= 0) {
        ::close(trace_fd);
    }
    if (runtime_pid < 0) {
        stop_process(xvfb.pid);
        std::filesystem::remove_all(staging, error);
        return SmokeResult::Failed;
    }
    (void)::setpgid(runtime_pid, runtime_pid);

    Display* const display = ::XOpenDisplay(xvfb.display.c_str());
    Window window = 0;
    if (display != nullptr) {
        for (int attempt = 0; attempt < 80 && window == 0; ++attempt) {
            window = find_window_by_name(display, DefaultRootWindow(display), "Configurator");
            if (window == 0) {
                std::this_thread::sleep_for(100ms);
            }
        }
    }

    bool passed = display != nullptr && window != 0;
    if (passed) {
        passed = send_close(display, window);
    }
    Window resource_error = 0;
    if (passed) {
        for (int attempt = 0; attempt < 30 && resource_error == 0; ++attempt) {
            resource_error = find_window_by_name(display, DefaultRootWindow(display),
                                                 "Load stylers.xml failed");
            if (resource_error == 0) {
                std::this_thread::sleep_for(100ms);
            }
        }
        if (resource_error != 0) {
            passed = send_close(display, resource_error);
        }
    }
    if (display != nullptr) {
        ::XCloseDisplay(display);
    }

    int runtime_status = 0;
    const bool runtime_exited = wait_for_exit(runtime_pid, 8000ms, &runtime_status);
    if (!runtime_exited) {
        stop_runtime_process(runtime_pid);
    }
    passed = passed && runtime_exited && WIFEXITED(runtime_status) &&
             WEXITSTATUS(runtime_status) == 71;
    stop_process(xvfb.pid);

    std::ifstream trace_input(trace_path);
    const std::string trace{std::istreambuf_iterator<char>{trace_input}, {}};
    passed = passed && trace.find("window-created backend=\"x11\" window_id=") !=
                           std::string::npos &&
             trace.find("caption=\"Configurator\"") != std::string::npos &&
             trace.find("window-mapped backend=\"x11\"") != std::string::npos &&
             trace.find("caption=\"Load stylers.xml failed\"") != std::string::npos &&
             trace.find("cxx-throw ignored") != std::string::npos &&
             trace.find("guest-signal") != std::string::npos &&
             trace.find("guest-timeout") == std::string::npos;
    if (!passed) {
        std::cerr << "smoke do Notepad++ não confirmou o bloqueio C++/SEH controlado\n";
        std::cerr << trace;
    }
    std::filesystem::remove_all(staging, error);
    return passed ? SmokeResult::Passed : SmokeResult::Failed;
}

}  // namespace

int main(const int argc, char** const argv) {
    if (argc != 3) {
        std::cerr << "uso: notepadpp_smoke <runtime> <notepad++.exe>\n";
        return 2;
    }
    const std::filesystem::path runtime = argv[1];
    const std::filesystem::path target = argv[2];
    std::error_code error;
    if (!std::filesystem::is_regular_file(runtime, error) || error ||
        !std::filesystem::is_regular_file(target, error) || error) {
        std::cerr << "runtime ou amostra Notepad++ inexistente\n";
        return 2;
    }
    const std::filesystem::path absolute_runtime = std::filesystem::absolute(runtime, error);
    if (error) {
        std::cerr << "não foi possível normalizar o caminho do runtime\n";
        return 2;
    }
    const SmokeResult result = run_smoke(absolute_runtime, target);
    return result == SmokeResult::Passed ? 0 : result == SmokeResult::Skipped ? 77 : 1;
}
