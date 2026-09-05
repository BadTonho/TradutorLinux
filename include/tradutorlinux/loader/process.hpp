#pragma once

#include "tradutorlinux/loader/image_mapper.hpp"
#include "tradutorlinux/loader/import_resolver.hpp"
#include "tradutorlinux/pe/pe_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace tradutorlinux::loader {

class GuestModuleGraph;

constexpr std::size_t kGuestStackSize = 32 << 20;  // 32 MiB usáveis (para suportar buffers estendidos de até 8 MiB)

struct GuestThread {
    std::uint64_t stack_top{};
    std::uint64_t stack_size{};
    std::uint64_t entry_point{};
};

struct GuestProcess {
    pe::PeInfo info;
    MappedImage image;
    ResolveResult imports;
    GuestThread thread;
    std::byte* stack{nullptr};
    std::size_t stack_size{};
};

enum class PrepareStatus {
    Success,
    InvalidImage,
    OutOfMemory,
    UnresolvedImports,
};

struct PrepareResult {
    PrepareStatus status{PrepareStatus::Success};
    std::string error_message;
    GuestProcess process;
};

// Maps the image, resolves the imports into the IAT and prepares the minimal
// stack of the initial thread (a guard page below, PROT_NONE). The entry point
// is never executed. Internal modules must be registered beforehand
// (register_builtin_modules). On UnresolvedImports the process is still valid
// so the diagnostic can be reported before destroy_process.
[[nodiscard]] PrepareResult prepare_process(const pe::PeInfo& info,
                                            std::span<const std::byte> file_bytes,
                                            const MapOptions& options = {});

[[nodiscard]] PrepareResult prepare_process(const pe::PeInfo& info,
                                            std::span<const std::byte> file_bytes,
                                            GuestModuleGraph* module_graph,
                                            const std::filesystem::path& requester,
                                            const MapOptions& options = {});

void destroy_process(GuestProcess& process);

}  // namespace tradutorlinux::loader
