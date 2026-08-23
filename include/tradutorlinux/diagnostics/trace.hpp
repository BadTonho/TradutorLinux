#pragma once

#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
    Install,
};

[[nodiscard]] bool trace_component_from_name(std::string_view name,
                                            TraceComponent& out) noexcept;
[[nodiscard]] std::string_view trace_component_name(TraceComponent component) noexcept;
void configure_trace_filter(const std::vector<TraceComponent>& filter) noexcept;
void configure_trace_all() noexcept;
[[nodiscard]] bool is_trace_enabled(TraceComponent component) noexcept;

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
