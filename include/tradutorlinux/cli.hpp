#pragma once

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>

namespace tradutorlinux {

enum class ExitCode : int {
    Success = 0,
    Usage = 2,
    InputUnavailable = 3,
    MalformedPe = 4,
    Unsupported = 5,
    InternalError = 70,
    GuestFault = 71,
};

struct CommandLine {
    bool show_help{false};
    bool show_version{false};
    bool trace_enabled{false};
    bool report_only{false};
    std::optional<std::filesystem::path> executable_path;
};

struct ParseResult {
    std::optional<CommandLine> command_line;
    std::string error_message;
};

[[nodiscard]] ParseResult parse_command_line(int argc, const char* const argv[]);
[[nodiscard]] ExitCode run_command(const CommandLine& command_line, std::ostream& stdout_stream,
                                   std::ostream& stderr_stream);
void print_help(std::ostream& stream);

}  // namespace tradutorlinux
