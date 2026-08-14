#include "tradutorlinux/loader/process.hpp"

#include "tradutorlinux/loader/module.hpp"

#include <algorithm>
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

    const std::uint64_t entry_rva = info.address_of_entry_point;
    const auto entry_region = std::find_if(
        map.image.regions.begin(), map.image.regions.end(), [entry_rva](const MapRegion& region) {
            const std::uint64_t end = static_cast<std::uint64_t>(region.rva) + region.size;
            return entry_rva >= region.rva && entry_rva < end &&
                   region.permissions == SectionPermissions::ReadExecute;
        });
    if (entry_region == map.image.regions.end()) {
        loader::unmap_image(map.image);
        return fail_prepare(PrepareStatus::InvalidImage,
                            "entry point fora de uma seção executável");
    }

    GuestProcess process;
    process.info = info;
    process.image = std::move(map.image);
    process.imports = resolve_imports(process.image, process.info);

    const long page_value = sysconf(_SC_PAGESIZE);
    const std::size_t page =
        page_value > 0 ? static_cast<std::size_t>(page_value) : static_cast<std::size_t>(0x1000);
    const std::size_t guard_size = page;
    const std::size_t total_stack_size = guard_size + kGuestStackSize;
    void* stack = mmap(nullptr, total_stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED) {
        loader::unmap_image(process.image);
        return fail_prepare(PrepareStatus::OutOfMemory,
                            "não foi possível alocar a pilha do thread inicial");
    }
    if (mprotect(stack, guard_size, PROT_NONE) != 0) {
        munmap(stack, total_stack_size);
        loader::unmap_image(process.image);
        return fail_prepare(PrepareStatus::OutOfMemory,
                            "não foi possível proteger a guard page da pilha");
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
    }
    process.stack = nullptr;
    process.stack_size = 0;
    loader::unmap_image(process.image);
    process.thread = {};
    process.imports.imports.clear();
    process.imports.error_message.clear();
    process.info = {};
}

}  // namespace tradutorlinux::loader
