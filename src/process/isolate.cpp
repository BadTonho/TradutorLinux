#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "tradutorlinux/process/isolate.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/runtime/guest_context.hpp"
#include "tradutorlinux/runtime/winapi.hpp"

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <limits>
#include <poll.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <fcntl.h>
#include <net/if.h>
#include <ostream>
#include <sched.h>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <vector>

extern char** environ;

namespace tradutorlinux::process {
namespace {

constexpr std::size_t kProtocolSize = 7;  // [kind:1][resource:1][explicit:1][exit-code:4 LE]

enum class ChildMessageKind : unsigned char {
    Exited = 0,
    ResourceSetupFailed = 1,
    NetworkSetupFailed = 2,
};

// Registro de falha escrito pelo handler de sinais do filho quando o convidado
// morre por sinal fatal: [signal:1][si_addr:8 LE][rip:8 LE].
constexpr std::size_t kFaultRecordSize = 17;

// Descritor do pipe de falha usado pelo handler no filho. Handlers de sinal
// não podem capturar estado, então o fd é publicado aqui antes da instalação;
// só existe no processo filho e vale enquanto o convidado executa. Deve ser
// volátil e sig_atomic_t para acesso async-signal-safe dentro do handler.
volatile std::sig_atomic_t g_crash_report_fd = -1;

// Sinais fatais tratados pelo reporter. Precisa estar no escopo do arquivo
// (não local da função) para o handler restaurar TODOS de uma vez num segundo
// fault, evitando recursão por um sinal diferente do que disparou.
constexpr std::array<int, 8> kFatalSignals{SIGSEGV, SIGILL, SIGBUS, SIGABRT,
                                           SIGFPE,  SIGTRAP, SIGSYS, SIGQUIT};

bool read_exact(const int fd, std::byte* const buffer, const std::size_t size) noexcept {
    std::size_t total = 0;
    while (total < size) {
        const ::ssize_t count = ::read(fd, buffer + total, size - total);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        total += static_cast<std::size_t>(count);
    }
    return true;
}

bool write_exact(const int fd, const std::byte* const buffer, const std::size_t size) noexcept {
    std::size_t total = 0;
    while (total < size) {
        const ::ssize_t count = ::write(fd, buffer + total, size - total);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        total += static_cast<std::size_t>(count);
    }
    return true;
}

void kill_process_group(const ::pid_t child) noexcept {
    if (child <= 0) {
        return;
    }
    if (::kill(-child, SIGKILL) != 0) {
        static_cast<void>(::kill(child, SIGKILL));
    }
}

// O convidado precisa terminar com a disposição padrão dos sinais fatais para
// que um crash (ex.: SIGSEGV) chegue ao waitpid como sinal real. Runtimes do
// hospedeiro como o AddressSanitizer instalam handlers próprios, que engoliriam
// o sinal e esconderiam o diagnóstico guest-signal.
//
// Em vez de apenas restaurar SIG_DFL, o filho instala um handler mínimo
// (async-signal-safe) que captura o si_addr reportado pelo kernel, publica o
// registro de falha no pipe dedicado e então restaura SIG_DFL e reentrega o
// mesmo sinal. Assim o processo morre exatamente como antes (mesmo status no
// waitpid, mesma interação com sanitizers), mas o pai ganha o endereço da
// falta para converter em contexto PE (RVA, seção, importação mais próxima).
__attribute__((no_instrument_function))
void install_crash_reporter(const int report_fd) noexcept {
    g_crash_report_fd = report_fd;
    struct sigaction action {};
    action.sa_sigaction = [](const int signal_number, siginfo_t* info, void* context_raw) noexcept {
        // Restaura SIG_DFL para TODOS os sinais fatais ANTES de qualquer outra
        // coisa: se algo falhar aqui dentro (inclusive uma nova falta durante o
        // write), o segundo disparo de qualquer um dos sinais já cai na
        // disposição padrão e mata o processo de vez — sem recursão por um
        // sinal diferente do que disparou.
        struct sigaction default_action {};
        default_action.sa_handler = SIG_DFL;
        ::sigemptyset(&default_action.sa_mask);
        for (const int fatal_signal : kFatalSignals) {
            static_cast<void>(::sigaction(fatal_signal, &default_action, nullptr));
        }

        std::array<std::byte, kFaultRecordSize> record{};
        record[0] = std::byte{static_cast<unsigned char>(static_cast<unsigned>(signal_number) & 0xFFU)};
        const auto address = info != nullptr && info->si_addr != nullptr
                                 ? reinterpret_cast<std::uintptr_t>(info->si_addr)
                                 : std::uintptr_t{0};
        std::uintptr_t rip = 0;
#if defined(__x86_64__)
        if (context_raw != nullptr) {
            const auto* const uc = static_cast<const ucontext_t*>(context_raw);
            rip = static_cast<std::uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
        }
#endif
        for (std::size_t index = 0; index < 8; ++index) {
            record[1 + index] =
                std::byte{static_cast<unsigned char>((address >> (8U * index)) & 0xFFU)};
            record[9 + index] =
                std::byte{static_cast<unsigned char>((rip >> (8U * index)) & 0xFFU)};
        }
        // Uma única escrita: registros de 17 bytes são atômicos em pipes. Falhas
        // são ignoradas de propósito — o diagnóstico sem endereço ainda vale,
        // e o handler não pode depender de nada além de syscalls diretas.
        if (g_crash_report_fd >= 0) {
            const ssize_t ignored = ::write(g_crash_report_fd, record.data(), record.size());
            static_cast<void>(ignored);
        }
    };
    action.sa_flags = SA_SIGINFO;
    ::sigemptyset(&action.sa_mask);
    for (const int signal_number : kFatalSignals) {
        static_cast<void>(::sigaction(signal_number, &action, nullptr));
    }
}

// Writes from the guest to a closed pipe (ex.: stdout piped to `head -c0`)
// must surface as EPIPE to WriteFile, not kill the child with SIGPIPE. Ignore
// the signal in the child so the failure is a controlled win32 error.
void ignore_broken_pipe() noexcept {
    struct sigaction action {};
    action.sa_handler = SIG_IGN;
    ::sigemptyset(&action.sa_mask);
    static_cast<void>(::sigaction(SIGPIPE, &action, nullptr));
}

std::uint64_t monotonic_ms() noexcept {
    struct ::timespec now {};
    static_cast<void>(::clock_gettime(CLOCK_MONOTONIC, &now));
    return static_cast<std::uint64_t>(now.tv_sec) * 1000U +
           static_cast<std::uint64_t>(now.tv_nsec) / 1000000U;
}

[[nodiscard]] bool set_cpu_limit(const std::uint64_t seconds) noexcept {
    if (seconds == 0) {
        return true;
    }
    struct ::rlimit current {};
    if (::getrlimit(RLIMIT_CPU, &current) != 0 ||
        seconds > static_cast<std::uint64_t>(RLIM_INFINITY)) {
        return false;
    }
    const rlim_t soft = static_cast<rlim_t>(seconds);
    rlim_t hard = soft;
    if (seconds < static_cast<std::uint64_t>(RLIM_INFINITY)) {
        hard = static_cast<rlim_t>(seconds + 1U);
    }
    if (current.rlim_max != RLIM_INFINITY && hard > current.rlim_max) {
        hard = current.rlim_max;
    }
    if (hard < soft) {
        return false;
    }
    const struct ::rlimit requested{soft, hard};
    return ::setrlimit(RLIMIT_CPU, &requested) == 0;
}

[[nodiscard]] bool set_memory_limit(const std::uint64_t memory_mib) noexcept {
    if (memory_mib == 0) {
        return true;
    }
    constexpr std::uint64_t kMib = 1024U * 1024U;
    if (memory_mib > static_cast<std::uint64_t>(RLIM_INFINITY) / kMib) {
        return false;
    }
    const rlim_t bytes = static_cast<rlim_t>(memory_mib * kMib);
    struct ::rlimit current {};
    if (::getrlimit(RLIMIT_AS, &current) != 0 ||
        (current.rlim_max != RLIM_INFINITY && bytes > current.rlim_max)) {
        return false;
    }
#if defined(TRADUTORLINUX_HOST_SANITIZED)
    // AddressSanitizer reserves a very large shadow mapping before the child
    // is forked.  Lowering RLIMIT_AS below that inherited mapping makes the
    // child fail before guest code starts.  Keep the same guest-facing limit
    // through the runtime's private allocation accounting instead.
    ::tradutorlinux::runtime::guest_context().guest_virtual_memory_limit_bytes =
        static_cast<std::size_t>(bytes);
    return true;
#else
    const struct ::rlimit requested{bytes, bytes};
    return ::setrlimit(RLIMIT_AS, &requested) == 0;
#endif
}

[[nodiscard]] ResourceLimitKind apply_resource_limits(const ResourceLimits& limits) noexcept {
    if (!set_cpu_limit(limits.cpu_seconds)) {
        return ResourceLimitKind::Cpu;
    }
    if (!set_memory_limit(limits.memory_mib)) {
        return ResourceLimitKind::Memory;
    }
    return ResourceLimitKind::None;
}

void write_child_message(const int fd, const ChildMessageKind kind,
                         const ResourceLimitKind resource, const bool explicit_exit,
                         const std::uint32_t exit_code) noexcept {
    const std::array<std::byte, kProtocolSize> message{
        std::byte{static_cast<unsigned char>(kind)},
        std::byte{static_cast<unsigned char>(resource)},
        std::byte{static_cast<unsigned char>(explicit_exit ? 1U : 0U)},
        std::byte{static_cast<unsigned char>(exit_code & 0xFFU)},
        std::byte{static_cast<unsigned char>((exit_code >> 8U) & 0xFFU)},
        std::byte{static_cast<unsigned char>((exit_code >> 16U) & 0xFFU)},
        std::byte{static_cast<unsigned char>((exit_code >> 24U) & 0xFFU)},
    };
    static_cast<void>(write_exact(fd, message.data(), message.size()));
}

}  // namespace

SignalDescription describe_signal(const int signal_number) noexcept {
    switch (signal_number) {
        case SIGSEGV:
            return {"SIGSEGV", "acesso inválido à memória"};
        case SIGILL:
            return {"SIGILL", "instrução ilegal"};
        case SIGBUS:
            return {"SIGBUS", "erro de barramento (endereçamento inválido)"};
        case SIGABRT:
            return {"SIGABRT", "abort"};
        case SIGFPE:
            return {"SIGFPE", "erro de ponto flutuante"};
        case SIGTRAP:
            return {"SIGTRAP", "breakpoint"};
        case SIGSYS:
            return {"SIGSYS", "chamada de sistema inválida"};
        case SIGKILL:
            return {"SIGKILL", "terminado por SIGKILL"};
        case SIGPIPE:
            return {"SIGPIPE", "escrita em pipe sem leitor"};
        case SIGTERM:
            return {"SIGTERM", "terminado por SIGTERM"};
        case SIGQUIT:
            return {"SIGQUIT", "terminado por SIGQUIT"};
        case SIGXCPU:
            return {"SIGXCPU", "limite de CPU excedido"};
        default:
            return {"sinal", "término inesperado por sinal"};
    }
}

std::string_view resource_limit_name(const ResourceLimitKind resource) noexcept {
    switch (resource) {
        case ResourceLimitKind::Cpu:
            return "cpu";
        case ResourceLimitKind::Memory:
            return "memory";
        case ResourceLimitKind::None:
            return "none";
    }
    return "none";
}

std::string_view network_mode_name(const NetworkMode mode) noexcept {
    switch (mode) {
        case NetworkMode::Full:
            return "full";
        case NetworkMode::None:
            return "none";
        case NetworkMode::Loopback:
            return "loopback";
    }
    return "full";
}

bool write_all_raw(const int fd, const char* const data, const std::size_t len) noexcept {
    std::size_t total = 0;
    while (total < len) {
        const ssize_t written = ::write(fd, data + total, len - total);
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (written == 0) return false;
        total += static_cast<std::size_t>(written);
    }
    return true;
}

bool apply_network_isolation(const NetworkMode mode) noexcept {
    if (mode == NetworkMode::Full) {
        return true;
    }

    // Tenta primeiro criar novo network namespace diretamente
    if (::unshare(CLONE_NEWNET) != 0) {
        // Se falhar (ex.: EPERM para processo desprivilegiado), cria também um user namespace
        const uid_t real_uid = ::getuid();
        const gid_t real_gid = ::getgid();

        if (::unshare(CLONE_NEWUSER | CLONE_NEWNET) != 0) {
            return false;
        }

        // Mapeia UID e GID do processo atual para manter acesso a seus arquivos
        const int setgroups_fd = ::open("/proc/self/setgroups", O_WRONLY | O_CLOEXEC);
        if (setgroups_fd >= 0) {
            static_cast<void>(write_all_raw(setgroups_fd, "deny\n", 5));
            ::close(setgroups_fd);
        }

        char map_buf[64];
        const int uid_fd = ::open("/proc/self/uid_map", O_WRONLY | O_CLOEXEC);
        if (uid_fd >= 0) {
            const int len = std::snprintf(map_buf, sizeof(map_buf), "0 %u 1\n",
                                          static_cast<unsigned int>(real_uid));
            if (len > 0) {
                static_cast<void>(write_all_raw(uid_fd, map_buf, static_cast<std::size_t>(len)));
            }
            ::close(uid_fd);
        }

        const int gid_fd = ::open("/proc/self/gid_map", O_WRONLY | O_CLOEXEC);
        if (gid_fd >= 0) {
            const int len = std::snprintf(map_buf, sizeof(map_buf), "0 %u 1\n",
                                          static_cast<unsigned int>(real_gid));
            if (len > 0) {
                static_cast<void>(write_all_raw(gid_fd, map_buf, static_cast<std::size_t>(len)));
            }
            ::close(gid_fd);
        }
    }

    if (mode == NetworkMode::Loopback) {
        // Habilita a interface loopback (lo) no novo namespace
        const int sock = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (sock >= 0) {
            struct ifreq ifr {};
            std::strncpy(ifr.ifr_name, "lo", IFNAMSIZ - 1);
            if (::ioctl(sock, SIOCGIFFLAGS, &ifr) >= 0) {
                ifr.ifr_flags |= static_cast<short>(IFF_UP | IFF_RUNNING);
                static_cast<void>(::ioctl(sock, SIOCSIFFLAGS, &ifr));
            }
            ::close(sock);
        }
    }

    return true;
}

__attribute__((no_instrument_function))
GuestOutcome run_guest_isolated(const std::uintptr_t entry_point,
                                const std::uintptr_t stack_top,
                                const std::uint64_t timeout_ms,
                                const ResourceLimits& resource_limits,
                                const std::filesystem::path& working_directory,
                                const NetworkMode network_mode) noexcept {
    int pipe_fds[2] = {-1, -1};
    if (::pipe2(pipe_fds, O_CLOEXEC) != 0) {
        return {.kind = GuestOutcomeKind::SpawnFailed};
    }
    int fault_fds[2] = {-1, -1};
    if (::pipe2(fault_fds, O_CLOEXEC) != 0) {
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        return {.kind = GuestOutcomeKind::SpawnFailed};
    }

    diagnostics::suspend_trace_json_for_fork();
    const ::pid_t child = ::fork();
    if (child < 0) {
        diagnostics::resume_trace_json_after_fork();
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        ::close(fault_fds[0]);
        ::close(fault_fds[1]);
        return {.kind = GuestOutcomeKind::SpawnFailed};
    }

    if (child == 0) {
        diagnostics::resume_trace_json_after_fork();
        ::close(pipe_fds[0]);
        ::close(fault_fds[0]);
        static_cast<void>(::setpgid(0, 0));
        if (!working_directory.empty() && ::chdir(working_directory.c_str()) != 0) {
            ::close(pipe_fds[1]);
            ::close(fault_fds[1]);
            ::_exit(126);
        }
        if (!apply_network_isolation(network_mode)) {
            write_child_message(pipe_fds[1], ChildMessageKind::NetworkSetupFailed,
                                ResourceLimitKind::None, false, 0);
            ::close(pipe_fds[1]);
            ::close(fault_fds[1]);
            ::_exit(0);
        }
        const ResourceLimitKind resource_setup_failure = apply_resource_limits(resource_limits);
        if (resource_setup_failure != ResourceLimitKind::None) {
            write_child_message(pipe_fds[1], ChildMessageKind::ResourceSetupFailed,
                                resource_setup_failure, false, 0);
            ::close(pipe_fds[1]);
            ::close(fault_fds[1]);
            ::_exit(0);
        }
        install_crash_reporter(fault_fds[1]);
        ignore_broken_pipe();
        const GuestExecutionResult result = execute_guest_entry(entry_point, stack_top);
        const std::array<std::byte, kProtocolSize> message{
            std::byte{static_cast<unsigned char>(ChildMessageKind::Exited)},
            std::byte{static_cast<unsigned char>(ResourceLimitKind::None)},
            std::byte{static_cast<unsigned char>(result.exited_explicitly ? 1U : 0U)},
            std::byte{static_cast<unsigned char>(result.exit_code & 0xFFU)},
            std::byte{static_cast<unsigned char>((result.exit_code >> 8) & 0xFFU)},
            std::byte{static_cast<unsigned char>((result.exit_code >> 16) & 0xFFU)},
            std::byte{static_cast<unsigned char>((result.exit_code >> 24) & 0xFFU)},
        };
        const bool sent = write_exact(pipe_fds[1], message.data(), message.size());
        ::close(pipe_fds[1]);
        diagnostics::disable_trace_json_directory();
        ::_exit(sent ? 0 : 125);
    }

    diagnostics::resume_trace_json_after_fork();
    ::close(pipe_fds[1]);
    ::close(fault_fds[1]);
    // O filho também faz setpgid para fechar a corrida; esta chamada cobre o
    // intervalo em que ele ainda não executou essa instrução.
    static_cast<void>(::setpgid(child, child));

    // Espera o filho com poll no pipe de resultado. O lado de escrita é
    // fechado quando o filho termina (HUP), então o pai não fica preso e pode
    // também aplicar o limite de tempo do convidado (timeout_ms; 0 = ilimitado).
    bool timed_out = false;
    int status = 0;
    const std::uint64_t deadline =
        timeout_ms == 0 ? 0 : monotonic_ms() + timeout_ms;
    while (true) {
        std::uint64_t remaining = 0;
        if (timeout_ms != 0) {
            const std::uint64_t now = monotonic_ms();
            if (now >= deadline) {
                timed_out = true;
                break;
            }
            remaining = deadline - now;
            if (remaining > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                remaining = static_cast<std::uint64_t>(std::numeric_limits<int>::max());
            }
        }
        struct ::pollfd descriptor {};
        descriptor.fd = pipe_fds[0];
        descriptor.events = POLLIN | POLLHUP;
        const int poll_result =
            ::poll(&descriptor, 1, timeout_ms == 0 ? -1 : static_cast<int>(remaining));
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            kill_process_group(child);
            while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            ::close(pipe_fds[0]);
            ::close(fault_fds[0]);
            return {.kind = GuestOutcomeKind::SpawnFailed};
        }
        const ::pid_t waited = ::waitpid(child, &status, WNOHANG);
        if (waited == child) {
            break;
        }
        if (waited < 0 && errno != EINTR) {
            kill_process_group(child);
            while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            ::close(pipe_fds[0]);
            ::close(fault_fds[0]);
            return {.kind = GuestOutcomeKind::SpawnFailed};
        }
    }

    if (timed_out) {
        kill_process_group(child);
        while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
        ::close(pipe_fds[0]);
        ::close(fault_fds[0]);
        return {.kind = GuestOutcomeKind::TimedOut, .signal_number = SIGKILL};
    }

    GuestOutcome outcome{};
    if (WIFEXITED(status)) {
        std::array<std::byte, kProtocolSize> message{};
        if (!read_exact(pipe_fds[0], message.data(), message.size())) {
            kill_process_group(child);
            ::close(pipe_fds[0]);
            ::close(fault_fds[0]);
            return {.kind = GuestOutcomeKind::SpawnFailed};
        }
        if (message[0] == std::byte{static_cast<unsigned char>(ChildMessageKind::ResourceSetupFailed)}) {
            outcome.kind = GuestOutcomeKind::ResourceSetupFailed;
            outcome.resource = static_cast<ResourceLimitKind>(std::to_integer<unsigned>(message[1]));
            ::close(pipe_fds[0]);
            ::close(fault_fds[0]);
            return outcome;
        }
        if (message[0] == std::byte{static_cast<unsigned char>(ChildMessageKind::NetworkSetupFailed)}) {
            outcome.kind = GuestOutcomeKind::NetworkSetupFailed;
            ::close(pipe_fds[0]);
            ::close(fault_fds[0]);
            return outcome;
        }
        if (message[0] != std::byte{static_cast<unsigned char>(ChildMessageKind::Exited)} ||
            message[1] != std::byte{static_cast<unsigned char>(ResourceLimitKind::None)} ||
            (message[2] != std::byte{0} && message[2] != std::byte{1})) {
            kill_process_group(child);
            ::close(pipe_fds[0]);
            ::close(fault_fds[0]);
            return {.kind = GuestOutcomeKind::SpawnFailed};
        }
        outcome.exited_explicitly = message[2] == std::byte{1};
        std::uint32_t code = 0;
        for (std::uint32_t shift = 0; shift < 4; ++shift) {
            const std::uint32_t byte =
                static_cast<std::uint32_t>(static_cast<unsigned char>(message[3 + shift]));
            code |= byte << (8U * shift);
        }
        outcome.exit_code = code;
        outcome.kind = GuestOutcomeKind::Exited;
    } else {
        // Sem WUNTRACED/WCONTINUED o waitpid só relata WIFEXITED ou
        // WIFSIGNALED; qualquer outro caso é término por sinal.
        outcome.signal_number = WTERMSIG(status);
        outcome.kind = outcome.signal_number == SIGXCPU ? GuestOutcomeKind::ResourceLimited
                                                         : GuestOutcomeKind::Signaled;
        if (outcome.kind == GuestOutcomeKind::ResourceLimited) {
            outcome.resource = ResourceLimitKind::Cpu;
        }

        // O handler do filho publica o registro antes de reentregar o sinal,
        // então os dados já estão no buffer do pipe. Se o filho morreu sem
        // passar pelo handler (ex.: SIGKILL externo), o lado de escrita está
        // fechado e read_exact retorna falso sem bloquear.
        std::array<std::byte, kFaultRecordSize> record{};
        if (read_exact(fault_fds[0], record.data(), record.size())) {
            const auto recorded_signal =
                static_cast<unsigned char>(std::to_integer<unsigned>(record[0]));
            if (recorded_signal == static_cast<unsigned char>(outcome.signal_number)) {
                std::uint64_t address = 0;
                std::uint64_t rip = 0;
                for (std::size_t index = 0; index < 8; ++index) {
                    const std::uint64_t byte =
                        static_cast<std::uint64_t>(
                            std::to_integer<unsigned>(record[1 + index]));
                    address |= byte << (8U * index);
                    const std::uint64_t rip_byte =
                        static_cast<std::uint64_t>(
                            std::to_integer<unsigned>(record[9 + index]));
                    rip |= rip_byte << (8U * index);
                }
                outcome.fault_recorded = true;
                outcome.fault_address = address;
                outcome.fault_rip = rip;
            }
        }
    }
    ::close(pipe_fds[0]);
    ::close(fault_fds[0]);
    return outcome;
}

namespace {

enum class ExternalStatus : unsigned char {
    SpawnFailed = 1,
    CpuLimitFailed = 2,
    MemoryLimitFailed = 3,
    WorkingDirectoryFailed = 4,
    NetworkSetupFailed = 5,
};

void write_external_status(const int fd, const ExternalStatus status) noexcept {
    const unsigned char value = static_cast<unsigned char>(status);
    const ssize_t ignored = ::write(fd, &value, sizeof(value));
    static_cast<void>(ignored);
}

[[nodiscard]] std::vector<std::string> inherited_environment(
    const std::vector<ExternalEnvironmentVariable>& overrides) {
    std::vector<std::string> environment;
    for (char** current = ::environ; current != nullptr && *current != nullptr; ++current) {
        environment.emplace_back(*current);
    }
    for (const ExternalEnvironmentVariable& override_value : overrides) {
        const std::string prefix = override_value.name + "=";
        const auto existing = std::find_if(
            environment.begin(), environment.end(), [&prefix](const std::string& entry) {
                return entry.starts_with(prefix);
            });
        const std::string replacement = prefix + override_value.value;
        if (existing == environment.end()) {
            environment.push_back(replacement);
        } else {
            *existing = replacement;
        }
    }
    return environment;
}

void forward_external_stderr(const int fd, std::string& pending,
                             const std::string_view prefix, std::ostream& stream,
                             const bool drain) {
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t count = ::read(fd, buffer.data(), buffer.size());
        if (count > 0) {
            pending.append(buffer.data(), static_cast<std::size_t>(count));
            std::size_t newline = 0;
            while ((newline = pending.find('\n')) != std::string::npos) {
                stream << prefix << pending.substr(0, newline + 1);
                pending.erase(0, newline + 1);
            }
            if (pending.size() >= 64U * 1024U) {
                stream << prefix << pending;
                pending.clear();
            }
            if (!drain) return;
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        return;
    }
}

void close_if_open(int& fd) noexcept {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

}  // namespace

GuestOutcome run_external_isolated(
    const std::vector<std::string>& argv,
    const std::vector<ExternalEnvironmentVariable>& environment_overrides,
    const std::uint64_t timeout_ms,
    const ResourceLimits& resource_limits,
    const std::filesystem::path& working_directory,
    std::ostream& diagnostic_stream,
    const std::string_view diagnostic_prefix,
    const NetworkMode network_mode) {
    if (argv.empty() || argv.front().empty()) {
        return {.kind = GuestOutcomeKind::SpawnFailed};
    }

    const std::vector<std::string> environment = inherited_environment(environment_overrides);
    std::vector<char*> environment_pointers;
    environment_pointers.reserve(environment.size() + 1U);
    for (const std::string& entry : environment) {
        environment_pointers.push_back(const_cast<char*>(entry.c_str()));
    }
    environment_pointers.push_back(nullptr);

    std::vector<char*> arguments;
    arguments.reserve(argv.size() + 1U);
    for (const std::string& argument : argv) {
        arguments.push_back(const_cast<char*>(argument.c_str()));
    }
    arguments.push_back(nullptr);

    int status_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    if (::pipe2(status_pipe, O_CLOEXEC) != 0 || ::pipe2(stderr_pipe, O_CLOEXEC) != 0) {
        close_if_open(status_pipe[0]);
        close_if_open(status_pipe[1]);
        close_if_open(stderr_pipe[0]);
        close_if_open(stderr_pipe[1]);
        return {.kind = GuestOutcomeKind::SpawnFailed};
    }

    diagnostics::suspend_trace_json_for_fork();
    const ::pid_t child = ::fork();
    if (child < 0) {
        diagnostics::resume_trace_json_after_fork();
        close_if_open(status_pipe[0]);
        close_if_open(status_pipe[1]);
        close_if_open(stderr_pipe[0]);
        close_if_open(stderr_pipe[1]);
        return {.kind = GuestOutcomeKind::SpawnFailed};
    }
    if (child == 0) {
        diagnostics::resume_trace_json_after_fork();
        close_if_open(status_pipe[0]);
        close_if_open(stderr_pipe[0]);
        static_cast<void>(::setpgid(0, 0));
        if (!working_directory.empty() && ::chdir(working_directory.c_str()) != 0) {
            write_external_status(status_pipe[1], ExternalStatus::WorkingDirectoryFailed);
            ::_exit(125);
        }
        if (!apply_network_isolation(network_mode)) {
            write_external_status(status_pipe[1], ExternalStatus::NetworkSetupFailed);
            ::_exit(125);
        }
        const ResourceLimitKind resource_failure = apply_resource_limits(resource_limits);
        if (resource_failure != ResourceLimitKind::None) {
            write_external_status(
                status_pipe[1], resource_failure == ResourceLimitKind::Cpu
                                    ? ExternalStatus::CpuLimitFailed
                                    : ExternalStatus::MemoryLimitFailed);
            ::_exit(125);
        }
        if (::dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
            write_external_status(status_pipe[1], ExternalStatus::SpawnFailed);
            ::_exit(125);
        }
        close_if_open(stderr_pipe[1]);
        ::execve(arguments[0], arguments.data(), environment_pointers.data());
        write_external_status(status_pipe[1], ExternalStatus::SpawnFailed);
        ::dprintf(STDERR_FILENO, "falha ao executar launcher externo: %s\\n", std::strerror(errno));
        ::_exit(127);
    }

    diagnostics::resume_trace_json_after_fork();
    close_if_open(status_pipe[1]);
    close_if_open(stderr_pipe[1]);
    static_cast<void>(::setpgid(child, child));
    const int flags = ::fcntl(stderr_pipe[0], F_GETFL, 0);
    if (flags >= 0) static_cast<void>(::fcntl(stderr_pipe[0], F_SETFL, flags | O_NONBLOCK));
    const int status_flags = ::fcntl(status_pipe[0], F_GETFL, 0);
    if (status_flags >= 0) {
        static_cast<void>(::fcntl(status_pipe[0], F_SETFL, status_flags | O_NONBLOCK));
    }

    std::string pending_stderr;
    unsigned char external_status = 0;
    bool status_received = false;
    bool timed_out = false;
    bool child_reaped = false;
    int wait_status = 0;
    const std::uint64_t deadline = timeout_ms == 0 ? 0 : monotonic_ms() + timeout_ms;
    while (!child_reaped) {
        int poll_timeout = -1;
        if (timeout_ms != 0) {
            const std::uint64_t now = monotonic_ms();
            if (now >= deadline) {
                timed_out = true;
                break;
            }
            const std::uint64_t remaining = deadline - now;
            poll_timeout = remaining > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
                               ? std::numeric_limits<int>::max()
                               : static_cast<int>(remaining);
        }
        struct ::pollfd descriptors[2]{
            {status_pipe[0], POLLIN | POLLHUP, 0},
            {stderr_pipe[0], POLLIN | POLLHUP, 0},
        };
        const int poll_result = ::poll(descriptors, 2, poll_timeout);
        if (poll_result < 0 && errno != EINTR) {
            timed_out = false;
            kill_process_group(child);
            break;
        }
        if (poll_result > 0) {
            if ((descriptors[0].revents & (POLLIN | POLLHUP)) != 0) {
                unsigned char value = 0;
                const ssize_t count = ::read(status_pipe[0], &value, sizeof(value));
                if (count == 1) {
                    external_status = value;
                    status_received = true;
                }
            }
            if ((descriptors[1].revents & (POLLIN | POLLHUP)) != 0) {
                forward_external_stderr(stderr_pipe[0], pending_stderr, diagnostic_prefix,
                                        diagnostic_stream, false);
            }
        }
        const ::pid_t waited = ::waitpid(child, &wait_status, WNOHANG);
        if (waited == child) {
            child_reaped = true;
        } else if (waited < 0 && errno != EINTR) {
            kill_process_group(child);
            break;
        }
    }

    if (!child_reaped) {
        if (timed_out) kill_process_group(child);
        while (::waitpid(child, &wait_status, 0) < 0 && errno == EINTR) {
        }
    }
    forward_external_stderr(stderr_pipe[0], pending_stderr, diagnostic_prefix,
                            diagnostic_stream, true);
    if (!pending_stderr.empty()) diagnostic_stream << diagnostic_prefix << pending_stderr;
    close_if_open(status_pipe[0]);
    close_if_open(stderr_pipe[0]);

    if (timed_out) return {.kind = GuestOutcomeKind::TimedOut, .signal_number = SIGKILL};
    if (status_received) {
        if (external_status == static_cast<unsigned char>(ExternalStatus::CpuLimitFailed)) {
            return {.kind = GuestOutcomeKind::ResourceSetupFailed,
                    .resource = ResourceLimitKind::Cpu};
        }
        if (external_status == static_cast<unsigned char>(ExternalStatus::MemoryLimitFailed)) {
            return {.kind = GuestOutcomeKind::ResourceSetupFailed,
                    .resource = ResourceLimitKind::Memory};
        }
        if (external_status == static_cast<unsigned char>(ExternalStatus::NetworkSetupFailed)) {
            return {.kind = GuestOutcomeKind::NetworkSetupFailed};
        }
        return {.kind = GuestOutcomeKind::SpawnFailed};
    }

    if (WIFEXITED(wait_status)) {
        return {.kind = GuestOutcomeKind::Exited,
                .exit_code = static_cast<std::uint32_t>(WEXITSTATUS(wait_status))};
    }
    if (WIFSIGNALED(wait_status)) {
        const int signal_number = WTERMSIG(wait_status);
        return {.kind = signal_number == SIGXCPU ? GuestOutcomeKind::ResourceLimited
                                                  : GuestOutcomeKind::Signaled,
                .signal_number = signal_number,
                .resource = signal_number == SIGXCPU ? ResourceLimitKind::Cpu
                                                      : ResourceLimitKind::None};
    }
    return {.kind = GuestOutcomeKind::SpawnFailed};
}

}  // namespace tradutorlinux::process
