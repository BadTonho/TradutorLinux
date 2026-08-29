#include "tradutorlinux/loader/process.hpp"

#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/util/basics.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <string>
#include <utility>

#include <sys/mman.h>
#include <unistd.h>

namespace tradutorlinux::loader {
namespace {

[[nodiscard]] PrepareResult fail_prepare(const PrepareStatus status, std::string message) {
    return {.status = status, .error_message = std::move(message), .process = {}};
}

}  // namespace

PrepareResult prepare_process(const pe::PeInfo& info, const std::span<const std::byte> file_bytes,
                              const MapOptions& options) {
    MapResult map = map_image(info, file_bytes, options);
    if (map.status == MapStatus::OutOfMemory) {
        return fail_prepare(PrepareStatus::OutOfMemory, std::move(map.error_message));
    }
    if (map.status == MapStatus::InvalidImage) {
        return fail_prepare(PrepareStatus::InvalidImage, std::move(map.error_message));
    }

    // A checagem usa a permissão efetiva da página do host: duas seções que
    // compartilham a mesma página (ex.: .text e .data alinhadas por 4 KiB)
    // podem elevar a permissão real acima da característica individual de cada
    // uma, e é essa permissão que o entry point encontrará na execução.
    const std::uint32_t entry_rva = info.address_of_entry_point;
    const auto perms = effective_page_permissions(map.image, entry_rva);
    if (perms != SectionPermissions::ReadExecute && perms != SectionPermissions::ReadWriteExecute) {
        loader::unmap_image(map.image);
        return fail_prepare(PrepareStatus::InvalidImage,
                            "entry point fora de uma página executável");
    }

    GuestProcess process;
    process.info = info;
    process.image = std::move(map.image);
    process.imports = resolve_imports(process.image, process.info);

    const std::size_t page = util::host_page_size();
    const std::size_t guard_size = page;
    const std::size_t total_stack_size = guard_size + kGuestStackSize;
    void* stack = mmap(nullptr, total_stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED) {
        loader::unmap_image(process.image);
        return fail_prepare(PrepareStatus::OutOfMemory,
                            "não foi possível alocar a pilha do thread inicial");
    }
    runtime::invalidate_memory_map_cache();
    if (mprotect(stack, guard_size, PROT_NONE) != 0) {
        const int error = errno;
        munmap(stack, total_stack_size);
        loader::unmap_image(process.image);
        return fail_prepare(PrepareStatus::OutOfMemory,
                            "não foi possível proteger a guard page da pilha (errno=" +
                                std::to_string(error) + ")");
    }
    process.stack = static_cast<std::byte*>(stack);
    process.stack_size = total_stack_size;

    const std::uint64_t stack_base =
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(stack));
    const std::uint64_t usable_end = stack_base + guard_size + kGuestStackSize;
    process.thread.stack_top = usable_end & ~static_cast<std::uint64_t>(0xF);
    process.thread.stack_size = kGuestStackSize;
    process.thread.entry_point =
        process.image.base + static_cast<std::uint64_t>(process.info.address_of_entry_point);

    if (process.imports.status != ImportStatus::Resolved) {
        PrepareResult result;
        result.status = PrepareStatus::UnresolvedImports;
        result.error_message = process.imports.error_message;
        result.process = std::move(process);
        return result;
    }
    return {.status = PrepareStatus::Success, .error_message = {}, .process = std::move(process)};
}

void destroy_process(GuestProcess& process) {
    if (process.stack != nullptr && process.stack_size > 0) {
        munmap(process.stack, process.stack_size);
        runtime::invalidate_memory_map_cache();
    }
    process.stack = nullptr;
    process.stack_size = 0;
    loader::unmap_image(process.image);
    runtime::invalidate_memory_map_cache();
    process.thread = {};
    process.imports.imports.clear();
    process.imports.error_message.clear();
    process.info = {};
}

}  // namespace tradutorlinux::loader
