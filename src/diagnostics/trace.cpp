#include "tradutorlinux/diagnostics/trace.hpp"

#include <ostream>
#include <string>

namespace tradutorlinux::diagnostics {
namespace {

[[nodiscard]] std::string_view component_name(const TraceComponent component) {
    switch (component) {
        case TraceComponent::Cli:
            return "cli";
        case TraceComponent::Pe:
            return "pe";
        case TraceComponent::Loader:
            return "loader";
        case TraceComponent::Imports:
            return "imports";
        case TraceComponent::Runtime:
            return "runtime";
        case TraceComponent::Process:
            return "process";
        case TraceComponent::Gui:
            return "gui";
        case TraceComponent::Crt:
            return "crt";
    }

    return "unknown";
}

[[nodiscard]] std::string_view level_name(const TraceLevel level) {
    switch (level) {
        case TraceLevel::Debug:
            return "debug";
        case TraceLevel::Info:
            return "info";
        case TraceLevel::Warning:
            return "warning";
        case TraceLevel::Error:
            return "error";
    }

    return "unknown";
}

[[nodiscard]] std::string escape_value(const std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (const char character : value) {
        switch (character) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += character;
                break;
        }
    }

    return escaped;
}

}  // namespace

std::string_view failure_category_name(const FailureCategory category) {
    switch (category) {
        case FailureCategory::ExitProcess:
            return "exit-process";
        case FailureCategory::Unsupported:
            return "unsupported";
        case FailureCategory::InvalidImage:
            return "invalid-image";
        case FailureCategory::GuestMemory:
            return "guest-memory";
        case FailureCategory::LinuxError:
            return "linux-error";
        case FailureCategory::GuestSignal:
            return "guest-signal";
        case FailureCategory::InternalError:
            return "internal-error";
    }
    return "unknown";
}

void write_trace(std::ostream& stream, const TraceComponent component, const TraceLevel level,
                 const std::string_view event, const std::span<const TraceField> fields) {
    stream << "[tl][" << component_name(component) << "][" << level_name(level) << "] " << event;
    for (const TraceField& field : fields) {
        stream << ' ' << field.key << "=\"" << escape_value(field.value) << '"';
    }
    stream << '\n';
}

}  // namespace tradutorlinux::diagnostics
