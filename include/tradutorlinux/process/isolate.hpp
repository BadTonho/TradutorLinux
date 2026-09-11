#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace tradutorlinux::process {

struct ResourceLimits {
    std::uint64_t cpu_seconds{0};
    std::uint64_t memory_mib{0};
};

enum class ResourceLimitKind {
    None,
    Cpu,
    Memory,
};

enum class NetworkMode {
    Full,      // Acesso irrestrito à rede do host (padrão)
    None,      // Desconectado da rede (novo namespace sem interfaces ativas)
    Loopback,  // Conexão apenas local (interface loopback 127.0.0.1)
};

[[nodiscard]] std::string_view network_mode_name(NetworkMode mode) noexcept;

enum class GuestOutcomeKind {
    Exited,       // o guest retornou do entry point ou chamou ExitProcess
    Signaled,     // o guest terminou por um sinal Linux (ex.: SIGSEGV)
    TimedOut,     // o guest não terminou dentro de timeout_ms e foi morto
    ResourceLimited,       // um limite configurado terminou o guest
    ResourceSetupFailed,   // o limite não pôde ser instalado no filho
    NetworkSetupFailed,    // o isolamento de rede não pôde ser configurado no filho
    SpawnFailed,  // não foi possível criar o processo filho
};

struct GuestOutcome {
    GuestOutcomeKind kind{GuestOutcomeKind::Exited};
    bool exited_explicitly{};
    std::uint32_t exit_code{};
    int signal_number{};
    // Preenchido apenas quando kind == Signaled e o filho alcançou o handler
    // de falha antes de morrer: endereço reportado pelo kernel no si_addr
    // (ex.: endereço acessado num SIGSEGV). O pai converte esse endereço em
    // contexto PE (RVA, seção, importação mais próxima) para o diagnóstico.
    bool fault_recorded{};
    std::uint64_t fault_address{};
    std::uint64_t fault_rip{};
    ResourceLimitKind resource{ResourceLimitKind::None};
};

struct SignalDescription {
    std::string_view name;
    std::string_view detail;
};

[[nodiscard]] SignalDescription describe_signal(int signal_number) noexcept;
[[nodiscard]] std::string_view resource_limit_name(ResourceLimitKind resource) noexcept;

// Executes the guest entry point in a freshly forked child process and waits
// for it. The parent keeps both streams (guest stdout, diagnostics stderr);
// the child only runs the guest and reports the outcome through a pipe before
// _exit(0). When the guest terminates by a signal, the parent observes it via
// waitpid and the caller can publish a controlled guest-signal diagnosis.
// timeout_ms > 0 limits how long the guest may run; on expiry the child is
// SIGKILLed and GuestOutcomeKind::TimedOut is returned (timeout_ms == 0 means
// no limit). Nonzero resource limits are installed in the isolated child
// before the entry point and inherited by its POSIX descendants; they are
// containment controls, not a security sandbox. SIGXCPU from ResourceLimits
// is reported as GuestOutcomeKind::ResourceLimited. Must not be called while
// the process has other running threads.
[[nodiscard]] GuestOutcome run_guest_isolated(std::uintptr_t entry_point,
                                              std::uintptr_t stack_top,
                                              std::uint64_t timeout_ms,
                                              const ResourceLimits& resource_limits = {},
                                              const std::filesystem::path& working_directory = {},
                                              NetworkMode network_mode = NetworkMode::Full) noexcept;

struct ExternalEnvironmentVariable {
    std::string name;
    std::string value;
};

// Executa um launcher externo em um grupo de processos próprio. O stdout é
// herdado sem transformação; o stderr é encaminhado ao stream informado com
// o prefixo escolhido. O launcher não herda o estado do GuestContext.
[[nodiscard]] GuestOutcome run_external_isolated(
    const std::vector<std::string>& argv,
    const std::vector<ExternalEnvironmentVariable>& environment_overrides,
    std::uint64_t timeout_ms,
    const ResourceLimits& resource_limits,
    const std::filesystem::path& working_directory,
    std::ostream& diagnostic_stream,
    std::string_view diagnostic_prefix = "[tl][external] ",
    NetworkMode network_mode = NetworkMode::Full);

}  // namespace tradutorlinux::process
