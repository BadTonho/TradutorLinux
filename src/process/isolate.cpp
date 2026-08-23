#include "tradutorlinux/process/isolate.hpp"

#include "tradutorlinux/runtime/winapi.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <cerrno>
#include <csignal>
#include <limits>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace tradutorlinux::process {
namespace {

constexpr std::size_t kProtocolSize = 5;  // [explicit:1][exit-code:4 LE]

// Registro de falha escrito pelo handler de sinais do filho quando o convidado
// morre por sinal fatal: [signal:1][si_addr:8 LE]. O pai só o consome depois de
// waitpid relatar WIFSIGNALED, e o valida contra o sinal observado.
constexpr std::size_t kFaultRecordSize = 9;

// Descritor do pipe de falha usado pelo handler no filho. Handlers de sinal
// não podem capturar estado, então o fd é publicado aqui antes da instalação;
// só existe no processo filho e vale enquanto o convidado executa.
int g_crash_report_fd = -1;

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
void install_crash_reporter(const int report_fd) noexcept {
    g_crash_report_fd = report_fd;
    struct sigaction action {};
    action.sa_sigaction = [](const int signal_number, siginfo_t* info, void*) noexcept {
        // Restaura SIG_DFL ANTES de qualquer outra coisa: se algo falhar aqui
        // dentro (inclusive uma nova falta durante o write), o segundo
        // disparo já cai na disposição padrão e mata o processo de vez.
        struct sigaction default_action {};
        default_action.sa_handler = SIG_DFL;
        ::sigemptyset(&default_action.sa_mask);
        static_cast<void>(::sigaction(signal_number, &default_action, nullptr));

        std::array<std::byte, kFaultRecordSize> record{};
        record[0] = std::byte{static_cast<unsigned char>(static_cast<unsigned>(signal_number) & 0xFFU)};
        const auto address = info != nullptr && info->si_addr != nullptr
                                 ? reinterpret_cast<std::uintptr_t>(info->si_addr)
                                 : std::uintptr_t{0};
        for (std::size_t index = 0; index < 8; ++index) {
            record[1 + index] =
                std::byte{static_cast<unsigned char>((address >> (8U * index)) & 0xFFU)};
        }
        // Uma única escrita: registros de 9 bytes são atômicos em pipes. Falhas
        // são ignoradas de propósito — o diagnóstico sem endereço ainda vale,
        // e o handler não pode depender de nada além de syscalls diretas.
        if (g_crash_report_fd >= 0) {
            static_cast<void>(::write(g_crash_report_fd, record.data(), record.size()));
        }
    };
    action.sa_flags = SA_SIGINFO;
    ::sigemptyset(&action.sa_mask);
    constexpr std::array<int, 8> kFatalSignals{SIGSEGV, SIGILL, SIGBUS, SIGABRT,
                                               SIGFPE,  SIGTRAP, SIGSYS, SIGQUIT};
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
        default:
            return {"sinal", "término inesperado por sinal"};
    }
}

GuestOutcome run_guest_isolated(const std::uintptr_t entry_point,
                                const std::uintptr_t stack_top,
                                const std::uint64_t timeout_ms,
                                const std::filesystem::path& working_directory) noexcept {
    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) != 0) {
        return {.kind = GuestOutcomeKind::SpawnFailed};
    }
    int fault_fds[2] = {-1, -1};
    if (::pipe(fault_fds) != 0) {
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        return {.kind = GuestOutcomeKind::SpawnFailed};
    }

    const ::pid_t child = ::fork();
    if (child < 0) {
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        ::close(fault_fds[0]);
        ::close(fault_fds[1]);
        return {.kind = GuestOutcomeKind::SpawnFailed};
    }

    if (child == 0) {
        ::close(pipe_fds[0]);
        ::close(fault_fds[0]);
        if (!working_directory.empty() && ::chdir(working_directory.c_str()) != 0) {
            ::close(pipe_fds[1]);
            ::close(fault_fds[1]);
            ::_exit(126);
        }
        install_crash_reporter(fault_fds[1]);
        ignore_broken_pipe();
        const GuestExecutionResult result = execute_guest_entry(entry_point, stack_top);
        const std::array<std::byte, kProtocolSize> message{
            result.exited_explicitly ? std::byte{1} : std::byte{0},
            std::byte{static_cast<unsigned char>(result.exit_code & 0xFFU)},
            std::byte{static_cast<unsigned char>((result.exit_code >> 8) & 0xFFU)},
            std::byte{static_cast<unsigned char>((result.exit_code >> 16) & 0xFFU)},
            std::byte{static_cast<unsigned char>((result.exit_code >> 24) & 0xFFU)},
        };
        const bool sent = write_exact(pipe_fds[1], message.data(), message.size());
        ::close(pipe_fds[1]);
        ::_exit(sent ? 0 : 125);
    }

    ::close(pipe_fds[1]);
    ::close(fault_fds[1]);

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
        descriptor.events = POLLIN;
        const int poll_result =
            ::poll(&descriptor, 1, timeout_ms == 0 ? -1 : static_cast<int>(remaining));
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
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
            ::close(pipe_fds[0]);
            ::close(fault_fds[0]);
            return {.kind = GuestOutcomeKind::SpawnFailed};
        }
    }

    if (timed_out) {
        static_cast<void>(::kill(child, SIGKILL));
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
            ::close(pipe_fds[0]);
            ::close(fault_fds[0]);
            return {.kind = GuestOutcomeKind::SpawnFailed};
        }
        if (message[0] != std::byte{0} && message[0] != std::byte{1}) {
            ::close(pipe_fds[0]);
            ::close(fault_fds[0]);
            return {.kind = GuestOutcomeKind::SpawnFailed};
        }
        outcome.exited_explicitly = message[0] == std::byte{1};
        std::uint32_t code = 0;
        for (std::uint32_t shift = 0; shift < 4; ++shift) {
            const std::uint32_t byte =
                static_cast<std::uint32_t>(static_cast<unsigned char>(message[1 + shift]));
            code |= byte << (8U * shift);
        }
        outcome.exit_code = code;
        outcome.kind = GuestOutcomeKind::Exited;
    } else {
        // Sem WUNTRACED/WCONTINUED o waitpid só relata WIFEXITED ou
        // WIFSIGNALED; qualquer outro caso é término por sinal.
        outcome.kind = GuestOutcomeKind::Signaled;
        outcome.signal_number = WTERMSIG(status);

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
                for (std::size_t index = 0; index < 8; ++index) {
                    const std::uint64_t byte =
                        static_cast<std::uint64_t>(
                            std::to_integer<unsigned>(record[1 + index]));
                    address |= byte << (8U * index);
                }
                outcome.fault_recorded = true;
                outcome.fault_address = address;
            }
        }
    }
    ::close(pipe_fds[0]);
    ::close(fault_fds[0]);
    return outcome;
}

}  // namespace tradutorlinux::process
