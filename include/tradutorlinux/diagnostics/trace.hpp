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
    Process,
    Gui,
    Crt,
};

enum class FailureCategory {
    ExitProcess,
    Unsupported,
    InvalidImage,
    GuestMemory,
    LinuxError,
    GuestSignal,
    GuestTimeout,
    InternalError,
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

[[nodiscard]] std::string_view failure_category_name(FailureCategory category);

}  // namespace tradutorlinux::diagnostics
