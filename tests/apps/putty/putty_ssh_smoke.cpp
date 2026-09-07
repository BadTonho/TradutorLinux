#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using namespace std::chrono_literals;

struct XvfbProcess {
    pid_t pid{-1};
    std::string display;
};

struct ServerProcess {
    pid_t pid{-1};
    int status_fd{-1};
    std::uint16_t port{0};
};

struct RuntimeResult {
    int exit_code{-1};
    bool exited{false};
    bool timed_out{false};
    std::string trace;
    std::string stdout_text;
};

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] bool wait_for_exit(const pid_t pid, const std::chrono::milliseconds timeout,
                                 int* const status = nullptr) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int local_status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t result = ::waitpid(pid, &local_status, WNOHANG);
        if (result == pid) {
            if (status != nullptr) *status = local_status;
            return true;
        }
        if (result < 0) return false;
        std::this_thread::sleep_for(20ms);
    }
    return false;
}

void stop_process(const pid_t pid) {
    if (pid <= 0) return;
    int status = 0;
    if (::waitpid(pid, &status, WNOHANG) == pid) return;
    if (::getpgid(pid) == pid) {
        (void)::kill(-pid, SIGKILL);
    } else {
        (void)::kill(pid, SIGKILL);
    }
    (void)::waitpid(pid, nullptr, 0);
}

[[nodiscard]] XvfbProcess start_xvfb() {
    int display_pipe[2]{};
    if (::pipe(display_pipe) != 0) return {};

    const pid_t pid = ::fork();
    if (pid == 0) {
        ::close(display_pipe[0]);
        const int null_fd = ::open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            (void)::dup2(null_fd, STDERR_FILENO);
            ::close(null_fd);
        }
        if (::dup2(display_pipe[1], 3) < 0) ::_exit(127);
        ::close(display_pipe[1]);
        ::execlp("Xvfb", "Xvfb", "-displayfd", "3", "-screen", "0", "1024x768x24",
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
        if (::poll(&descriptor, 1, 100) <= 0) continue;
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
        if (matches) return root;
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
    if (children != nullptr) ::XFree(children);
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

[[nodiscard]] bool send_key_event(Display* const display, const Window window,
                                   const KeySym key, const int type,
                                   const unsigned int state = 0) {
    const KeyCode keycode = ::XKeysymToKeycode(display, key);
    if (keycode == 0) return false;
    XEvent event{};
    event.xkey.type = type;
    event.xkey.display = display;
    event.xkey.window = window;
    event.xkey.root = DefaultRootWindow(display);
    event.xkey.subwindow = 0;
    event.xkey.time = CurrentTime;
    event.xkey.x = 10;
    event.xkey.y = 10;
    event.xkey.x_root = 10;
    event.xkey.y_root = 10;
    event.xkey.state = state;
    event.xkey.keycode = keycode;
    event.xkey.same_screen = True;
    const long mask = type == KeyPress ? KeyPressMask : KeyReleaseMask;
    const bool sent = ::XSendEvent(display, window, False, mask, &event) != 0;
    (void)::XFlush(display);
    return sent;
}

[[nodiscard]] bool send_key_pair(Display* const display, const Window window,
                                  const KeySym key) {
    return send_key_event(display, window, key, KeyPress) &&
           send_key_event(display, window, key, KeyRelease);
}

[[nodiscard]] bool send_ascii(Display* const display, const Window window,
                              const std::string_view text) {
    for (const char raw_character : text) {
        const unsigned char character = static_cast<unsigned char>(raw_character);
        KeySym key = 0;
        if (character >= 'a' && character <= 'z') {
            key = static_cast<KeySym>(XK_a + (character - 'a'));
        } else if (character >= '0' && character <= '9') {
            key = static_cast<KeySym>(XK_0 + (character - '0'));
        } else if (character == '.') {
            key = XK_period;
        } else {
            return false;
        }
        if (!send_key_pair(display, window, key)) return false;
    }
    return true;
}

[[nodiscard]] bool send_backspaces(Display* const display, const Window window,
                                   const int count) {
    for (int index = 0; index < count; ++index) {
        if (!send_key_pair(display, window, XK_BackSpace)) return false;
    }
    return true;
}

[[nodiscard]] bool configure_session(Display* const display, const Window configuration,
                                     const std::uint16_t port) {
    ::XSetInputFocus(display, configuration, RevertToParent, CurrentTime);
    (void)::XSync(display, False);
    if (!send_backspaces(display, configuration, 16) ||
        !send_ascii(display, configuration, "127.0.0.1") ||
        !send_key_pair(display, configuration, XK_Tab) ||
        !send_backspaces(display, configuration, 16) ||
        !send_ascii(display, configuration, std::to_string(port)) ||
        !send_key_pair(display, configuration, XK_Return)) {
        return false;
    }
    (void)::XSync(display, False);
    return true;
}

void collect_windows(Display* const display, const Window root,
                     std::vector<std::pair<Window, std::string>>& windows) {
    char* window_name = nullptr;
    if (::XFetchName(display, root, &window_name) != 0 && window_name != nullptr) {
        windows.emplace_back(root, window_name);
        ::XFree(window_name);
    }

    Window returned_root = 0;
    Window returned_parent = 0;
    Window* children = nullptr;
    unsigned int child_count = 0;
    if (::XQueryTree(display, root, &returned_root, &returned_parent, &children, &child_count) == 0) {
        return;
    }
    for (unsigned int index = 0; index < child_count; ++index) {
        collect_windows(display, children[index], windows);
    }
    if (children != nullptr) ::XFree(children);
}

void close_session_windows(Display* const display) {
    std::vector<std::pair<Window, std::string>> windows;
    collect_windows(display, DefaultRootWindow(display), windows);
    for (const auto& [window, name] : windows) {
        if (name == "PuTTY" || name.starts_with("PuTTY:") ||
            name.find("PuTTY Security Alert") != std::string::npos ||
            name.find("PuTTY Fatal Error") != std::string::npos) {
            (void)send_close(display, window);
        }
    }
}

[[nodiscard]] ServerProcess start_server(int& listener_out) {
    listener_out = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener_out < 0) return {};
    int reuse = 1;
    (void)::setsockopt(listener_out, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listener_out, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listener_out, 1) != 0) {
        ::close(listener_out);
        listener_out = -1;
        return {};
    }

    socklen_t address_length = sizeof(address);
    if (::getsockname(listener_out, reinterpret_cast<sockaddr*>(&address), &address_length) != 0) {
        ::close(listener_out);
        listener_out = -1;
        return {};
    }

    int status_pipe[2]{};
    if (::pipe2(status_pipe, O_CLOEXEC) != 0) {
        ::close(listener_out);
        listener_out = -1;
        return {};
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(status_pipe[0]);
        ::close(status_pipe[1]);
        ::close(listener_out);
        listener_out = -1;
        return {};
    }
    if (pid == 0) {
        ::close(status_pipe[0]);
        struct pollfd listener_descriptor{listener_out, POLLIN, 0};
        const int ready = ::poll(&listener_descriptor, 1, 10000);
        bool valid_banner = false;
        if (ready > 0 && (listener_descriptor.revents & POLLIN) != 0) {
            const int client = ::accept(listener_out, nullptr, nullptr);
            if (client >= 0) {
                char buffer[256]{};
                std::size_t used = 0;
                const auto deadline = std::chrono::steady_clock::now() + 3s;
                while (used < sizeof(buffer) - 1U &&
                       std::chrono::steady_clock::now() < deadline) {
                    struct pollfd client_descriptor{client, POLLIN, 0};
                    if (::poll(&client_descriptor, 1, 100) <= 0) continue;
                    const ::ssize_t count = ::recv(client, buffer + used,
                                                   sizeof(buffer) - 1U - used, 0);
                    if (count <= 0) break;
                    used += static_cast<std::size_t>(count);
                    buffer[used] = '\0';
                    if (std::string_view{buffer, used}.find('\n') != std::string_view::npos) {
                        break;
                    }
                }
                const std::string_view received{buffer, used};
                valid_banner = received.starts_with("SSH-") &&
                               received.find("\r\n") != std::string_view::npos;
                constexpr std::string_view response = "SSH-2.0-TLProbe_1.0\r\n";
                (void)::send(client, response.data(), response.size(), MSG_NOSIGNAL);
                ::close(client);
            }
        }
        const unsigned char result = valid_banner ? 1U : 0U;
        (void)::write(status_pipe[1], &result, sizeof(result));
        ::close(status_pipe[1]);
        ::close(listener_out);
        ::_exit(valid_banner ? 0 : 1);
    }

    ::close(status_pipe[1]);
    ::close(listener_out);
    listener_out = -1;
    return ServerProcess{pid, status_pipe[0], ntohs(address.sin_port)};
}

[[nodiscard]] pid_t start_runtime(const std::filesystem::path& runtime,
                                  const std::filesystem::path& target,
                                  const std::filesystem::path& prefix,
                                  const std::string& display,
                                  const std::uint16_t port,
                                  const std::filesystem::path& trace_path,
                                  const std::filesystem::path& stdout_path) {
    const int trace_fd = ::open(trace_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    const int stdout_fd = ::open(stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (trace_fd < 0 || stdout_fd < 0) {
        if (trace_fd >= 0) ::close(trace_fd);
        if (stdout_fd >= 0) ::close(stdout_fd);
        return -1;
    }

    (void)port;
    std::vector<std::string> arguments{
        runtime.string(), "--trace=loader,process,runtime,gui", "--timeout", "8", "--cpu",
        "10", "--memory", "512", target.string()};
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1U);
    for (std::string& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid == 0) {
        (void)::setpgid(0, 0);
        (void)::setenv("DISPLAY", display.c_str(), 1);
        (void)::setenv("TL_PREFIX", prefix.c_str(), 1);
        (void)::setenv("APPDATA", (prefix / "appdata").c_str(), 1);
        (void)::dup2(trace_fd, STDERR_FILENO);
        (void)::dup2(stdout_fd, STDOUT_FILENO);
        ::close(trace_fd);
        ::close(stdout_fd);
        ::execv(runtime.c_str(), argv.data());
        ::_exit(127);
    }
    ::close(trace_fd);
    ::close(stdout_fd);
    if (pid > 0) (void)::setpgid(pid, pid);
    return pid;
}

[[nodiscard]] RuntimeResult collect_runtime(const pid_t pid,
                                            const std::filesystem::path& trace_path,
                                            const std::filesystem::path& stdout_path) {
    RuntimeResult result;
    if (pid <= 0) return result;
    int status = 0;
    if (!wait_for_exit(pid, 8000ms, &status)) {
        result.timed_out = true;
        stop_process(pid);
    } else if (WIFEXITED(status)) {
        result.exited = true;
        result.exit_code = WEXITSTATUS(status);
    }
    result.trace = read_text(trace_path);
    result.stdout_text = read_text(stdout_path);
    return result;
}

[[nodiscard]] bool has_controlled_exit(const RuntimeResult& result) {
    return result.exited && !result.timed_out &&
           result.trace.find("ExitProcess symbol=\"ExitProcess\"") != std::string::npos &&
           result.trace.find("guest-timeout") == std::string::npos &&
           result.trace.find("guest-signal") == std::string::npos;
}

[[nodiscard]] bool take_server_result(ServerProcess& server, const int timeout_ms,
                                       bool* const valid) {
    if (valid == nullptr || server.status_fd < 0) return false;
    struct pollfd descriptor{server.status_fd, POLLIN | POLLHUP, 0};
    if (::poll(&descriptor, 1, timeout_ms) <= 0) return false;
    unsigned char result = 0;
    const bool read_ok = ::read(server.status_fd, &result, sizeof(result)) ==
                         static_cast<ssize_t>(sizeof(result));
    ::close(server.status_fd);
    server.status_fd = -1;
    if (!read_ok) return false;
    *valid = result == 1U;
    return true;
}

}  // namespace

int main(const int argc, char** const argv) {
    if (argc != 3) {
        std::cerr << "uso: putty_ssh_smoke <runtime> <putty.exe>\n";
        return 2;
    }

    const std::filesystem::path runtime = argv[1];
    const std::filesystem::path target = argv[2];
    std::error_code error;
    if (!std::filesystem::is_regular_file(runtime, error) || error ||
        !std::filesystem::is_regular_file(target, error) || error) {
        std::cerr << "runtime ou amostra PuTTY inexistente\n";
        return 2;
    }

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path staging = std::filesystem::temp_directory_path() /
                                          ("tradutorlinux-putty-ssh-smoke-" +
                                           std::to_string(static_cast<unsigned long long>(::getpid())) +
                                           "-" + std::to_string(stamp));
    if (!std::filesystem::create_directories(staging / "prefix" / "appdata", error) || error) {
        std::cerr << "falha ao criar staging do PuTTY: " << staging << '\n';
        return 1;
    }

    const XvfbProcess xvfb = start_xvfb();
    if (xvfb.pid <= 0 || xvfb.display.empty()) {
        std::filesystem::remove_all(staging, error);
        std::cerr << "smoke SSH do PuTTY: Xvfb indisponível; cenário ignorado\n";
        return 77;
    }

    int listener = -1;
    ServerProcess server = start_server(listener);
    if (listener >= 0) ::close(listener);
    if (server.pid <= 0 || server.status_fd < 0 || server.port == 0) {
        stop_process(xvfb.pid);
        std::filesystem::remove_all(staging, error);
        std::cerr << "falha ao iniciar servidor TCP local\n";
        return 1;
    }

    const std::filesystem::path trace_path = staging / "trace.log";
    const std::filesystem::path stdout_path = staging / "stdout.log";
    const pid_t runtime_pid = start_runtime(runtime, target, staging / "prefix", xvfb.display,
                                             server.port, trace_path, stdout_path);
    if (runtime_pid <= 0) {
        stop_process(server.pid);
        if (server.status_fd >= 0) ::close(server.status_fd);
        stop_process(xvfb.pid);
        std::filesystem::remove_all(staging, error);
        std::cerr << "falha ao iniciar runtime\n";
        return 1;
    }

    Display* const display = ::XOpenDisplay(xvfb.display.c_str());
    Window about = 0;
    const bool display_open = display != nullptr;
    bool about_found = false;
    bool about_closed = false;
    Window configuration = 0;
    if (display_open) {
        for (int attempt = 0; attempt < 100 && configuration == 0; ++attempt) {
            if (about == 0) {
                about = find_window_by_name(display, DefaultRootWindow(display), "About PuTTY");
                if (about != 0) {
                    about_found = true;
                    about_closed = send_close(display, about);
                }
            }
            configuration = find_window_by_name(display, DefaultRootWindow(display),
                                                "PuTTY Configuration");
            if (configuration == 0) std::this_thread::sleep_for(100ms);
        }
    }
    const bool configured = display_open && configuration != 0 &&
                            configure_session(display, configuration, server.port);

    bool banner_received = false;
    bool server_result_available = false;
    if (configured) {
        for (int attempt = 0; attempt < 100 && !server_result_available; ++attempt) {
            server_result_available = take_server_result(server, 100, &banner_received);
            if (server_result_available) break;
            std::this_thread::sleep_for(50ms);
        }
        if (banner_received) close_session_windows(display);
    }
    if (display != nullptr) ::XCloseDisplay(display);

    const RuntimeResult runtime_result = collect_runtime(runtime_pid, trace_path, stdout_path);
    if (!server_result_available) {
        server_result_available = take_server_result(server, 100, &banner_received);
    }
    int server_status = 0;
    const bool server_exited = wait_for_exit(server.pid, 3000ms, &server_status);
    if (!server_exited) stop_process(server.pid);
    stop_process(xvfb.pid);

    const bool successful_exchange = configured && banner_received && server_result_available &&
                    server_exited &&
                    WIFEXITED(server_status) && WEXITSTATUS(server_status) == 0 &&
                    has_controlled_exit(runtime_result) && runtime_result.stdout_text.empty();
    const bool controlled_limitation = configured && !banner_received && server_result_available &&
                                       server_exited && WIFEXITED(server_status) &&
                                       WEXITSTATUS(server_status) == 1 && runtime_result.exited &&
                                       runtime_result.exit_code == 72 &&
                                       runtime_result.trace.find("category=\"guest-timeout\"") !=
                                           std::string::npos &&
                                       runtime_result.trace.find("guest-signal") == std::string::npos &&
                                       runtime_result.stdout_text.empty();
    const bool ok = successful_exchange || controlled_limitation;
    if (!ok) {
        std::cerr << "smoke SSH local do PuTTY falhou em " << staging << '\n'
                  << "display-open=" << display_open << " about-found=" << about_found
                  << " about-closed=" << about_closed
                  << " configuration=" << (configuration != 0)
                  << " configured=" << configured << " banner=" << banner_received
                  << " server-exited=" << server_exited << " runtime-exited="
                  << runtime_result.exited << " runtime-timeout=" << runtime_result.timed_out
                  << " runtime-exit=" << runtime_result.exit_code << '\n';
        const std::size_t start = runtime_result.trace.size() > 24000U
                                      ? runtime_result.trace.size() - 24000U
                                      : 0U;
        std::cerr << runtime_result.trace.substr(start);
        std::filesystem::remove_all(staging, error);
        return 1;
    }

    if (successful_exchange) {
        std::cout << "PuTTY SSH local version exchange and controlled termination: ok\n";
    } else {
        std::cout << "PuTTY SSH local probe: configuration reached, no bytes sent, "
                     "guest-timeout 72 (limitation recorded)\n";
    }
    std::filesystem::remove_all(staging, error);
    return 0;
}
