#include "tradutorlinux/process/isolate.hpp"

#include "tradutorlinux/runtime/winapi.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tradutorlinux::process {
namespace {

constexpr std::size_t kProtocolSize = 5;  // [explicit:1][exit-code:4 LE]

bool read_exact(const int fd, std::byte* const buffer, const std::size_t size) noexcept {
    std::size_t total = 0;
    while (total < size) {
        const ::ssize_t count = ::read(fd, buffer + total, size - total);
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

// The guest must run with the default disposition for fatal signals so that a
// crash (ex.: SIGSEGV) reaches waitpid as a real signal. Host runtimes such as
// AddressSanitizer install their own handlers, which would swallow the signal
// and hide the guest-signal diagnosis; restore SIG_DFL in the freshly forked
// child before executing the guest.
void reset_fatal_signal_handlers() noexcept {
    struct sigaction action {};
    action.sa_handler = SIG_DFL;
    ::sigemptyset(&action.sa_mask);
    constexpr std::array<int, 8> kFatalSignals{SIGSEGV, SIGILL, SIGBUS, SIGABRT,
                                               SIGFPE,  SIGTRAP, SIGSYS, SIGQUIT};
    for (const int signal_number : kFatalSignals) {
        static_cast<void>(::sigaction(signal_number, &action, nullptr));
    }
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
        case SIGTERM:
            return {"SIGTERM", "terminado por SIGTERM"};
        case SIGQUIT:
            return {"SIGQUIT", "terminado por SIGQUIT"};
        default:
            return {"sinal", "término inesperado por sinal"};
    }
}

GuestOutcome run_guest_isolated(const std::uintptr_t entry_point,
                                const std::uintptr_t stack_top) noexcept {
    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) != 0) {
        return {.kind = GuestOutcomeKind::SpawnFailed};
    }

    const ::pid_t child = ::fork();
    if (child < 0) {
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        return {.kind = GuestOutcomeKind::SpawnFailed};
    }

    if (child == 0) {
        ::close(pipe_fds[0]);
        reset_fatal_signal_handlers();
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

    int status = 0;
    while (::waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            ::close(pipe_fds[0]);
            return {.kind = GuestOutcomeKind::SpawnFailed};
        }
    }

    GuestOutcome outcome{};
    if (WIFEXITED(status)) {
        std::array<std::byte, kProtocolSize> message{};
        if (!read_exact(pipe_fds[0], message.data(), message.size())) {
            ::close(pipe_fds[0]);
            return {.kind = GuestOutcomeKind::SpawnFailed};
        }
        if (message[0] != std::byte{0} && message[0] != std::byte{1}) {
            ::close(pipe_fds[0]);
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
    }
    ::close(pipe_fds[0]);
    return outcome;
}

}  // namespace tradutorlinux::process
