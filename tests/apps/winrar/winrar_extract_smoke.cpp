#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <string>
#include <thread>
#include <vector>

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

[[nodiscard]] std::string to_windows_z_path(const std::filesystem::path& path) {
    std::string s = "Z:" + path.string();
    for (char& c : s) {
        if (c == '/') {
            c = '\\';
        }
    }
    if (s.empty() || s.back() != '\\') {
        s.push_back('\\');
    }
    return s;
}

[[nodiscard]] SmokeResult run_extraction_smoke(const std::filesystem::path& runtime,
                                              const std::filesystem::path& target) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path staging = std::filesystem::temp_directory_path() /
                                          ("tradutorlinux-winrar-extract-" +
                                           std::to_string(::getpid()) + "-" +
                                           std::to_string(stamp));
    const std::filesystem::path dest = staging / "dest";

    std::error_code error;
    std::filesystem::create_directories(dest, error);
    if (error) {
        std::cerr << "falha ao criar diretorio de staging: " << error.message() << '\n';
        return SmokeResult::Failed;
    }

    const XvfbProcess xvfb = start_xvfb();
    if (xvfb.pid <= 0 || xvfb.display.empty()) {
        std::filesystem::remove_all(staging, error);
        std::cerr << "smoke de extracao do WinRAR: Xvfb indisponivel; cenario ignorado\n";
        return SmokeResult::Skipped;
    }

    const std::string dest_arg = "-d" + to_windows_z_path(dest);
    const std::filesystem::path trace_path = staging / "trace.log";
    const int trace_fd = ::open(trace_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);

    const pid_t runtime_pid = trace_fd < 0 ? -1 : ::fork();
    if (runtime_pid == 0) {
        (void)::setpgid(0, 0);
        (void)::setenv("DISPLAY", xvfb.display.c_str(), 1);
        (void)::dup2(trace_fd, STDERR_FILENO);
        ::close(trace_fd);

        ::execl(runtime.c_str(), runtime.c_str(),
                "--trace=runtime,process,gui",
                "--timeout", "15",
                "--cpu", "10",
                "--memory", "1024",
                target.c_str(),
                "--",
                "-s",
                dest_arg.c_str(),
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

    int runtime_status = 0;
    const bool runtime_exited = wait_for_exit(runtime_pid, 15000ms, &runtime_status);
    if (!runtime_exited) {
        stop_runtime_process(runtime_pid);
    }
    stop_process(xvfb.pid);

    const bool exit_ok = runtime_exited && WIFEXITED(runtime_status) &&
                         WEXITSTATUS(runtime_status) == 0;

    std::ifstream trace_input(trace_path);
    const std::string trace{std::istreambuf_iterator<char>{trace_input}, {}};

    const bool trace_ok = trace.find("ExitProcess symbol=\"ExitProcess\" exit-code=\"0\"") != std::string::npos &&
                          trace.find("guest-timeout") == std::string::npos &&
                          trace.find("guest-signal") == std::string::npos;

    // Verify extracted files
    const std::filesystem::path license_file = dest / "License.txt";
    const std::filesystem::path winrar_exe = dest / "WinRAR.exe";
    const std::filesystem::path rar_exe = dest / "Rar.exe";

    bool files_ok = std::filesystem::is_regular_file(license_file, error) && !error &&
                    std::filesystem::file_size(license_file, error) > 1000 && !error &&
                    std::filesystem::is_regular_file(winrar_exe, error) && !error &&
                    std::filesystem::file_size(winrar_exe, error) > 1000000 && !error &&
                    std::filesystem::is_regular_file(rar_exe, error) && !error &&
                    std::filesystem::file_size(rar_exe, error) > 500000 && !error;

    if (files_ok) {
        std::ifstream license_in(license_file);
        const std::string license_content{std::istreambuf_iterator<char>{license_in}, {}};
        if (license_content.find("END USER LICENSE AGREEMENT") == std::string::npos) {
            files_ok = false;
        }
    }

    const bool passed = exit_ok && trace_ok && files_ok;
    if (!passed) {
        std::cerr << "smoke de extracao do WinRAR falhou:\n"
                  << "  exit_ok: " << (exit_ok ? "true" : "false")
                  << " (status=" << runtime_status << ")\n"
                  << "  trace_ok: " << (trace_ok ? "true" : "false") << '\n'
                  << "  files_ok: " << (files_ok ? "true" : "false") << '\n';
        if (!trace.empty()) {
            std::cerr << "Trace log:\n" << trace << '\n';
        }
    }

    std::filesystem::remove_all(staging, error);
    return passed ? SmokeResult::Passed : SmokeResult::Failed;
}

}  // namespace

int main(const int argc, char** const argv) {
    if (argc != 3) {
        std::cerr << "uso: winrar_extract_smoke <runtime> <WinRAR_x64.exe>\n";
        return 2;
    }
    const std::filesystem::path runtime = argv[1];
    const std::filesystem::path target = argv[2];
    std::error_code error;
    if (!std::filesystem::is_regular_file(runtime, error) || error ||
        !std::filesystem::is_regular_file(target, error) || error) {
        std::cerr << "runtime ou amostra WinRAR inexistente\n";
        return 2;
    }
    const SmokeResult result = run_extraction_smoke(runtime, target);
    return result == SmokeResult::Passed ? 0 : result == SmokeResult::Skipped ? 77 : 1;
}
