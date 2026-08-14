#include "tradutorlinux/cli.hpp"

#include <cstddef>
#include <iostream>
#include <vector>

int main(int argc, char* argv[]) {
    std::vector<const char*> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.push_back(argv[index]);
    }

    const auto result = tradutorlinux::parse_command_line(argc, arguments.data());
    if (!result.command_line.has_value()) {
        std::cerr << "erro: " << result.error_message << '\n';
        tradutorlinux::print_help(std::cerr);
        return static_cast<int>(tradutorlinux::ExitCode::Usage);
    }

    return static_cast<int>(tradutorlinux::run_command(*result.command_line, std::cout, std::cerr));
}
