#pragma once

#include <iosfwd>
#include <span>
#include <string>
#include <string_view>

namespace tradutorlinux::diagnostics {

enum class TraceComponent {
    Cli,
    Pe,
    Loader,
    Imports,
    Runtime,
};

enum class TraceLevel {
    Debug,
    Info,
    Warning,
    Error,
};

struct TraceField {
    std::string key;
    std::string value;
};

void write_trace(std::ostream& stream, TraceComponent component, TraceLevel level,
                 std::string_view event, std::span<const TraceField> fields = {});

}  // namespace tradutorlinux::diagnostics
