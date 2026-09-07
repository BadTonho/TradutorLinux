#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using namespace std::chrono_literals;

struct CommandResult {
    int exit_code{-1};
    std::string stdout_text;
    std::string stderr_text;
    bool timed_out{false};
};

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::string to_guest_path(const std::filesystem::path& path) {
    std::string value = std::filesystem::absolute(path).string();
    for (char& character : value) {
        if (character == '/') character = '\\';
    }
    return "Z:" + value;
}

[[nodiscard]] bool wait_for_exit(const pid_t pid, const std::chrono::milliseconds timeout,
                                 int& status) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t result = ::waitpid(pid, &status, WNOHANG);
        if (result == pid) return true;
        if (result < 0) return false;
        std::this_thread::sleep_for(20ms);
    }
    return false;
}

void stop_process(const pid_t pid) {
    if (pid <= 0) return;
    if (::getpgid(pid) == pid) {
        (void)::kill(-pid, SIGKILL);
    } else {
        (void)::kill(pid, SIGKILL);
    }
    (void)::waitpid(pid, nullptr, 0);
}

[[nodiscard]] CommandResult run_runtime(const std::filesystem::path& runtime,
                                         const std::filesystem::path& executable,
                                         const std::filesystem::path& prefix,
                                         const std::vector<std::string>& guest_arguments,
                                         const std::filesystem::path& output_path,
                                         const std::filesystem::path& error_path) {
    const int output_fd = ::open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    const int error_fd = ::open(error_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (output_fd < 0 || error_fd < 0) {
        if (output_fd >= 0) ::close(output_fd);
        if (error_fd >= 0) ::close(error_fd);
        return {};
    }

    std::vector<std::string> arguments;
    arguments.reserve(5U + guest_arguments.size());
    arguments.emplace_back(runtime.string());
    arguments.emplace_back("--trace=loader,process");
    arguments.emplace_back("--timeout");
    arguments.emplace_back("10");
    arguments.emplace_back(executable.string());
    arguments.insert(arguments.end(), guest_arguments.begin(), guest_arguments.end());

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1U);
    for (std::string& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid == 0) {
        (void)::setpgid(0, 0);
        (void)::dup2(output_fd, STDOUT_FILENO);
        (void)::dup2(error_fd, STDERR_FILENO);
        ::close(output_fd);
        ::close(error_fd);
        (void)::setenv("TL_PREFIX", prefix.c_str(), 1);
        ::execv(runtime.c_str(), argv.data());
        ::_exit(127);
    }
    ::close(output_fd);
    ::close(error_fd);
    if (pid < 0) return {};

    int status = 0;
    CommandResult result;
    if (!wait_for_exit(pid, 15s, status)) {
        result.timed_out = true;
        stop_process(pid);
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }
    result.stdout_text = read_text(output_path);
    result.stderr_text = read_text(error_path);
    return result;
}

[[nodiscard]] bool contains_loader_lifecycle(const CommandResult& result,
                                              const std::string& operation) {
    return !result.timed_out && result.exit_code == 0 &&
           result.stderr_text.find("dll-mapped module=\"7z.dll\"") != std::string::npos &&
           result.stderr_text.find("dll-attach module=\"7z.dll\"") != std::string::npos &&
           result.stderr_text.find("dll-unload module=\"7z.dll\"") != std::string::npos &&
           result.stdout_text.find("Everything is Ok") != std::string::npos &&
           (operation.empty() || result.stdout_text.find(operation) != std::string::npos);
}

[[nodiscard]] bool write_input(const std::filesystem::path& path,
                               const std::string& content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    return output.good();
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "uso: seven_zip_cli_smoke <runtime> <7z_x64.exe>\n";
        return 2;
    }

    const std::filesystem::path runtime = argv[1];
    const std::filesystem::path target = argv[2];
    const std::filesystem::path sibling = target.parent_path() / "7z.dll";
    std::error_code error;
    if (!std::filesystem::is_regular_file(runtime, error) || error ||
        !std::filesystem::is_regular_file(target, error) || error ||
        !std::filesystem::is_regular_file(sibling, error) || error) {
        std::cerr << "runtime, 7z_x64.exe ou 7z.dll inexistente\n";
        return 77;
    }

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path staging = std::filesystem::temp_directory_path() /
                                          ("tradutorlinux-7z cli-smoke-" +
                                           std::to_string(static_cast<unsigned long long>(::getpid())) +
                                           "-" + std::to_string(stamp));
    std::filesystem::remove_all(staging, error);
    if (!std::filesystem::create_directories(staging / "extracted-stored", error) || error ||
        !std::filesystem::create_directories(staging / "extracted-stored-final", error) || error ||
        !std::filesystem::create_directories(staging / "extracted-deflate", error) || error ||
        !std::filesystem::create_directories(staging / "extracted-7z", error) || error) {
        std::cerr << "falha ao criar staging: " << staging << '\n';
        return 1;
    }
    const std::filesystem::path staged_executable = staging / "7z_x64.exe";
    if (!std::filesystem::copy_file(target, staged_executable,
                                    std::filesystem::copy_options::overwrite_existing, error) ||
        error ||
        !std::filesystem::copy_file(sibling, staging / "7z.dll",
                                    std::filesystem::copy_options::overwrite_existing, error) ||
        error) {
        std::cerr << "falha ao copiar o aplicativo para o staging\n";
        std::filesystem::remove_all(staging, error);
        return 1;
    }

    const std::string kPayload = [] {
        std::string value;
        value.reserve(32U * 1024U);
        for (int index = 0; index < 1024; ++index) {
            value += "TradutorLinux 7-Zip CLI compression smoke line ";
            value += std::to_string(index);
            value += "\n";
        }
        return value;
    }();
    const std::string kUnicodePayload = "TradutorLinux Unicode path smoke\n";
    const std::filesystem::path input = staging / "input.txt";
    const std::filesystem::path updated_input = staging / "updated.txt";
    const std::filesystem::path unicode_directory = staging / "input-data";
    const std::filesystem::path unicode_input = unicode_directory / "café-日本.txt";
    const std::filesystem::path unicode_relative = unicode_input.lexically_relative(staging);
    const std::filesystem::path stored_archive = staging / "payload-stored.zip";
    const std::filesystem::path deflate_archive = staging / "payload-deflate.zip";
    const std::filesystem::path seven_zip_archive = staging / "payload.7z";
    const std::string stored_archive_name = stored_archive.filename().string();
    const std::string deflate_archive_name = deflate_archive.filename().string();
    const std::string seven_zip_archive_name = seven_zip_archive.filename().string();
    if (!std::filesystem::create_directories(unicode_directory, error) || error ||
        !write_input(input, kPayload) || !write_input(updated_input, "updated payload\n") ||
        !write_input(unicode_input, kUnicodePayload)) {
        std::cerr << "falha ao criar a entrada do smoke\n";
        std::filesystem::remove_all(staging, error);
        return 1;
    }

    const std::filesystem::path prefix = staging / "prefix";
    const auto create = run_runtime(
        runtime, staged_executable, prefix,
        {"a", "-tzip", "-mm=Store", to_guest_path(stored_archive), to_guest_path(input),
         to_guest_path(unicode_directory)},
        staging / "create.stdout", staging / "create.stderr");
    const auto list = run_runtime(
        runtime, staged_executable, prefix,
        {"l", "-sccUTF-8", stored_archive_name}, staging / "list.stdout",
        staging / "list.stderr");
    const auto extract = run_runtime(
        runtime, staged_executable, prefix,
        {"x", stored_archive_name, "-oextracted-stored", "-y"},
        staging / "extract.stdout", staging / "extract.stderr");
    const auto test_stored = run_runtime(
        runtime, staged_executable, prefix, {"t", stored_archive_name},
        staging / "test-stored.stdout", staging / "test-stored.stderr");
    const auto delete_stored = run_runtime(
        runtime, staged_executable, prefix,
        {"d", "-y", stored_archive_name, "input.txt"},
        staging / "delete-stored.stdout", staging / "delete-stored.stderr");
    const auto update_stored = run_runtime(
        runtime, staged_executable, prefix,
        {"u", "-tzip", "-mm=Deflate", "-mx=1", stored_archive_name, "updated.txt"},
        staging / "update-stored.stdout", staging / "update-stored.stderr");
    const auto list_stored_final = run_runtime(
        runtime, staged_executable, prefix,
        {"l", "-slt", "-sccUTF-8", stored_archive_name},
        staging / "list-stored-final.stdout", staging / "list-stored-final.stderr");
    const auto extract_stored_final = run_runtime(
        runtime, staged_executable, prefix,
        {"x", stored_archive_name, "-oextracted-stored-final", "-y"},
        staging / "extract-stored-final.stdout", staging / "extract-stored-final.stderr");

    const auto create_deflate = run_runtime(
        runtime, staged_executable, prefix,
        {"a", "-tzip", "-mm=Deflate", "-mx=1", to_guest_path(deflate_archive),
         to_guest_path(input), to_guest_path(unicode_directory)},
        staging / "create-deflate.stdout", staging / "create-deflate.stderr");
    const auto list_deflate = run_runtime(
        runtime, staged_executable, prefix,
        {"l", "-slt", "-sccUTF-8", deflate_archive_name},
        staging / "list-deflate.stdout",
        staging / "list-deflate.stderr");
    const auto extract_deflate = run_runtime(
        runtime, staged_executable, prefix,
        {"x", deflate_archive_name, "-oextracted-deflate", "-y"},
        staging / "extract-deflate.stdout", staging / "extract-deflate.stderr");

    const auto create_7z = run_runtime(
        runtime, staged_executable, prefix,
        {"a", "-t7z", "-m0=LZMA2", "-mx=1", to_guest_path(seven_zip_archive),
         to_guest_path(input), to_guest_path(unicode_directory)},
        staging / "create-7z.stdout", staging / "create-7z.stderr");
    const auto list_7z = run_runtime(
        runtime, staged_executable, prefix,
        {"l", "-slt", "-sccUTF-8", seven_zip_archive_name},
        staging / "list-7z.stdout",
        staging / "list-7z.stderr");
    const auto extract_7z = run_runtime(
        runtime, staged_executable, prefix,
        {"x", seven_zip_archive_name, "-oextracted-7z", "-y"},
        staging / "extract-7z.stdout", staging / "extract-7z.stderr");

    const bool extracted_stored =
        read_text(staging / "extracted-stored" / "input.txt") == kPayload;
    const bool extracted_stored_unicode =
        read_text(staging / "extracted-stored" / unicode_relative) ==
        kUnicodePayload;
    const bool extracted_stored_final_unicode =
        read_text(staging / "extracted-stored-final" / unicode_relative) ==
        kUnicodePayload;
    const bool extracted_stored_final_updated =
        read_text(staging / "extracted-stored-final" / "updated.txt") ==
        "updated payload\n";
    const bool removed_stored_input =
        !std::filesystem::exists(staging / "extracted-stored-final" / "input.txt");
    const bool extracted_deflate =
        read_text(staging / "extracted-deflate" / "input.txt") == kPayload;
    const bool extracted_deflate_unicode =
        read_text(staging / "extracted-deflate" / unicode_relative) ==
        kUnicodePayload;
    const bool extracted_7z = read_text(staging / "extracted-7z" / "input.txt") == kPayload;
    const bool extracted_7z_unicode =
        read_text(staging / "extracted-7z" / unicode_relative) ==
        kUnicodePayload;
    const bool ok = contains_loader_lifecycle(create, "Archive size:") &&
                    !list.timed_out && list.exit_code == 0 &&
                    list.stdout_text.find("input.txt") != std::string::npos &&
                    list.stdout_text.find("café-日本.txt") != std::string::npos &&
                    list.stderr_text.find("dll-mapped module=\"7z.dll\"") != std::string::npos &&
                    contains_loader_lifecycle(extract, "Size:") && extracted_stored &&
                    extracted_stored_unicode &&
                    contains_loader_lifecycle(test_stored, "Everything is Ok") &&
                    contains_loader_lifecycle(delete_stored, "Everything is Ok") &&
                    contains_loader_lifecycle(update_stored, "Everything is Ok") &&
                    !list_stored_final.timed_out && list_stored_final.exit_code == 0 &&
                    list_stored_final.stdout_text.find("café-日本.txt") != std::string::npos &&
                    list_stored_final.stdout_text.find("Path = updated.txt") !=
                        std::string::npos &&
                    list_stored_final.stdout_text.find("Path = input.txt") == std::string::npos &&
                    list_stored_final.stderr_text.find("dll-mapped module=\"7z.dll\"") !=
                        std::string::npos &&
                    contains_loader_lifecycle(extract_stored_final, "Everything is Ok") &&
                    extracted_stored_final_unicode && extracted_stored_final_updated &&
                    removed_stored_input &&
                    contains_loader_lifecycle(create_deflate, "Archive size:") &&
                    !list_deflate.timed_out && list_deflate.exit_code == 0 &&
                    list_deflate.stdout_text.find("Method = Deflate") != std::string::npos &&
                    list_deflate.stdout_text.find("café-日本.txt") != std::string::npos &&
                    list_deflate.stderr_text.find("dll-mapped module=\"7z.dll\"") !=
                        std::string::npos &&
                    contains_loader_lifecycle(extract_deflate, "Size:") && extracted_deflate &&
                    extracted_deflate_unicode &&
                    contains_loader_lifecycle(create_7z, "Archive size:") &&
                    !list_7z.timed_out && list_7z.exit_code == 0 &&
                    list_7z.stdout_text.find("Method = LZMA2") != std::string::npos &&
                    list_7z.stdout_text.find("café-日本.txt") != std::string::npos &&
                    list_7z.stderr_text.find("dll-mapped module=\"7z.dll\"") !=
                        std::string::npos &&
                    contains_loader_lifecycle(extract_7z, "Size:") && extracted_7z &&
                    extracted_7z_unicode;
    if (!ok) {
        std::cerr << "smoke do 7-Zip CLI falhou em " << staging << '\n';
        std::cerr << "create exit=" << create.exit_code << " timeout=" << create.timed_out << '\n';
        std::cerr << "list exit=" << list.exit_code << " timeout=" << list.timed_out << '\n';
        std::cerr << "extract exit=" << extract.exit_code << " timeout=" << extract.timed_out
                  << " extracted-stored=" << extracted_stored
                  << " extracted-stored-unicode=" << extracted_stored_unicode << '\n';
        std::cerr << "test-stored exit=" << test_stored.exit_code
                  << " timeout=" << test_stored.timed_out << '\n';
        std::cerr << "delete-stored exit=" << delete_stored.exit_code
                  << " timeout=" << delete_stored.timed_out << '\n';
        std::cerr << "update-stored exit=" << update_stored.exit_code
                  << " timeout=" << update_stored.timed_out << '\n';
        std::cerr << "list-stored-final exit=" << list_stored_final.exit_code
                  << " timeout=" << list_stored_final.timed_out << '\n';
        std::cerr << "extract-stored-final exit=" << extract_stored_final.exit_code
                  << " timeout=" << extract_stored_final.timed_out
                  << " extracted-stored-final-unicode=" << extracted_stored_final_unicode
                  << " extracted-stored-final-updated=" << extracted_stored_final_updated
                  << " removed-stored-input=" << removed_stored_input << '\n';
        std::cerr << "create-deflate exit=" << create_deflate.exit_code
                  << " timeout=" << create_deflate.timed_out << '\n';
        std::cerr << "list-deflate exit=" << list_deflate.exit_code
                  << " timeout=" << list_deflate.timed_out << '\n';
        std::cerr << "extract-deflate exit=" << extract_deflate.exit_code
                  << " timeout=" << extract_deflate.timed_out
                  << " extracted-deflate=" << extracted_deflate
                  << " extracted-deflate-unicode=" << extracted_deflate_unicode << '\n';
        std::cerr << "create-7z exit=" << create_7z.exit_code
                  << " timeout=" << create_7z.timed_out << '\n';
        std::cerr << "list-7z exit=" << list_7z.exit_code << " timeout=" << list_7z.timed_out
                  << '\n';
        std::cerr << "extract-7z exit=" << extract_7z.exit_code
                  << " timeout=" << extract_7z.timed_out << " extracted-7z=" << extracted_7z
                  << " extracted-7z-unicode=" << extracted_7z_unicode << '\n';
        std::cerr << "list-deflate stdout:\n" << list_deflate.stdout_text;
        std::cerr << "list-7z stdout:\n" << list_7z.stdout_text;
        std::filesystem::remove_all(staging, error);
        return 1;
    }

    std::cout << "7-Zip CLI stored test/delete/update/deflate/7z lifecycle: ok\n";
    std::filesystem::remove_all(staging, error);
    return 0;
}
