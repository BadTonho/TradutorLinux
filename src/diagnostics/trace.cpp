#include "tradutorlinux/diagnostics/trace.hpp"

#include <array>
#include <ostream>
#include <string>
#include <vector>

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

bool trace_component_from_name(const std::string_view name,
                                 TraceComponent& out) noexcept {
    // case-insensitive como Wine WINEDEBUG
    auto iequals = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    };
    if (iequals(name, "cli")) { out = TraceComponent::Cli; return true; }
    if (iequals(name, "pe")) { out = TraceComponent::Pe; return true; }
    if (iequals(name, "loader")) { out = TraceComponent::Loader; return true; }
    if (iequals(name, "imports")) { out = TraceComponent::Imports; return true; }
    if (iequals(name, "runtime")) { out = TraceComponent::Runtime; return true; }
    if (iequals(name, "process")) { out = TraceComponent::Process; return true; }
    if (iequals(name, "gui")) { out = TraceComponent::Gui; return true; }
    if (iequals(name, "crt")) { out = TraceComponent::Crt; return true; }
    return false;
}

std::string_view trace_component_name(const TraceComponent component) noexcept {
    return component_name(component);
}

namespace {
std::array<bool, 8> g_trace_enabled{};
bool g_trace_filter_active = false;
}  // namespace

void configure_trace_filter(const std::vector<TraceComponent>& filter) noexcept {
    g_trace_enabled.fill(false);
    for (auto c : filter) {
        const auto idx = static_cast<std::size_t>(c);
        if (idx < g_trace_enabled.size()) g_trace_enabled[idx] = true;
    }
    g_trace_filter_active = true;
}

void configure_trace_all() noexcept {
    g_trace_enabled.fill(true);
    g_trace_filter_active = false;
}

bool is_trace_enabled(const TraceComponent component) noexcept {
    if (!g_trace_filter_active) return true;
    const auto idx = static_cast<std::size_t>(component);
    return idx < g_trace_enabled.size() && g_trace_enabled[idx];
}

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
        case FailureCategory::GuestTimeout:
            return "guest-timeout";
        case FailureCategory::InternalError:
            return "internal-error";
    }
    return "unknown";
}

void write_trace(std::ostream& stream, const TraceComponent component, const TraceLevel level,
                 const std::string_view event, const std::span<const TraceField> fields) {
    if (!is_trace_enabled(component)) return;
    stream << "[tl][" << component_name(component) << "][" << level_name(level) << "] " << event;
    for (const TraceField& field : fields) {
        stream << ' ' << field.key << "=\"" << escape_value(field.value) << '"';
    }
    stream << '\n';
}

}  // namespace tradutorlinux::diagnostics
