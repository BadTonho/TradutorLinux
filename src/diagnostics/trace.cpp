#include "tradutorlinux/diagnostics/trace.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <dlfcn.h>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <thread>

namespace tradutorlinux::diagnostics {

extern "C" volatile unsigned char tl_function_trace_enabled;

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
        case TraceComponent::Install:
            return "install";
        case TraceComponent::Proton:
            return "proton";
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

[[nodiscard]] bool is_project_function(const char* const symbol) noexcept {
    if (symbol == nullptr) return false;
    const std::string_view name{symbol};
    if (name.starts_with("_ZNSt") || name.starts_with("_ZSt") ||
        name.starts_with("_ZNS") || name.starts_with("_ZNKSt") ||
        name.starts_with("_ZN9__gnu_cxx") || name.starts_with("__gnu_cxx")) return false;
    if (name == "__cyg_profile_func_enter" || name == "__cyg_profile_func_exit" ||
        name == "trace_assembly_function_entry") return true;
    return name.find("tl_") != std::string_view::npos ||
           name.find("tradutorlinux") != std::string_view::npos;
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
    if (iequals(name, "install")) { out = TraceComponent::Install; return true; }
    if (iequals(name, "proton")) { out = TraceComponent::Proton; return true; }
    return false;
}

std::string_view trace_component_name(const TraceComponent component) noexcept {
    return component_name(component);
}

namespace {
std::array<bool, 10> g_trace_enabled{};
bool g_trace_filter_active = false;
std::atomic<bool> g_trace_requested{false};
std::mutex g_trace_mutex;
std::filesystem::path g_trace_json_directory;
std::uint64_t g_trace_json_sequence = 0;
bool g_trace_json_enabled = false;
std::atomic<bool> g_trace_json_enabled_fast{false};
std::ofstream g_trace_json_output;
pid_t g_trace_json_pid = -1;
std::mutex g_trace_json_queue_mutex;
std::condition_variable g_trace_json_queue_cv;
std::deque<std::string> g_trace_json_queue;
std::thread g_trace_json_writer;
bool g_trace_json_writer_stop = false;
pid_t g_trace_json_owner_pid = -1;
bool g_trace_json_atexit_registered = false;
struct FunctionTraceRecord {
    bool entering{};
    std::uintptr_t function{};
    std::uintptr_t caller{};
};
std::deque<FunctionTraceRecord> g_function_trace_queue;
constexpr std::size_t kFunctionTraceSeenCapacity = 262144U;
std::array<std::atomic<std::uintptr_t>, kFunctionTraceSeenCapacity> g_function_trace_seen{};

bool open_json_output_locked();
void write_json_event_locked(TraceComponent component, TraceLevel level,
                             std::string_view event, std::span<const TraceField> fields,
                             bool direct = false);

void write_json_record_locked(const std::string& record) {
    if (!open_json_output_locked()) return;
    g_trace_json_output << record;
}

[[nodiscard]] bool remember_function(const std::uintptr_t function) noexcept {
    if (function == 0) return false;
    const std::size_t initial =
        (function >> 4U) % kFunctionTraceSeenCapacity;
    constexpr std::size_t kMaxProbes = 32U;
    for (std::size_t probe = 0; probe < kMaxProbes; ++probe) {
        auto& slot = g_function_trace_seen[(initial + probe) % kFunctionTraceSeenCapacity];
        std::uintptr_t expected = 0;
        if (slot.compare_exchange_strong(expected, function, std::memory_order_relaxed,
                                         std::memory_order_relaxed)) {
            return true;
        }
        if (expected == function) return false;
    }
    return false;
}

void json_writer_loop() {
    for (;;) {
        std::string record;
        FunctionTraceRecord function_record{};
        bool has_function_record = false;
        {
            std::unique_lock lock(g_trace_json_queue_mutex);
            g_trace_json_queue_cv.wait(lock, [] {
                return g_trace_json_writer_stop || !g_trace_json_queue.empty() ||
                       !g_function_trace_queue.empty();
            });
            if (g_trace_json_queue.empty() && g_function_trace_queue.empty() &&
                g_trace_json_writer_stop) return;
            if (!g_function_trace_queue.empty()) {
                function_record = g_function_trace_queue.front();
                g_function_trace_queue.pop_front();
                has_function_record = true;
            } else {
                record = std::move(g_trace_json_queue.front());
                g_trace_json_queue.pop_front();
            }
        }
        if (has_function_record) {
            Dl_info info{};
            const bool resolved = dladdr(reinterpret_cast<void*>(function_record.function), &info) != 0;
            if (!resolved || !is_project_function(info.dli_sname)) continue;
            const std::array fields{
                TraceField{"address", std::to_string(function_record.function)},
                TraceField{"caller", std::to_string(function_record.caller)},
                TraceField{"symbol", info.dli_sname},
            };
            std::lock_guard lock(g_trace_mutex);
            if (g_trace_json_enabled) {
                write_json_event_locked(
                    TraceComponent::Runtime, TraceLevel::Debug,
                    function_record.entering ? "function-enter" : "function-exit", fields, true);
            }
            continue;
        }
        std::lock_guard lock(g_trace_mutex);
        if (g_trace_json_enabled && ::getpid() == g_trace_json_owner_pid) {
            write_json_record_locked(record);
        }
    }
}

void stop_json_writer() noexcept {
    if (g_trace_json_owner_pid != -1 && ::getpid() != g_trace_json_owner_pid) {
        if (g_trace_json_writer.joinable()) g_trace_json_writer.detach();
        return;
    }
    {
        std::lock_guard lock(g_trace_json_queue_mutex);
        g_trace_json_writer_stop = true;
    }
    g_trace_json_queue_cv.notify_all();
    if (g_trace_json_writer.joinable()) g_trace_json_writer.join();
    std::lock_guard lock(g_trace_json_queue_mutex);
    g_trace_json_queue.clear();
    g_function_trace_queue.clear();
    g_trace_json_writer_stop = false;
}

bool open_json_output_locked() {
    const pid_t current_pid = ::getpid();
    if (g_trace_json_output.is_open() && g_trace_json_pid == current_pid) return true;
    if (g_trace_json_output.is_open()) g_trace_json_output.close();
    std::ostringstream filename;
    filename << "events-" << static_cast<long long>(current_pid) << ".jsonl";
    g_trace_json_output.open(g_trace_json_directory / filename.str(),
                             std::ios::out | std::ios::app);
    g_trace_json_pid = current_pid;
    g_trace_json_sequence = 0;
    return static_cast<bool>(g_trace_json_output);
}

void write_json_event_locked(const TraceComponent component, const TraceLevel level,
                             const std::string_view event,
                             const std::span<const TraceField> fields, const bool direct) {
    if (!g_trace_json_enabled) return;

    const std::uint64_t sequence = ++g_trace_json_sequence;

    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::ostringstream record;
    record << "{\n"
           << "  \"sequence\": " << sequence << ",\n"
           << "  \"pid\": " << static_cast<long long>(::getpid()) << ",\n"
           << "  \"timestamp_ms\": " << timestamp << ",\n"
           << "  \"component\": \"" << escape_value(component_name(component)) << "\",\n"
           << "  \"level\": \"" << escape_value(level_name(level)) << "\",\n"
           << "  \"event\": \"" << escape_value(event) << "\",\n"
           << "  \"fields\": {";
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0) record << ',';
        record << "\n    \"" << escape_value(fields[index].key) << "\": \""
               << escape_value(fields[index].value) << '\"';
    }
    if (!fields.empty()) record << '\n' << "  ";
    record << "}\n}\n";

    if (direct || ::getpid() != g_trace_json_owner_pid) {
        write_json_record_locked(record.str());
        return;
    }
    {
        std::lock_guard queue_lock(g_trace_json_queue_mutex);
        if (g_trace_json_queue.size() >= 65536U) return;
        g_trace_json_queue.push_back(record.str());
    }
    g_trace_json_queue_cv.notify_one();
}
}  // namespace

bool configure_trace_json_directory(const std::filesystem::path& directory) noexcept {
    try {
        if (directory.empty()) return false;
        stop_json_writer();
        std::lock_guard<std::mutex> lock(g_trace_mutex);
        std::filesystem::create_directories(directory);
        if (!std::filesystem::is_directory(directory)) return false;
        for (auto& slot : g_function_trace_seen) {
            slot.store(0, std::memory_order_relaxed);
        }
        g_trace_json_directory = directory;
        if (g_trace_json_output.is_open()) g_trace_json_output.close();
        g_trace_json_pid = -1;
        g_trace_json_sequence = 0;
        g_trace_json_enabled = true;
        g_trace_json_owner_pid = ::getpid();
        __atomic_store_n(&tl_function_trace_enabled, static_cast<unsigned char>(1),
                         __ATOMIC_RELEASE);
        g_trace_json_enabled_fast.store(true, std::memory_order_release);
        if (!g_trace_json_atexit_registered) {
            std::atexit([] { disable_trace_json_directory(); });
            g_trace_json_atexit_registered = true;
        }
        g_trace_json_writer = std::thread(json_writer_loop);
        return true;
    } catch (...) {
        return false;
    }
}

bool is_trace_json_enabled() noexcept {
    return g_trace_json_enabled_fast.load(std::memory_order_acquire);
}

void suspend_trace_json_for_fork() noexcept {
    if (!is_trace_json_enabled()) return;
    __atomic_store_n(&tl_function_trace_enabled, static_cast<unsigned char>(0),
                     __ATOMIC_RELEASE);
    g_trace_json_enabled_fast.store(false, std::memory_order_release);
    stop_json_writer();
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    if (g_trace_json_output.is_open()) {
        g_trace_json_output.flush();
        g_trace_json_output.close();
    }
    g_trace_json_pid = -1;
}

void resume_trace_json_after_fork() noexcept {
    if (!g_trace_json_enabled) return;
    try {
        const bool is_new_process = ::getpid() != g_trace_json_owner_pid;
        g_trace_json_owner_pid = ::getpid();
        g_trace_json_pid = -1;
        g_trace_json_sequence = 0;
        if (is_new_process) {
            for (auto& slot : g_function_trace_seen) {
                slot.store(0, std::memory_order_relaxed);
            }
        }
        g_trace_json_writer = std::thread(json_writer_loop);
        __atomic_store_n(&tl_function_trace_enabled, static_cast<unsigned char>(1),
                         __ATOMIC_RELEASE);
        g_trace_json_enabled_fast.store(true, std::memory_order_release);
    } catch (...) {
        g_trace_json_enabled = false;
        g_trace_json_enabled_fast.store(false, std::memory_order_release);
    }
}

void disable_trace_json_directory() noexcept {
    __atomic_store_n(&tl_function_trace_enabled, static_cast<unsigned char>(0),
                     __ATOMIC_RELEASE);
    g_trace_json_enabled_fast.store(false, std::memory_order_release);
    stop_json_writer();
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    g_trace_json_enabled = false;
    if (g_trace_json_output.is_open()) {
        g_trace_json_output.flush();
        g_trace_json_output.close();
    }
}

void configure_trace_filter(const std::vector<TraceComponent>& filter) noexcept {
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    g_trace_enabled.fill(false);
    for (auto c : filter) {
        const auto idx = static_cast<std::size_t>(c);
        if (idx < g_trace_enabled.size()) g_trace_enabled[idx] = true;
    }
    g_trace_filter_active = true;
}

void configure_trace_all() noexcept {
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    g_trace_enabled.fill(true);
    g_trace_filter_active = false;
}

void set_trace_requested(const bool requested) noexcept {
    g_trace_requested.store(requested, std::memory_order_release);
}

bool is_trace_requested() noexcept {
    return g_trace_requested.load(std::memory_order_acquire);
}

bool is_trace_enabled(const TraceComponent component) noexcept {
    std::lock_guard<std::mutex> lock(g_trace_mutex);
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
        case FailureCategory::GuestResourceLimit:
            return "guest-resource-limit";
        case FailureCategory::InternalError:
            return "internal-error";
    }
    return "unknown";
}

void write_trace(std::ostream& stream, const TraceComponent component, const TraceLevel level,
                 const std::string_view event, const std::span<const TraceField> fields) {
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    write_json_event_locked(component, level, event, fields);
    if (g_trace_filter_active) {
        const auto idx = static_cast<std::size_t>(component);
        if (idx >= g_trace_enabled.size() || !g_trace_enabled[idx]) return;
    }
        // Mantém o lock durante a escrita para evitar intercalação de linhas de threads concorrentes.
        stream << "[tl][" << component_name(component) << "][" << level_name(level) << "] " << event;
        for (const TraceField& field : fields) {
            stream << ' ' << field.key << "=\"" << escape_value(field.value) << '"';
        }
        stream << '\n';
}

void write_json_trace(const TraceComponent component, const TraceLevel level,
                      const std::string_view event, const std::span<const TraceField> fields) {
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    write_json_event_locked(component, level, event, fields);
}

FunctionTraceScope::FunctionTraceScope(const std::string_view function) noexcept
    : function_(function) {
    try {
        const std::array fields{TraceField{"function", std::string{function_}}};
        write_json_trace(TraceComponent::Runtime, TraceLevel::Debug, "function-enter", fields);
    } catch (...) {
    }
}

FunctionTraceScope::~FunctionTraceScope() noexcept {
    try {
        const std::array fields{TraceField{"function", std::string{function_}}};
        write_json_trace(TraceComponent::Runtime, TraceLevel::Debug, "function-exit", fields);
    } catch (...) {
    }
}

void enqueue_function_json_trace(const bool entering, const std::uintptr_t function,
                                 const std::uintptr_t caller) noexcept {
    if (!is_trace_json_enabled()) return;
    if (!remember_function(function)) return;
    try {
        if (::getpid() != g_trace_json_owner_pid) return;
        {
            std::lock_guard lock(g_trace_json_queue_mutex);
            g_function_trace_queue.push_back(FunctionTraceRecord{entering, function, caller});
        }
        g_trace_json_queue_cv.notify_one();
    } catch (...) {
    }
}

}  // namespace tradutorlinux::diagnostics
