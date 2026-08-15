#pragma once

#include <cstdint>
#include <string_view>

namespace tradutorlinux::process {

enum class GuestOutcomeKind {
    Exited,       // o guest retornou do entry point ou chamou ExitProcess
    Signaled,     // o guest terminou por um sinal Linux (ex.: SIGSEGV)
    SpawnFailed,  // não foi possível criar o processo filho
};

struct GuestOutcome {
    GuestOutcomeKind kind{GuestOutcomeKind::Exited};
    bool exited_explicitly{};
    std::uint32_t exit_code{};
    int signal_number{};
};

struct SignalDescription {
    std::string_view name;
    std::string_view detail;
};

[[nodiscard]] SignalDescription describe_signal(int signal_number) noexcept;

// Executes the guest entry point in a freshly forked child process and waits
// for it. The parent keeps both streams (guest stdout, diagnostics stderr);
// the child only runs the guest and reports the outcome through a pipe before
// _exit(0). When the guest terminates by a signal, the parent observes it via
// waitpid and the caller can publish a controlled guest-signal diagnosis.
// Must not be called while the process has other running threads.
[[nodiscard]] GuestOutcome run_guest_isolated(std::uintptr_t entry_point,
                                              std::uintptr_t stack_top) noexcept;

}  // namespace tradutorlinux::process
