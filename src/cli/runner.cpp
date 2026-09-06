#include "tradutorlinux/cli.hpp"
#include "cli_internal.hpp"

#include "tradutorlinux/catalog/app_catalog.hpp"
#include "tradutorlinux/backend/proton.hpp"
#include "tradutorlinux/compat/materializer.hpp"
#include "tradutorlinux/compat/profile.hpp"
#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/diagnostics/crash_context.hpp"
#include "tradutorlinux/loader/import_resolver.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/module_graph.hpp"
#include "tradutorlinux/loader/process.hpp"
#include "tradutorlinux/package/msix.hpp"
#include "tradutorlinux/pe/pe_reader.hpp"
#if defined(TRADUTORLINUX_RUST_PE_PARSER)
#include "../pe/rust_pe_parser.hpp"
#endif
#if defined(TRADUTORLINUX_RUST_MSIX_PARSER)
#include "tradutorlinux/package/rust_msix_parser.hpp"
#endif
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/process/isolate.hpp"
#include "tradutorlinux/runtime/msvcrt.hpp"
#include "tradutorlinux/runtime/guest_context.hpp"
#include "tradutorlinux/runtime/unwind.hpp"
#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/util/basics.hpp"
#include "tradutorlinux/util/sha256.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <ostream>
#include <spawn.h>
#include <span>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

extern char** environ;

namespace tradutorlinux {

using namespace cli_detail;

namespace {

constexpr std::uint64_t kMaxPeFileSize = 512ULL * 1024 * 1024;

// Os campos de IMAGE_TLS_DIRECTORY são VAs absolutos gravados na imagem na
// base preferencial. O parser preserva essa representação para o --report;
// antes da execução eles precisam apontar para a cópia realmente mapeada.
[[nodiscard]] std::uint64_t relocate_tls_va(const std::uint64_t value,
                                             const pe::PeInfo& info,
                                             const loader::MappedImage& image) noexcept {
    if (value == 0 || value < info.image_base ||
        value - info.image_base >= static_cast<std::uint64_t>(info.size_of_image)) {
        return value;
    }
    const std::uint64_t rva = value - info.image_base;
    if (rva >= image.size || image.base > std::numeric_limits<std::uint64_t>::max() - rva) {
        return value;
    }
    return image.base + rva;
}

[[nodiscard]] std::vector<std::uint64_t> relocated_tls_callbacks(
    const pe::PeInfo& info, const loader::MappedImage& image) {
    std::vector<std::uint64_t> callbacks;
    callbacks.reserve(info.tls_info.callback_vas.size());
    for (const std::uint64_t callback : info.tls_info.callback_vas) {
        callbacks.push_back(relocate_tls_va(callback, info, image));
    }
    return callbacks;
}

[[nodiscard]] std::optional<std::vector<std::byte>> read_file(
    const std::filesystem::path& path) {
    TL_TRACE_FUNCTION();
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return std::nullopt;
    }
    stream.seekg(0, std::ios::end);
    const std::streamoff end = stream.tellg();
    if (end < 0) {
        return std::nullopt;
    }
    if (static_cast<std::uint64_t>(end) > kMaxPeFileSize) {
        return std::nullopt;
    }
    stream.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            return std::nullopt;
        }
    }
    return bytes;
}

#if defined(TRADUTORLINUX_RUST_MSIX_PARSER)
struct MsixFileReadResult {
    std::optional<std::vector<std::uint8_t>> bytes;
    std::uint64_t size{0};
    bool too_large{false};
    bool unavailable{false};
    bool internal_failure{false};
};

[[nodiscard]] MsixFileReadResult read_msix_file(
    const std::filesystem::path& path) {
    TL_TRACE_FUNCTION();
    MsixFileReadResult result;
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        result.unavailable = true;
        return result;
    }
    stream.seekg(0, std::ios::end);
    const std::streamoff end = stream.tellg();
    if (end < 0) {
        result.unavailable = true;
        return result;
    }
    result.size = static_cast<std::uint64_t>(end);
    if (result.size > TL_MSIX_LIMIT_MAX_PACKAGE_BYTES) {
        result.too_large = true;
        return result;
    }
    try {
        stream.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(result.size));
        if (!bytes.empty()) {
            stream.read(reinterpret_cast<char*>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size()));
            if (!stream) {
                result.unavailable = true;
                return result;
            }
        }
        result.bytes = std::move(bytes);
    } catch (const std::bad_alloc&) {
        result.internal_failure = true;
    }
    return result;
}
#endif

[[nodiscard]] bool extract_archive_with_7z(const std::filesystem::path& archive,
                                           const std::filesystem::path& destination) {
    TL_TRACE_FUNCTION();
    std::array<std::string, 5> arguments{
        "7z", "x", "-y", "-o" + destination.string(), archive.string()};
    std::array<char*, 6> argv{};
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        argv[index] = arguments[index].data();
    }

    posix_spawn_file_actions_t actions{};
    if (posix_spawn_file_actions_init(&actions) != 0) {
        return false;
    }
    const auto destroy_actions = [&actions]() noexcept {
        (void)posix_spawn_file_actions_destroy(&actions);
    };
    if (posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0) != 0 ||
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0) != 0) {
        destroy_actions();
        return false;
    }

    pid_t child = -1;
    const int spawn_status = posix_spawnp(&child, arguments[0].c_str(), &actions, nullptr,
                                          argv.data(), environ);
    destroy_actions();
    if (spawn_status != 0) {
        return false;
    }

    int child_status = 0;
    pid_t wait_result = -1;
    do {
        wait_result = ::waitpid(child, &child_status, 0);
    } while (wait_result < 0 && errno == EINTR);
    return wait_result == child && WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0;
}

struct FileSignature {
    std::uintmax_t size{};
    std::filesystem::file_time_type modified{};
};

using ExecutableSnapshot = std::map<std::filesystem::path, FileSignature>;

[[nodiscard]] std::string lowercase(std::string value) {
    TL_TRACE_FUNCTION();
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

void write_path_validation_trace(std::ostream& stream, const bool trace_enabled,
                                 const diagnostics::TraceComponent component,
                                 const std::string_view phase,
                                 const compat::PathValidationMetrics& metrics,
                                 const std::string_view status,
                                 const std::string_view detail = {}) {
    if (!trace_enabled || metrics.backend != compat::PathValidationBackend::Rust ||
        (metrics.checks == 0U && !metrics.infrastructure_error)) {
        return;
    }
    const std::array fields{
        diagnostics::TraceField{"phase", std::string{phase}},
        diagnostics::TraceField{"backend", "rust"},
        diagnostics::TraceField{"handle-count", std::to_string(metrics.handle_count)},
        diagnostics::TraceField{"checks", std::to_string(metrics.checks)},
        diagnostics::TraceField{"rejected", std::to_string(metrics.rejected)},
        diagnostics::TraceField{"duration-us", std::to_string(metrics.duration_us)},
        diagnostics::TraceField{"status", std::string{status}},
        diagnostics::TraceField{"detail", std::string{detail}},
    };
    diagnostics::write_trace(
        stream, component,
        status == "internal-error" ? diagnostics::TraceLevel::Error
                                    : diagnostics::TraceLevel::Info,
        "path-validation", fields);
}

enum class PeParserBackend {
    Cpp,
    Rust,
};

enum class MsixParserBackend {
    Cpp,
    Rust,
};

[[nodiscard]] MsixParserBackend select_msix_parser_backend(
    const CommandLine& command) noexcept {
#if defined(TRADUTORLINUX_RUST_MSIX_PARSER)
    if ((command.mode == CommandMode::DirectRun && command.report_only) ||
        command.mode == CommandMode::Install) {
        return MsixParserBackend::Rust;
    }
#else
    (void)command;
#endif
    return MsixParserBackend::Cpp;
}

#if defined(TRADUTORLINUX_RUST_MSIX_PARSER)
[[nodiscard]] const char* msix_status_label(const std::uint32_t status) noexcept {
    switch (status) {
        case TL_MSIX_STATUS_SUCCESS:
            return "success";
        case TL_MSIX_STATUS_TRUNCATED:
            return "truncated";
        case TL_MSIX_STATUS_MALFORMED:
            return "malformed";
        case TL_MSIX_STATUS_UNSUPPORTED_FORMAT:
            return "unsupported-format";
        case TL_MSIX_STATUS_UNSUPPORTED_MECHANISM:
            return "unsupported-mechanism";
        case TL_MSIX_STATUS_INVALID_ARGUMENT:
            return "invalid-argument";
        case TL_MSIX_STATUS_BUFFER_TOO_SMALL:
            return "buffer-too-small";
        case TL_MSIX_STATUS_INPUT_TOO_LARGE:
            return "input-too-large";
        case TL_MSIX_STATUS_OUTPUT_TOO_LARGE:
            return "output-too-large";
        case TL_MSIX_STATUS_INTERNAL:
            return "internal";
    }
    return "unknown";
}

[[nodiscard]] ExitCode map_msix_status_to_exit(const std::uint32_t status) noexcept {
    switch (status) {
        case TL_MSIX_STATUS_TRUNCATED:
        case TL_MSIX_STATUS_MALFORMED:
            return ExitCode::MalformedPe;
        case TL_MSIX_STATUS_UNSUPPORTED_FORMAT:
        case TL_MSIX_STATUS_UNSUPPORTED_MECHANISM:
            return ExitCode::Unsupported;
        case TL_MSIX_STATUS_SUCCESS:
            return ExitCode::Success;
        default:
            return ExitCode::InternalError;
    }
}

void write_msix_parse_trace(std::ostream& stream, const bool enabled,
                            const diagnostics::TraceComponent component,
                            const std::uint32_t status,
                            const tl_msix_error_v1& error) {
    if (!enabled) return;
    std::vector<diagnostics::TraceField> fields{
        {"format", "MSIX / AppX"},
        {"backend", "rust"},
        {"status", msix_status_label(status)},
    };
    if (status != TL_MSIX_STATUS_SUCCESS) {
        fields.push_back({"code", std::to_string(error.code)});
        fields.push_back({"phase", std::to_string(error.phase)});
        fields.push_back({"input-offset", std::to_string(error.input_offset)});
        fields.push_back({"detail-value", std::to_string(error.detail_value)});
    }
    diagnostics::write_trace(stream, component,
                             status == TL_MSIX_STATUS_SUCCESS
                                 ? diagnostics::TraceLevel::Info
                                 : diagnostics::TraceLevel::Error,
                             "package-parse", fields);
}

[[nodiscard]] package::RustMsixParseResult make_msix_input_failure(
    const MsixFileReadResult& input) {
    package::RustMsixParseResult result;
    result.status = input.too_large ? TL_MSIX_STATUS_INPUT_TOO_LARGE
                                    : TL_MSIX_STATUS_INTERNAL;
    result.internal_failure = true;
    result.error = input.too_large
                       ? tl_msix_error_v1{TL_MSIX_ERROR_INPUT_TOO_LARGE,
                                          TL_MSIX_ERROR_PHASE_INPUT,
                                          TL_MSIX_ERROR_OFFSET_UNKNOWN, input.size}
                       : tl_msix_error_v1{TL_MSIX_ERROR_INTERNAL,
                                          TL_MSIX_ERROR_PHASE_INPUT,
                                          TL_MSIX_ERROR_OFFSET_UNKNOWN, input.size};
    result.error_message = input.too_large
                               ? "pacote MSIX / AppX excede o limite de entrada"
                               : (input.internal_failure
                                      ? "não foi possível alocar o buffer do pacote MSIX / AppX"
                                      : "não foi possível ler o pacote MSIX / AppX");
    return result;
}
#endif

[[nodiscard]] PeParserBackend select_pe_parser_backend(
    const CommandLine& command,
    const std::optional<compat::Profile>& compatibility_profile) noexcept {
#if defined(TRADUTORLINUX_RUST_PE_PARSER)
    const bool direct_report = command.mode == CommandMode::DirectRun && command.report_only;
    const bool native_app_run =
        command.mode == CommandMode::AppRun && !command.report_only &&
        !(compatibility_profile.has_value() &&
          compatibility_profile->backend.kind == compat::BackendKind::Proton);
    if (direct_report || native_app_run) {
        return PeParserBackend::Rust;
    }
#else
    (void)command;
    (void)compatibility_profile;
#endif
    return PeParserBackend::Cpp;
}

[[nodiscard]] bool is_x64_pe_file(const std::filesystem::path& path) {
    TL_TRACE_FUNCTION();
    const std::optional<std::vector<std::byte>> bytes = read_file(path);
    if (!bytes.has_value()) {
        return false;
    }
    const pe::ParseResult parsed = pe::parse_pe(*bytes);
    return parsed.status == pe::ParseStatus::Success && parsed.info.is_pe32_plus &&
           parsed.info.machine == 0x8664;
}

[[nodiscard]] ExecutableSnapshot snapshot_executables(const std::filesystem::path& drive_c) {
    TL_TRACE_FUNCTION();
    ExecutableSnapshot result;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator iterator(
        drive_c, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end;
    for (; !ec && iterator != end; iterator.increment(ec)) {
        const std::filesystem::directory_entry& entry = *iterator;
        if (!entry.is_regular_file(ec) || ec || lowercase(entry.path().extension().string()) != ".exe") {
            ec.clear();
            continue;
        }
        const std::optional<std::vector<std::byte>> bytes = read_file(entry.path());
        if (!bytes.has_value()) {
            continue;
        }
        const pe::ParseResult parsed = pe::parse_pe(*bytes);
        if (parsed.status != pe::ParseStatus::Success || !parsed.info.is_pe32_plus ||
            parsed.info.machine != 0x8664) {
            continue;
        }
        const std::uintmax_t size = entry.file_size(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        const std::filesystem::file_time_type modified = entry.last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        result.emplace(entry.path().lexically_normal(), FileSignature{size, modified});
    }
    return result;
}

[[nodiscard]] std::vector<std::filesystem::path> changed_executables(
    const ExecutableSnapshot& before, const ExecutableSnapshot& after) {
    TL_TRACE_FUNCTION();
    std::vector<std::filesystem::path> candidates;
    for (const auto& [path, signature] : after) {
        const auto previous = before.find(path);
        if (previous == before.end() || previous->second.size != signature.size ||
            previous->second.modified != signature.modified) {
            candidates.push_back(path);
        }
    }
    return candidates;
}

[[nodiscard]] std::optional<std::filesystem::path> resolve_installed_executable(
    const std::filesystem::path& raw_path, const std::filesystem::path& prefix_root) {
    TL_TRACE_FUNCTION();
    const std::string value = raw_path.string();
    const bool windows_absolute =
        (value.size() >= 2 && std::isalpha(static_cast<unsigned char>(value[0])) != 0 &&
         value[1] == ':') ||
        (!value.empty() && value.front() == '\\');
    std::filesystem::path candidate = raw_path.is_absolute()
                                          ? raw_path
                                          : prefix::resolve_windows_path(value, prefix_root);
    if (windows_absolute) {
        candidate = prefix::resolve_windows_path(value, prefix_root);
    }
    std::error_code ec;
    candidate = std::filesystem::weakly_canonical(candidate, ec);
    const prefix::EnvironmentPaths paths = prefix::get_environment_paths(prefix_root);
    if (ec || !prefix::is_path_within(candidate, paths.drive_c) ||
        !std::filesystem::is_regular_file(candidate, ec) || ec || !is_x64_pe_file(candidate)) {
        return std::nullopt;
    }
    return candidate;
}

[[nodiscard]] std::string unique_app_id(const catalog::AppCatalog& app_catalog,
                                        const std::string_view requested_name) {
    TL_TRACE_FUNCTION();
    const std::string base = catalog::AppCatalog::generate_id(requested_name);
    auto has_id = [&app_catalog](const std::string_view value) {
        return std::any_of(app_catalog.list_apps().begin(), app_catalog.list_apps().end(),
                           [value](const catalog::AppEntry& entry) { return entry.id == value; });
    };
    if (!has_id(base)) {
        return base;
    }
    for (std::size_t suffix = 2; suffix < 10000; ++suffix) {
        const std::string candidate = base + "-" + std::to_string(suffix);
        if (!has_id(candidate)) {
            return candidate;
        }
    }
    return base + "-overflow";
}

void record_executable_fingerprint(catalog::AppEntry& entry,
                                   const std::filesystem::path& executable_path) {
    const auto hash = util::sha256_file(executable_path);
    if (hash.has_value()) {
        entry.app_sha256 = *hash;
    }
}

void write_install_trace(const bool enabled, std::ostream& stream,
                         const diagnostics::TraceLevel level, const std::string_view event,
                         const std::initializer_list<diagnostics::TraceField> fields) {
    TL_TRACE_FUNCTION();
    if (enabled) {
        diagnostics::write_trace(stream, diagnostics::TraceComponent::Install, level, event,
                                 std::span<const diagnostics::TraceField>{fields.begin(), fields.size()});
    }
}

[[nodiscard]] ExitCode finish_proton_outcome(const backend::ProtonRunResult& result,
                                             std::ostream& stderr_stream) {
    const process::GuestOutcome& outcome = result.outcome;
    switch (outcome.kind) {
        case process::GuestOutcomeKind::Exited:
            return static_cast<ExitCode>(outcome.exit_code);
        case process::GuestOutcomeKind::TimedOut:
            stderr_stream << "erro: o processo Proton não terminou dentro do tempo limite\n";
            return ExitCode::GuestTimeout;
        case process::GuestOutcomeKind::Signaled: {
            const process::SignalDescription signal =
                process::describe_signal(outcome.signal_number);
            stderr_stream << "erro: o processo Proton terminou por sinal " << signal.name
                          << " (" << signal.detail << ")\n";
            return ExitCode::GuestFault;
        }
        case process::GuestOutcomeKind::ResourceLimited:
            stderr_stream << "erro: o processo Proton excedeu o limite de "
                          << process::resource_limit_name(outcome.resource) << "\n";
            return ExitCode::GuestResourceLimit;
        case process::GuestOutcomeKind::ResourceSetupFailed:
            stderr_stream << "erro: não foi possível instalar limites no processo Proton\n";
            return ExitCode::InternalError;
        case process::GuestOutcomeKind::SpawnFailed:
            stderr_stream << "erro: não foi possível iniciar o launcher Proton\n";
            return ExitCode::InternalError;
    }
    return ExitCode::InternalError;
}

}  // namespace

ExitCode run_command(const CommandLine& command_line, std::ostream& stdout_stream,
                     std::ostream& stderr_stream) {
    // Configura filtro de trace (inspirado em WINEDEBUG): --trace sozinho = tudo,
    // --trace=pe,loader filtra apenas esses componentes.
    if (command_line.trace_enabled) {
        if (!command_line.trace_channels_raw.empty()) {
            std::vector<diagnostics::TraceComponent> filter;
            filter.reserve(command_line.trace_channels_raw.size());
            for (const auto& name : command_line.trace_channels_raw) {
                diagnostics::TraceComponent comp;
                if (diagnostics::trace_component_from_name(name, comp)) {
                    filter.push_back(comp);
                }
            }
            diagnostics::configure_trace_filter(filter);
        } else {
            diagnostics::configure_trace_all();
        }
    } else {
        diagnostics::configure_trace_all();
    }

    if (command_line.trace_json_directory.has_value() &&
        !diagnostics::configure_trace_json_directory(*command_line.trace_json_directory)) {
        stderr_stream << "erro: não foi possível criar o diretório de trace JSON: "
                      << command_line.trace_json_directory->string() << '\n';
        return ExitCode::Usage;
    }

    if (command_line.show_help) {
        print_help(stdout_stream);
        return ExitCode::Success;
    }

    if (command_line.show_version) {
        stdout_stream << "TradutorLinux " << TRADUTORLINUX_VERSION << '\n';
        return ExitCode::Success;
    }

    // Modo: Listar biblioteca de aplicativos
    if (command_line.mode == CommandMode::AppList) {
        catalog::AppCatalog app_catalog;
        (void)app_catalog.load_from_file();
        const auto& apps = app_catalog.list_apps();
        if (apps.empty()) {
            stdout_stream << "Nenhum aplicativo cadastrado na biblioteca.\n";
            stdout_stream << "Dica: use 'tradutorlinux app add <arquivo.exe> --name \"Nome\"' ou 'tradutorlinux install <setup.exe>'.\n";
            return ExitCode::Success;
        }
        stdout_stream << "Aplicativos cadastrados na biblioteca (" << apps.size() << "):\n\n";
        for (const auto& app : apps) {
            stdout_stream << "  [" << app.id << "] " << app.name << '\n';
            stdout_stream << "    Executável: " << app.executable_path << '\n';
            if (!app.prefix_path.empty()) {
                stdout_stream << "    Prefixo:    " << app.prefix_path << '\n';
            }
            stdout_stream << '\n';
        }
        return ExitCode::Success;
    }

    // Modo: Cadastrar aplicativo manualmente na biblioteca
    if (command_line.mode == CommandMode::AppAdd) {
        if (!command_line.executable_path.has_value()) {
            stderr_stream << "erro: informe o caminho do executável para cadastrar\n";
            return ExitCode::Usage;
        }
        catalog::AppCatalog app_catalog;
        (void)app_catalog.load_from_file();
        catalog::AppEntry entry;
        entry.name = command_line.app_name.empty()
                         ? command_line.executable_path->filename().string()
                         : command_line.app_name;
        entry.id = command_line.app_id.empty()
                       ? unique_app_id(app_catalog, entry.name)
                       : command_line.app_id;
        entry.executable_path = command_line.executable_path->string();
        entry.prefix_path = command_line.custom_prefix.has_value()
                                ? command_line.custom_prefix->string()
                                : prefix::default_app_prefix(entry.id).string();
        const std::filesystem::path entry_prefix{entry.prefix_path};
        if (!prefix::initialize_prefix(entry_prefix)) {
            stderr_stream << "erro: não foi possível preparar o prefixo do aplicativo\n";
            return ExitCode::InternalError;
        }
        const prefix::EnvironmentPaths paths = prefix::get_environment_paths(entry_prefix);
        const std::filesystem::path executable_parent = command_line.executable_path->parent_path();
        entry.working_directory =
            !executable_parent.empty() && std::filesystem::is_directory(executable_parent)
                ? executable_parent.string()
                : paths.drive_c.string();
        record_executable_fingerprint(entry, *command_line.executable_path);
        entry.cpu_limit_seconds = command_line.cpu_limit_seconds;
        entry.memory_limit_mib = command_line.memory_limit_mib;

        if (!app_catalog.add_app(entry) || !app_catalog.save_to_file()) {
            stderr_stream << "erro: falha ao salvar aplicativo na biblioteca\n";
            return ExitCode::InternalError;
        }
        (void)catalog::AppCatalog::create_desktop_entry(entry);
        stdout_stream << "Aplicativo '" << entry.name << "' cadastrado com sucesso [id: "
                      << entry.id << "].\n";
        return ExitCode::Success;
    }

    // Modo: Remover aplicativo da biblioteca
    if (command_line.mode == CommandMode::AppRemove) {
        if (command_line.app_id.empty()) {
            stderr_stream << "erro: informe o ID do aplicativo a remover\n";
            return ExitCode::Usage;
        }
        catalog::AppCatalog app_catalog;
        (void)app_catalog.load_from_file();
        if (!app_catalog.remove_app(command_line.app_id) || !app_catalog.save_to_file()) {
            stderr_stream << "erro: aplicativo com ID '" << command_line.app_id
                          << "' não encontrado na biblioteca\n";
            return ExitCode::Usage;
        }
        stdout_stream << "Aplicativo '" << command_line.app_id
                      << "' removido da biblioteca com sucesso.\n";
        return ExitCode::Success;
    }

    runtime::GuestContext execution_context;
    runtime::GuestContextScope execution_scope(execution_context);
    CommandLine effective_cmd = command_line;
    std::optional<ExecutableSnapshot> installation_before;
    std::string installation_name;
    std::string installation_id;
    std::string compatibility_app_id;
    std::string compatibility_app_sha256;
    std::string compatibility_app_version;
    std::optional<compat::Profile> compatibility_profile;
    std::optional<compat::FileExposure> compatibility_files;

    // Modo: Executar aplicativo cadastrado
    if (command_line.mode == CommandMode::AppRun) {
        if (command_line.app_id.empty()) {
            stderr_stream << "erro: informe o ID ou nome do aplicativo para executar\n";
            return ExitCode::Usage;
        }
        catalog::AppCatalog app_catalog;
        (void)app_catalog.load_from_file();
        const auto app_opt = app_catalog.find_app(command_line.app_id);
        if (!app_opt) {
            stderr_stream << "erro: aplicativo '" << command_line.app_id
                          << "' não encontrado na biblioteca\n";
            return ExitCode::InputUnavailable;
        }
        compatibility_app_id = app_opt->id;
        compatibility_app_sha256 = app_opt->app_sha256;
        compatibility_app_version = app_opt->app_version;
        effective_cmd.executable_path = std::filesystem::path(app_opt->executable_path);
        if (!app_opt->prefix_path.empty()) {
            effective_cmd.custom_prefix = std::filesystem::path(app_opt->prefix_path);
        }
        if (!app_opt->working_directory.empty()) {
            effective_cmd.guest_working_directory =
                std::filesystem::path(app_opt->working_directory);
        }
        if (!command_line.cpu_limit_set) {
            effective_cmd.cpu_limit_seconds = app_opt->cpu_limit_seconds;
            effective_cmd.cpu_limit_set = app_opt->cpu_limit_seconds != 0;
        }
        if (!command_line.memory_limit_set) {
            effective_cmd.memory_limit_mib = app_opt->memory_limit_mib;
            effective_cmd.memory_limit_set = app_opt->memory_limit_mib != 0;
        }
        std::vector<std::string> combined_args = app_opt->args;
        combined_args.insert(combined_args.end(), command_line.guest_arguments.begin(),
                             command_line.guest_arguments.end());
        effective_cmd.guest_arguments = std::move(combined_args);
    }

    // Modo: Instalar aplicativo
    if (command_line.mode == CommandMode::Install) {
        if (!effective_cmd.executable_path.has_value()) {
            stderr_stream << "erro: o comando 'install' requer o caminho do instalador (.exe, .msix ou .appx)\n";
            return ExitCode::Usage;
        }
        if (effective_cmd.report_only) {
            stderr_stream << "erro: --report não pode ser usado com o comando 'install'\n";
            return ExitCode::Usage;
        }
        catalog::AppCatalog app_catalog;
        (void)app_catalog.load_from_file();
        installation_name = effective_cmd.app_name.empty()
                                ? effective_cmd.executable_path->stem().string()
                                : effective_cmd.app_name;
        installation_id = unique_app_id(app_catalog, installation_name);
        effective_cmd.app_id = installation_id;
        const std::filesystem::path p_root = effective_cmd.custom_prefix.value_or(
            prefix::default_app_prefix(installation_id));
        effective_cmd.custom_prefix = p_root;
        if (!prefix::initialize_prefix(p_root)) {
            stderr_stream << "erro: não foi possível preparar o prefixo da instalação\n";
            return ExitCode::InternalError;
        }
        installation_before = snapshot_executables(prefix::get_environment_paths(p_root).drive_c);
        write_install_trace(effective_cmd.trace_enabled, stderr_stream, diagnostics::TraceLevel::Info,
                            "prepared", {{"prefix", p_root.string()},
                                         {"app-id", installation_id},
                                         {"name", installation_name}});
    }

    if (!effective_cmd.executable_path.has_value()) {
        stderr_stream << "erro: informe um arquivo .exe\n";
        print_help(stderr_stream);
        return ExitCode::Usage;
    }

    const std::filesystem::path prefix_dir =
        effective_cmd.custom_prefix.value_or(prefix::default_prefix_root());
    if (!prefix::initialize_prefix(prefix_dir)) {
        stderr_stream << "erro: não foi possível preparar o prefixo da execução\n";
        return ExitCode::InternalError;
    }

    if (effective_cmd.mode == CommandMode::AppRun) {
        const compat::ProfileLoadResult profile = compat::load_profile(
            prefix_dir, compatibility_app_id, compatibility_app_sha256,
            compatibility_app_version);
        const std::string profile_status = [&profile]() {
            switch (profile.status) {
                case compat::ProfileStatus::Missing: return std::string{"missing"};
                case compat::ProfileStatus::Loaded: return std::string{"loaded"};
                case compat::ProfileStatus::Invalid: return std::string{"invalid"};
                case compat::ProfileStatus::InternalError: return std::string{"internal-error"};
            }
            return std::string{"unknown"};
        }();
        if (effective_cmd.trace_enabled) {
            const std::array fields{
                diagnostics::TraceField{"status", profile_status},
                diagnostics::TraceField{"prefix", prefix_dir.string()},
                diagnostics::TraceField{"app-id", compatibility_app_id},
                diagnostics::TraceField{"files", std::to_string(profile.profile.files.size())},
                diagnostics::TraceField{"detail", profile.error},
            };
            diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Runtime,
                                     profile.status == compat::ProfileStatus::Invalid
                                         ? diagnostics::TraceLevel::Warning
                                         : profile.status == compat::ProfileStatus::InternalError
                                               ? diagnostics::TraceLevel::Error
                                               : diagnostics::TraceLevel::Info,
                                     "compat-profile", fields);
        }
        write_path_validation_trace(
            stderr_stream, effective_cmd.trace_enabled, diagnostics::TraceComponent::Runtime,
            "profile", profile.path_validation,
            profile.status == compat::ProfileStatus::InternalError ? "internal-error"
                                                                     : profile.path_validation.rejected != 0U
                                                                           ? "invalid-input"
                                                                           : "completed",
            profile.error);
        if (profile.status == compat::ProfileStatus::InternalError) {
            stderr_stream << "erro: falha interna ao validar o perfil de compatibilidade";
            if (!profile.error.empty()) stderr_stream << ": " << profile.error;
            stderr_stream << '\n';
            return ExitCode::InternalError;
        }
        if (profile.status == compat::ProfileStatus::Loaded) {
            compatibility_profile = profile.profile;
        }
        if (profile.status != compat::ProfileStatus::Loaded) {
            stderr_stream << "aviso: perfil de compatibilidade " << profile_status
                          << "; usando comportamento genérico";
            if (!profile.error.empty()) stderr_stream << ": " << profile.error;
            stderr_stream << '\n';
        }
    }

    const prefix::EnvironmentPaths active_paths = prefix::get_environment_paths(prefix_dir);
    const std::filesystem::path legacy_prefix = prefix::default_prefix_root();
    const bool uses_legacy_shared_prefix =
        effective_cmd.mode == CommandMode::AppRun &&
        (prefix_dir == legacy_prefix ||
         prefix::is_path_within(prefix_dir, legacy_prefix) ||
         prefix::is_path_within(*effective_cmd.guest_working_directory, prefix_dir));
    if (!effective_cmd.guest_working_directory.has_value()) {
        if (effective_cmd.mode == CommandMode::DirectRun) {
            std::filesystem::path parent = effective_cmd.executable_path->parent_path();
            if (!parent.empty() && std::filesystem::is_directory(parent)) {
                effective_cmd.guest_working_directory = parent;
            } else {
                std::error_code current_directory_error;
                effective_cmd.guest_working_directory =
                    std::filesystem::current_path(current_directory_error);
                if (current_directory_error) {
                    stderr_stream << "erro: não foi possível obter o diretório atual\n";
                    return ExitCode::InternalError;
                }
            }
        } else {
            effective_cmd.guest_working_directory = active_paths.drive_c;
        }
    } else {
        std::error_code working_directory_error;
        const bool is_directory = std::filesystem::is_directory(
            *effective_cmd.guest_working_directory, working_directory_error);
        const bool within_active_prefix = prefix::is_path_within(
            *effective_cmd.guest_working_directory, active_paths.drive_c);
        const std::filesystem::path executable_parent =
            effective_cmd.executable_path->parent_path();
        const bool is_external_app_directory =
            !executable_parent.empty() &&
            std::filesystem::is_directory(executable_parent, working_directory_error) &&
            prefix::is_path_within(*effective_cmd.executable_path,
                                   *effective_cmd.guest_working_directory) &&
            prefix::is_path_within(executable_parent, *effective_cmd.guest_working_directory) &&
            !within_active_prefix;
        if (working_directory_error || !is_directory ||
            (!within_active_prefix && !uses_legacy_shared_prefix && !is_external_app_directory)) {
            effective_cmd.guest_working_directory = active_paths.drive_c;
        }
    }

    if (!std::filesystem::exists(*effective_cmd.executable_path)) {
        const std::filesystem::path resolved =
            prefix::resolve_windows_path(effective_cmd.executable_path->string(), prefix_dir);
        if (std::filesystem::exists(resolved)) {
            effective_cmd.executable_path = resolved;
        }
    }

    std::error_code filesystem_error;
    const bool is_regular_file =
        std::filesystem::is_regular_file(*effective_cmd.executable_path, filesystem_error);
    if (filesystem_error || !is_regular_file) {
        stderr_stream << "erro: não foi possível acessar o arquivo: "
                      << effective_cmd.executable_path->string() << '\n';
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "input"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        return ExitCode::InputUnavailable;
    }

    if (effective_cmd.trace_enabled) {
        const std::string input_path = effective_cmd.executable_path->string();
        const std::array input_fields{diagnostics::TraceField{"path", input_path}};
        diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Cli,
                                 diagnostics::TraceLevel::Info, "input", input_fields);
    }

    const MsixParserBackend msix_backend = select_msix_parser_backend(effective_cmd);
    const bool rust_msix_backend = msix_backend == MsixParserBackend::Rust;
    const bool is_package = rust_msix_backend
                                ? package::has_msix_or_appx_extension(
                                      *effective_cmd.executable_path)
                                : package::is_msix_or_appx_package(
                                      *effective_cmd.executable_path);
    if (is_package) {
        std::optional<package::AppxPackageInfo> package_info;
#if defined(TRADUTORLINUX_RUST_MSIX_PARSER)
        if (rust_msix_backend) {
            const MsixFileReadResult input =
                read_msix_file(*effective_cmd.executable_path);
            const package::RustMsixParseResult parsed =
                input.bytes.has_value()
                    ? package::parse_msix_rust(std::span<const std::uint8_t>{
                          input.bytes->data(), input.bytes->size()})
                    : make_msix_input_failure(input);
            if (parsed.status != TL_MSIX_STATUS_SUCCESS) {
                const diagnostics::TraceComponent component =
                    effective_cmd.mode == CommandMode::Install
                        ? diagnostics::TraceComponent::Install
                        : diagnostics::TraceComponent::Cli;
                write_msix_parse_trace(stderr_stream, effective_cmd.trace_enabled, component,
                                       parsed.status, parsed.error);
                stderr_stream << "erro: pacote MSIX / AppX inválido ou não suportado\n";
                return map_msix_status_to_exit(parsed.status);
            }
            package_info = parsed.info;
            const diagnostics::TraceComponent component =
                effective_cmd.mode == CommandMode::Install
                    ? diagnostics::TraceComponent::Install
                    : diagnostics::TraceComponent::Cli;
            write_msix_parse_trace(stderr_stream, effective_cmd.trace_enabled, component,
                                   parsed.status, parsed.error);
        } else {
            package_info = package::inspect_msix_package(*effective_cmd.executable_path);
        }
#else
        package_info = package::inspect_msix_package(*effective_cmd.executable_path);
#endif
        if (!package_info.has_value()) {
            if (effective_cmd.trace_enabled) {
                write_install_trace(effective_cmd.mode == CommandMode::Install,
                                    stderr_stream, diagnostics::TraceLevel::Error, "failed",
                                    {{"stage", "package-parse"}, {"prefix", prefix_dir.string()},
                                     {"app-id", installation_id}});
            }
            stderr_stream << "erro: pacote MSIX / AppX inválido ou não suportado\n";
            return ExitCode::MalformedPe;
        }
        if (effective_cmd.report_only) {
            stdout_stream << "TradutorLinux package report\n";
            stdout_stream << "format: MSIX / AppX package\n";
            stdout_stream << "package-name: "
                          << (package_info->package_name.empty()
                                  ? effective_cmd.executable_path->stem().string()
                                  : package_info->package_name)
                          << '\n';
            if (!package_info->publisher.empty()) {
                stdout_stream << "publisher: " << package_info->publisher << '\n';
            }
            if (!package_info->version.empty()) {
                stdout_stream << "version: " << package_info->version << '\n';
            }
            if (package_info->main_executable.has_value()) {
                stdout_stream << "main-executable: " << *package_info->main_executable << '\n';
            }
            stdout_stream << "applications: " << package_info->applications.size() << '\n';
            for (const auto& app : package_info->applications) {
                stdout_stream << "  app: id=\"" << app.id << "\" exec=\"" << app.executable
                              << "\" name=\"" << app.display_name << "\"\n";
            }
            stdout_stream << "result: package-recognized\n";
            stdout_stream << "execution: not-attempted\n";
            return ExitCode::Success;
        }
        if (effective_cmd.mode == CommandMode::Install) {
            const std::filesystem::path package_destination =
                active_paths.program_files / installation_id;
            const auto extracted = rust_msix_backend
                                       ? package::extract_msix_package(
                                             *effective_cmd.executable_path, package_destination,
                                             *package_info)
                                       : package::extract_msix_package(
                                             *effective_cmd.executable_path, package_destination);
            if (!extracted.has_value()) {
                write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                    diagnostics::TraceLevel::Error, "failed",
                                    {{"stage", "package-extract"}, {"prefix", prefix_dir.string()},
                                     {"app-id", installation_id}});
                stderr_stream << "erro: não foi possível extrair o pacote MSIX / AppX com segurança\n";
                return ExitCode::MalformedPe;
            }
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Info, "extracted",
                                {{"prefix", prefix_dir.string()}, {"app-id", installation_id},
                                 {"path", extracted->string()}});
            if (!is_x64_pe_file(*extracted)) {
                write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                    diagnostics::TraceLevel::Error, "failed",
                                    {{"stage", "package-executable"}, {"prefix", prefix_dir.string()},
                                     {"app-id", installation_id}});
                stderr_stream << "erro: o executável interno do pacote não é PE32+ x86-64\n";
                return ExitCode::Unsupported;
            }

            catalog::AppCatalog app_catalog;
            (void)app_catalog.load_from_file();
            catalog::AppEntry entry;
            entry.id = installation_id;
            entry.name = installation_name;
            entry.executable_path = extracted->string();
            entry.prefix_path = prefix_dir.string();
            entry.working_directory = extracted->parent_path().string();
            record_executable_fingerprint(entry, *extracted);
            entry.cpu_limit_seconds = effective_cmd.cpu_limit_seconds;
            entry.memory_limit_mib = effective_cmd.memory_limit_mib;
            if (!app_catalog.add_app(entry) || !app_catalog.save_to_file()) {
                write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                    diagnostics::TraceLevel::Error, "failed",
                                    {{"stage", "catalog"}, {"prefix", prefix_dir.string()},
                                     {"app-id", installation_id}});
                stderr_stream << "erro: pacote extraído, mas não foi possível salvar o catálogo\n";
                return ExitCode::InternalError;
            }
            (void)catalog::AppCatalog::create_desktop_entry(entry);
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Info, "registered",
                                {{"prefix", prefix_dir.string()}, {"app-id", entry.id},
                                 {"path", entry.executable_path}});
            stderr_stream << "instalação concluída; aplicativo registrado como '" << entry.name
                          << "' [id: " << entry.id << "]\n";
            return ExitCode::Success;
        }
        stderr_stream << "erro: formato de pacote MSIX / AppX reconhecido; use 'install' ou especifique o executável interno (.exe)\n";
        return ExitCode::Unsupported;
    }

    const std::optional<std::vector<std::byte>> bytes = read_file(*effective_cmd.executable_path);
    if (!bytes.has_value()) {
        stderr_stream << "erro: não foi possível ler o arquivo: "
                      << effective_cmd.executable_path->string() << '\n';
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "input"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        return ExitCode::InputUnavailable;
    }

    const PeParserBackend pe_backend =
        select_pe_parser_backend(effective_cmd, compatibility_profile);
    const bool rust_pe_backend = pe_backend == PeParserBackend::Rust;
    pe::ParseResult parse_result;
    bool rust_internal_failure = false;
#if defined(TRADUTORLINUX_RUST_PE_PARSER)
    tl_pe_error_v1 rust_error{};
    if (rust_pe_backend) {
        const pe::RustPeParseResult rust_result = pe::parse_pe_rust(*bytes);
        rust_internal_failure = rust_result.internal_failure;
        rust_error = rust_result.error;
        parse_result.status = rust_result.status;
        parse_result.error_message = rust_result.error_message;
        parse_result.info = rust_result.info;
    } else {
        parse_result = pe::parse_pe(*bytes);
    }
#else
    parse_result = pe::parse_pe(*bytes);
#endif
    if (parse_result.status != pe::ParseStatus::Success) {
        if (effective_cmd.mode == CommandMode::Install) {
            const auto target_prog_dir = prefix_dir / "drive_c" / "Program Files" / installation_name;
            std::error_code ec;
            std::filesystem::create_directories(target_prog_dir, ec);
            if (extract_archive_with_7z(*effective_cmd.executable_path, target_prog_dir)) {
                // Instaladores NSIS descompactados trazem langs.model.xml e stylers.model.xml.
                // Na primeira execução eles viram langs.xml e stylers.xml.
                for (const auto& entry_it : std::filesystem::recursive_directory_iterator(target_prog_dir, ec)) {
                    if (entry_it.is_regular_file()) {
                        const auto p = entry_it.path();
                        if (p.filename() == "langs.model.xml") {
                            const auto dst = p.parent_path() / "langs.xml";
                            if (!std::filesystem::exists(dst, ec)) std::filesystem::copy_file(p, dst, ec);
                        } else if (p.filename() == "stylers.model.xml") {
                            const auto dst = p.parent_path() / "stylers.xml";
                            if (!std::filesystem::exists(dst, ec)) std::filesystem::copy_file(p, dst, ec);
                        }
                    }
                }
                const prefix::EnvironmentPaths inst_paths = prefix::get_environment_paths(prefix_dir);
                const ExecutableSnapshot after = snapshot_executables(inst_paths.drive_c);
                const std::vector<std::filesystem::path> candidates = changed_executables(
                    installation_before.value_or(ExecutableSnapshot{}), after);
                if (!candidates.empty()) {
                    std::filesystem::path best_candidate;
                    for (const auto& cand : candidates) {
                        const auto cand_bytes = read_file(cand);
                        if (cand_bytes) {
                            const auto cand_pe = pe::parse_pe(*cand_bytes);
                            if (cand_pe.status == pe::ParseStatus::Success) {
                                best_candidate = cand;
                                break;
                            }
                        }
                    }
                    if (best_candidate.empty()) {
                        best_candidate = candidates.front();
                    }
                    catalog::AppCatalog app_catalog;
                    (void)app_catalog.load_from_file();
                    catalog::AppEntry entry;
                    entry.id = installation_id;
                    entry.name = installation_name;
                    entry.executable_path = best_candidate.string();
                    entry.prefix_path = prefix_dir.string();
                    entry.working_directory = best_candidate.parent_path().string();
                    record_executable_fingerprint(entry, best_candidate);
                    entry.cpu_limit_seconds = effective_cmd.cpu_limit_seconds;
                    entry.memory_limit_mib = effective_cmd.memory_limit_mib;
                    if (app_catalog.add_app(entry) && app_catalog.save_to_file()) {
                        write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                            diagnostics::TraceLevel::Info, "registered",
                                            {{"prefix", prefix_dir.string()}, {"app-id", entry.id},
                                             {"path", entry.executable_path}});
                        stderr_stream << "instalação concluída; aplicativo registrado como '" << entry.name
                                      << "' [id: " << entry.id << "]\n";
                        return ExitCode::Success;
                    }
                }
            }
        }
        if (effective_cmd.trace_enabled) {
#if defined(TRADUTORLINUX_RUST_PE_PARSER)
            if (rust_pe_backend) {
                const std::array fields{
                    diagnostics::TraceField{"status", rust_internal_failure
                                                       ? "internal"
                                                       : status_label(parse_result.status)},
                    diagnostics::TraceField{"detail", parse_result.error_message},
                    diagnostics::TraceField{"backend", "rust"},
                    diagnostics::TraceField{"code", std::to_string(rust_error.code)},
                    diagnostics::TraceField{"phase", std::to_string(rust_error.phase)},
                    diagnostics::TraceField{"input-offset", std::to_string(rust_error.input_offset)},
                    diagnostics::TraceField{"detail-value", std::to_string(rust_error.detail_value)},
                };
                diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Pe,
                                         diagnostics::TraceLevel::Error, "parse-failed", fields);
            } else {
#endif
            const std::array fields{
                diagnostics::TraceField{"status", status_label(parse_result.status)},
                diagnostics::TraceField{"detail", parse_result.error_message},
            };
            diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Pe,
                                     diagnostics::TraceLevel::Error, "parse-failed", fields);
#if defined(TRADUTORLINUX_RUST_PE_PARSER)
            }
#endif
        }
        stderr_stream << "erro: " << parse_result.error_message << '\n';
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "parse"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        if (rust_internal_failure) {
            return ExitCode::InternalError;
        }
        if (parse_result.status == pe::ParseStatus::UnsupportedArchitecture ||
            parse_result.status == pe::ParseStatus::UnsupportedFormat ||
            parse_result.status == pe::ParseStatus::UnsupportedMechanism) {
            return ExitCode::Unsupported;
        }
        return ExitCode::MalformedPe;
    }

    if (effective_cmd.trace_enabled) {
        write_pe_trace(stderr_stream, parse_result.info,
                       rust_pe_backend ? std::string_view{"rust"} : std::string_view{});
    } else {
        print_pe_summary(stderr_stream, parse_result.info);
    }

    loader::register_builtin_modules();

    if (effective_cmd.report_only) {
        const loader::ResolveResult report_result =
            print_support_report(stdout_stream, parse_result.info);
        return report_result.status == loader::ImportStatus::Resolved ? ExitCode::Success
                                                                       : ExitCode::Unsupported;
    }

    if (effective_cmd.mode == CommandMode::AppRun && compatibility_profile.has_value() &&
        compatibility_profile->backend.kind == compat::BackendKind::Proton) {
        const backend::ProtonConfigResult config = backend::load_config();
        if (config.status != backend::ConfigStatus::Loaded || !config.config.has_value()) {
            const std::string detail = config.error.empty()
                                           ? "configuração do Proton ausente"
                                           : config.error;
            if (effective_cmd.trace_enabled) {
                const std::array fields{
                    diagnostics::TraceField{"status", "unavailable"},
                    diagnostics::TraceField{"app-id", compatibility_app_id},
                    diagnostics::TraceField{"detail", detail},
                };
                diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Proton,
                                         diagnostics::TraceLevel::Warning,
                                         "provider-rejected", fields);
            }
            stderr_stream << "erro: backend Proton indisponível: " << detail << '\n';
            return ExitCode::Unsupported;
        }

        backend::ProtonRunRequest request;
        request.prefix_root = prefix_dir;
        request.executable = *effective_cmd.executable_path;
        request.working_directory = *effective_cmd.guest_working_directory;
        request.app_id = compatibility_app_id;
        request.guest_arguments = effective_cmd.guest_arguments;
        request.timeout_ms = effective_cmd.timeout_ms;
        request.resource_limits = process::ResourceLimits{
            effective_cmd.cpu_limit_set ? effective_cmd.cpu_limit_seconds : 0,
            effective_cmd.memory_limit_set ? effective_cmd.memory_limit_mib : 0,
        };
        request.trace_enabled = effective_cmd.trace_enabled;
        const backend::ProtonRunResult result = backend::run_proton_application(
            *config.config, *compatibility_profile, request, stderr_stream);
        if (result.status == backend::ProtonRunStatus::Unsupported) {
            stderr_stream << "erro: backend Proton não suportado: " << result.error << '\n';
            return ExitCode::Unsupported;
        }
        if (result.status == backend::ProtonRunStatus::InternalError) {
            stderr_stream << "erro: falha interna ao preparar o backend Proton: "
                          << result.error << '\n';
            return ExitCode::InternalError;
        }
        return finish_proton_outcome(result, stderr_stream);
    }

    execution_context.module_graph = std::make_unique<loader::GuestModuleGraph>(
        prefix_dir, compatibility_profile, *effective_cmd.executable_path,
        effective_cmd.trace_enabled);
    loader::PrepareResult prepare_result = loader::prepare_process(
        parse_result.info, *bytes, execution_context.module_graph.get(),
        *effective_cmd.executable_path);
    if (prepare_result.status == loader::PrepareStatus::OutOfMemory) {
        if (effective_cmd.trace_enabled) {
            write_map_failed_trace(stderr_stream, "out-of-memory", prepare_result.error_message);
        }
        stderr_stream << "erro: " << prepare_result.error_message << '\n';
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "prepare"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        return ExitCode::InternalError;
    }
    if (prepare_result.status == loader::PrepareStatus::InvalidImage) {
        if (effective_cmd.trace_enabled) {
            write_map_failed_trace(stderr_stream, "invalid-image", prepare_result.error_message);
        }
        stderr_stream << "erro: " << prepare_result.error_message << '\n';
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "prepare"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        return ExitCode::MalformedPe;
    }

    loader::GuestProcess& process = prepare_result.process;
    if (execution_context.module_graph != nullptr) {
        execution_context.module_graph->bind_main_image(
            process.image, process.info, *effective_cmd.executable_path);
    }
    if (effective_cmd.trace_enabled) {
        write_map_trace(stderr_stream, process.image);
    } else {
        print_map_summary(stderr_stream, process.image);
    }
    if (effective_cmd.trace_enabled) {
        write_imports_trace(stderr_stream, process.imports);
    } else {
        print_imports_summary(stderr_stream, process.imports);
    }

    if (prepare_result.status == loader::PrepareStatus::UnresolvedImports) {
        if (!effective_cmd.trace_enabled) {
            stderr_stream << "erro: importações não resolvidas: "
                          << prepare_result.error_message << '\n';
        }
        const std::uint64_t unmap_base = process.image.base;
        loader::destroy_process(process);
        if (effective_cmd.trace_enabled) {
            write_unmap_trace(stderr_stream, unmap_base);
        }
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "imports"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        return ExitCode::Unsupported;
    }

    if (process.image.delta != 0 && !process.image.has_relocation_directory) {
        const std::uint64_t unmap_base = process.image.base;
        if (effective_cmd.trace_enabled) {
            const std::array fields{
                diagnostics::TraceField{"reason", "imagem sem diretório de relocations"},
                diagnostics::TraceField{"delta", util::format_signed_hex(process.image.delta)},
            };
            diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Loader,
                                     diagnostics::TraceLevel::Error, "execution-rejected", fields);
        } else {
            stderr_stream << "erro: imagem sem relocations não pode ser executada fora da base preferencial\n";
        }
        loader::destroy_process(process);
        if (effective_cmd.trace_enabled) {
            write_unmap_trace(stderr_stream, unmap_base);
        }
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "relocations"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        return ExitCode::Unsupported;
    }

    if (compatibility_profile.has_value()) {
        compatibility_files.emplace(
            compat::FileExposure::materialize(prefix_dir, *compatibility_profile));
        const auto exposure_status = [](const compat::FileExposureStatus status) {
            switch (status) {
                case compat::FileExposureStatus::Rejected: return std::string{"rejected"};
                case compat::FileExposureStatus::Applied: return std::string{"applied"};
                case compat::FileExposureStatus::RollbackFailed:
                    return std::string{"rollback-failed"};
                case compat::FileExposureStatus::InternalError:
                    return std::string{"internal-error"};
            }
            return std::string{"unknown"};
        };
        if (effective_cmd.trace_enabled) {
            const std::array fields{
                diagnostics::TraceField{"status", exposure_status(compatibility_files->status())},
                diagnostics::TraceField{"prefix", prefix_dir.string()},
                diagnostics::TraceField{"app-id", compatibility_app_id},
                diagnostics::TraceField{"files",
                                        std::to_string(compatibility_files->files().size())},
                diagnostics::TraceField{"detail",
                                        std::string{compatibility_files->error()}},
            };
            diagnostics::write_trace(
                stderr_stream, diagnostics::TraceComponent::Runtime,
                compatibility_files->rollback_failed()
                    ? diagnostics::TraceLevel::Error
                    : compatibility_files->internal_error()
                          ? diagnostics::TraceLevel::Error
                          : compatibility_files->applied()
                                ? diagnostics::TraceLevel::Info
                                : diagnostics::TraceLevel::Warning,
                "compat-files", fields);
        }
        write_path_validation_trace(
            stderr_stream, effective_cmd.trace_enabled, diagnostics::TraceComponent::Runtime,
            "files", compatibility_files->path_validation(),
            compatibility_files->internal_error()
                ? "internal-error"
                : compatibility_files->path_validation().rejected != 0U ? "invalid-input"
                                                                          : "completed",
            compatibility_files->error());
        if (compatibility_files->rollback_failed()) {
            const std::uint64_t unmap_base = process.image.base;
            loader::destroy_process(process);
            if (effective_cmd.trace_enabled) {
                write_unmap_trace(stderr_stream, unmap_base);
            }
            stderr_stream << "erro: não foi possível desfazer a exposição dos arquivos de compatibilidade\n";
            return ExitCode::InternalError;
        }
        if (compatibility_files->internal_error()) {
            const std::uint64_t unmap_base = process.image.base;
            loader::destroy_process(process);
            if (effective_cmd.trace_enabled) {
                write_unmap_trace(stderr_stream, unmap_base);
            }
            stderr_stream << "erro: falha interna ao validar os arquivos de compatibilidade";
            if (!compatibility_files->error().empty()) {
                stderr_stream << ": " << compatibility_files->error();
            }
            stderr_stream << '\n';
            return ExitCode::InternalError;
        }
        if (!compatibility_files->applied()) {
            stderr_stream << "aviso: arquivos do perfil não foram expostos; usando comportamento genérico";
            if (!compatibility_files->error().empty()) {
                stderr_stream << ": " << compatibility_files->error();
            }
            stderr_stream << '\n';
        } else if (effective_cmd.trace_enabled) {
            for (std::size_t index = 0; index < compatibility_profile->files.size(); ++index) {
                const auto& mapping = compatibility_profile->files[index];
                const std::array fields{
                    diagnostics::TraceField{"status", "copied"},
                    diagnostics::TraceField{"source", mapping.source.generic_string()},
                    diagnostics::TraceField{"target", mapping.target},
                };
                diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Runtime,
                                         diagnostics::TraceLevel::Info, "compat-file", fields);
            }
        }
    }

    std::vector<std::string> guest_argv;
    guest_argv.reserve(1 + effective_cmd.guest_arguments.size());
    guest_argv.push_back(prefix::to_windows_path(*effective_cmd.executable_path, prefix_dir));
    guest_argv.insert(guest_argv.end(), effective_cmd.guest_arguments.begin(),
                      effective_cmd.guest_arguments.end());
    msvcrt_set_guest_command_line(std::move(guest_argv));
    set_guest_prefix_path(prefix_dir);
    set_guest_module_path(effective_cmd.executable_path->c_str());
    set_guest_image_view(process.image.memory, process.image.size,
                         parse_result.info.resource_directory_rva,
                         parse_result.info.resource_directory_size);
    runtime::set_guest_unwind_view(process.image.memory, process.image.size,
                                   parse_result.info.exception_directory_rva,
                                   process.info.runtime_functions);
    set_guest_tls_directory(
        relocate_tls_va(parse_result.info.tls_info.start_address_of_raw_data,
                        parse_result.info, process.image),
        relocate_tls_va(parse_result.info.tls_info.end_address_of_raw_data,
                        parse_result.info, process.image),
        relocate_tls_va(parse_result.info.tls_info.address_of_index,
                        parse_result.info, process.image),
        parse_result.info.tls_info.size_of_zero_fill,
        relocated_tls_callbacks(parse_result.info, process.image));

    const process::ResourceLimits resource_limits{
        effective_cmd.cpu_limit_set ? effective_cmd.cpu_limit_seconds : 0,
        effective_cmd.memory_limit_set ? effective_cmd.memory_limit_mib : 0,
    };
    if (resource_limits.cpu_seconds != 0 || resource_limits.memory_mib != 0) {
        std::vector<diagnostics::TraceField> fields;
        fields.reserve(3);
        fields.emplace_back("inheritance", "fork");
        if (resource_limits.cpu_seconds != 0) {
            fields.emplace_back("cpu-seconds", std::to_string(resource_limits.cpu_seconds));
        }
        if (resource_limits.memory_mib != 0) {
            fields.emplace_back("memory-mib", std::to_string(resource_limits.memory_mib));
        }
        diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Process,
                                 diagnostics::TraceLevel::Info, "resource-limits", fields);
    }
    const process::GuestOutcome outcome = process::run_guest_isolated(
        process.thread.entry_point, process.thread.stack_top, effective_cmd.timeout_ms,
        resource_limits,
        *effective_cmd.guest_working_directory);

    if (compatibility_files.has_value() && compatibility_files->applied()) {
        const bool cleanup_ok = compatibility_files->cleanup();
        const auto& cleanup = compatibility_files->cleanup_summary();
        if (effective_cmd.trace_enabled) {
            const std::array fields{
                diagnostics::TraceField{"status", cleanup_ok ? "cleaned" : "cleanup-failed"},
                diagnostics::TraceField{"prefix", prefix_dir.string()},
                diagnostics::TraceField{"app-id", compatibility_app_id},
                diagnostics::TraceField{"removed-files",
                                        std::to_string(cleanup.files_removed)},
                diagnostics::TraceField{"removed-directories",
                                        std::to_string(cleanup.directories_removed)},
                diagnostics::TraceField{"retained",
                                        std::to_string(cleanup.paths_retained)},
            };
            diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Runtime,
                                     cleanup_ok ? diagnostics::TraceLevel::Info
                                                : diagnostics::TraceLevel::Warning,
                                     "compat-files-cleanup", fields);
        }
        if (!cleanup_ok) {
            stderr_stream << "aviso: não foi possível limpar completamente os arquivos de compatibilidade\n";
        }
    }

    // Contexto de falha calculado antes do destroy_process: o evento
    // guest-signal usa a imagem mapeada e as importações ainda vivas.
    const diagnostics::GuestCrashContext crash_context =
        outcome.kind == process::GuestOutcomeKind::Signaled && outcome.fault_recorded
            ? diagnostics::describe_guest_crash(process.image, process.imports,
                                                outcome.fault_rip != 0 ? outcome.fault_rip : outcome.fault_address)
            : diagnostics::GuestCrashContext{};

    const std::uint64_t unmap_base = process.image.base;
    set_guest_tls_directory(0, 0, 0, 0, {});
    runtime::clear_guest_unwind_view();
    set_guest_image_view(nullptr, 0, 0, 0);
    loader::destroy_process(process);
    set_guest_prefix_path({});

    if (outcome.kind == process::GuestOutcomeKind::Exited) {
        if (effective_cmd.trace_enabled) {
            const std::array fields{
                diagnostics::TraceField{"exit-code", std::to_string(outcome.exit_code)},
                diagnostics::TraceField{"explicit", outcome.exited_explicitly ? "sim" : "não"},
            };
            diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Process,
                                     diagnostics::TraceLevel::Info, "exit", fields);
            write_unmap_trace(stderr_stream, unmap_base);
        }
        if (effective_cmd.mode != CommandMode::Install) {
            return static_cast<ExitCode>(outcome.exit_code);
        }

        if (outcome.exit_code != 0U) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "setup"},
                                 {"exit-code", std::to_string(outcome.exit_code)},
                                 {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
            return static_cast<ExitCode>(outcome.exit_code);
        }

        const prefix::EnvironmentPaths installation_paths =
            prefix::get_environment_paths(prefix_dir);
        std::optional<std::filesystem::path> selected_executable;
        if (effective_cmd.installed_executable_path.has_value()) {
            selected_executable = resolve_installed_executable(
                *effective_cmd.installed_executable_path, prefix_dir);
            if (!selected_executable.has_value()) {
                write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                    diagnostics::TraceLevel::Warning, "pending",
                                    {{"reason", "invalid-app-exe"},
                                     {"prefix", prefix_dir.string()},
                                     {"app-id", installation_id}});
                stderr_stream << "instalação concluída, mas --app-exe não aponta para um PE32+ x64 dentro do prefixo\n";
                return ExitCode::InstallPending;
            }
        } else {
            const ExecutableSnapshot after = snapshot_executables(installation_paths.drive_c);
            const std::vector<std::filesystem::path> candidates = changed_executables(
                installation_before.value_or(ExecutableSnapshot{}), after);
            for (const std::filesystem::path& candidate : candidates) {
                write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                    diagnostics::TraceLevel::Info, "candidate",
                                    {{"path", candidate.string()},
                                     {"prefix", prefix_dir.string()},
                                     {"app-id", installation_id}});
            }
            if (candidates.size() == 1U) {
                selected_executable = candidates.front();
            } else {
                const std::string reason = candidates.empty() ? "no-candidate" : "selection-required";
                write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                    diagnostics::TraceLevel::Warning, "pending",
                                    {{"reason", reason}, {"prefix", prefix_dir.string()},
                                     {"app-id", installation_id}});
                stderr_stream << "instalação concluída; cadastro pendente (" << reason << ").\n";
                return ExitCode::InstallPending;
            }
        }

        catalog::AppCatalog app_catalog;
        (void)app_catalog.load_from_file();
        catalog::AppEntry entry;
        entry.id = installation_id;
        entry.name = installation_name;
        entry.executable_path = selected_executable->string();
        entry.prefix_path = prefix_dir.string();
        entry.working_directory = selected_executable->parent_path().string();
        record_executable_fingerprint(entry, *selected_executable);
        entry.cpu_limit_seconds = effective_cmd.cpu_limit_seconds;
        entry.memory_limit_mib = effective_cmd.memory_limit_mib;
        if (!app_catalog.add_app(entry) || !app_catalog.save_to_file()) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "catalog"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
            stderr_stream << "erro: instalação concluída, mas não foi possível salvar o catálogo\n";
            return ExitCode::InternalError;
        }
        (void)catalog::AppCatalog::create_desktop_entry(entry);
        write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                            diagnostics::TraceLevel::Info, "registered",
                            {{"prefix", prefix_dir.string()}, {"app-id", entry.id},
                             {"path", entry.executable_path}});
        stderr_stream << "instalação concluída; aplicativo registrado como '" << entry.name
                      << "' [id: " << entry.id << "]\n";
        return ExitCode::Success;
    }

    if (outcome.kind == process::GuestOutcomeKind::ResourceSetupFailed) {
        const std::string resource{process::resource_limit_name(outcome.resource)};
        if (effective_cmd.trace_enabled) {
            const std::array fields{
                diagnostics::TraceField{"category",
                                        std::string{diagnostics::failure_category_name(
                                            diagnostics::FailureCategory::InternalError)}},
                diagnostics::TraceField{"resource", resource},
                diagnostics::TraceField{"detail",
                                        "não foi possível instalar o limite no processo filho"},
            };
            diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Process,
                                     diagnostics::TraceLevel::Error,
                                     "resource-limit-setup-failed", fields);
            write_unmap_trace(stderr_stream, unmap_base);
        } else {
            stderr_stream << "erro: não foi possível instalar o limite de recurso no processo filho ("
                          << resource << ")\n";
        }
        return ExitCode::InternalError;
    }

    if (outcome.kind == process::GuestOutcomeKind::ResourceLimited) {
        const process::SignalDescription signal = process::describe_signal(outcome.signal_number);
        const std::string resource{process::resource_limit_name(outcome.resource)};
        if (effective_cmd.trace_enabled) {
            std::vector<diagnostics::TraceField> fields;
            fields.reserve(5);
            fields.emplace_back(
                "category",
                std::string{diagnostics::failure_category_name(
                    diagnostics::FailureCategory::GuestResourceLimit)});
            fields.emplace_back("resource", resource);
            fields.emplace_back("signal", std::string{signal.name});
            if (outcome.resource == process::ResourceLimitKind::Cpu) {
                fields.emplace_back("limit-seconds",
                                    std::to_string(resource_limits.cpu_seconds));
            }
            diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Process,
                                     diagnostics::TraceLevel::Error, "terminated", fields);
            write_unmap_trace(stderr_stream, unmap_base);
        } else {
            stderr_stream << "erro: o programa convidado excedeu o limite de " << resource << '\n';
        }
        return ExitCode::GuestResourceLimit;
    }

    if (outcome.kind == process::GuestOutcomeKind::Signaled) {
        const process::SignalDescription signal = process::describe_signal(outcome.signal_number);
        if (effective_cmd.trace_enabled) {
            std::vector<diagnostics::TraceField> fields;
            fields.reserve(7);
            fields.push_back(diagnostics::TraceField{
                "category",
                std::string{diagnostics::failure_category_name(
                    diagnostics::FailureCategory::GuestSignal)}});
            fields.push_back(diagnostics::TraceField{"signal", std::string{signal.name}});
            fields.push_back(diagnostics::TraceField{"detail", std::string{signal.detail}});
            if (outcome.fault_recorded) {
                fields.push_back(diagnostics::TraceField{
                    "fault-address", util::format_hex(outcome.fault_address)});
                fields.push_back(diagnostics::TraceField{
                    "fault-rip", util::format_hex(outcome.fault_rip)});
            }
            if (crash_context.valid) {
                fields.push_back(
                    diagnostics::TraceField{"rva", util::format_hex(crash_context.rva)});
                if (!crash_context.section.empty()) {
                    fields.push_back(diagnostics::TraceField{
                        "section", std::string{crash_context.section}});
                }
                if (!crash_context.nearest_import.empty()) {
                    fields.push_back(diagnostics::TraceField{
                        "nearest-import", crash_context.nearest_import});
                }
            }
            diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Process,
                                     diagnostics::TraceLevel::Error, "terminated", fields);
            write_unmap_trace(stderr_stream, unmap_base);
        } else {
            stderr_stream << "erro: o programa convidado terminou por sinal " << signal.name
                          << " (" << signal.detail << ")";
            if (outcome.fault_recorded) {
                stderr_stream << " no endereço " << util::format_hex(outcome.fault_address);
                if (crash_context.valid) {
                    stderr_stream << " (rva " << util::format_hex(crash_context.rva);
                    if (!crash_context.section.empty()) {
                        stderr_stream << ", seção " << crash_context.section;
                    }
                    if (!crash_context.nearest_import.empty()) {
                        stderr_stream << ", importação mais próxima "
                                      << crash_context.nearest_import;
                    }
                    stderr_stream << ")";
                }
            }
            stderr_stream << '\n';
        }
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "setup-signal"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        return ExitCode::GuestFault;
    }

    if (outcome.kind == process::GuestOutcomeKind::TimedOut) {
        if (effective_cmd.trace_enabled) {
            const std::array fields{
                diagnostics::TraceField{"category",
                                        std::string{diagnostics::failure_category_name(
                                            diagnostics::FailureCategory::GuestTimeout)}},
                diagnostics::TraceField{"timeout-ms", std::to_string(effective_cmd.timeout_ms)},
            };
            diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Process,
                                     diagnostics::TraceLevel::Error, "terminated", fields);
            write_unmap_trace(stderr_stream, unmap_base);
        } else {
            stderr_stream << "erro: o programa convidado não terminou dentro de "
                          << effective_cmd.timeout_ms << " ms\n";
        }
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "setup-timeout"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        return ExitCode::GuestTimeout;
    }

    if (effective_cmd.trace_enabled) {
        const std::array fields{
            diagnostics::TraceField{
                "category",
                std::string{diagnostics::failure_category_name(
                    diagnostics::FailureCategory::InternalError)}},
            diagnostics::TraceField{"detail",
                                    "não foi possível criar o processo filho do convidado"},
        };
        diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Process,
                                 diagnostics::TraceLevel::Error, "terminated", fields);
        write_unmap_trace(stderr_stream, unmap_base);
    } else {
        stderr_stream << "erro: não foi possível criar o processo filho do convidado\n";
    }
    if (effective_cmd.mode == CommandMode::Install) {
        write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                            diagnostics::TraceLevel::Error, "failed",
                            {{"stage", "setup-spawn"}, {"prefix", prefix_dir.string()},
                             {"app-id", installation_id}});
    }
    return ExitCode::InternalError;
}

}  // namespace tradutorlinux
