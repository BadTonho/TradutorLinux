#include "tradutorlinux/diagnostics/crash_context.hpp"

#include <dlfcn.h>

namespace tradutorlinux::diagnostics {

GuestCrashContext describe_guest_crash(const loader::MappedImage& image,
                                       const loader::ResolveResult& imports,
                                       const std::uint64_t fault_address) {
    GuestCrashContext context;
    if (fault_address < image.base ||
        fault_address >= image.base + static_cast<std::uint64_t>(image.size)) {
        return context;
    }

    context.valid = true;
    context.rva = fault_address - image.base;

    for (const loader::MapRegion& region : image.regions) {
        const std::uint64_t start = region.rva;
        const std::uint64_t end = start + region.size;
        if (context.rva >= start && context.rva < end && !region.name.empty()) {
            context.section = region.name;
            break;
        }
    }

    // O slot da IAT imediatamente abaixo da falta é a melhor pista de qual
    // importação estava em jogo: as chamadas do convidado passam por esses
    // slots, que ficam contíguos na imagem.
    bool has_candidate = false;
    std::uint32_t best_rva = 0;
    for (const loader::ResolvedImport& entry : imports.imports) {
        if (entry.status != loader::ImportStatus::Resolved) {
            continue;
        }
        if (entry.iat_rva > context.rva) {
            continue;
        }
        if (!has_candidate || entry.iat_rva >= best_rva) {
            has_candidate = true;
            best_rva = entry.iat_rva;
            context.nearest_import = entry.dll + "!" + entry.symbol;
        }
    }

    return context;
}

HostAddressContext describe_host_address(const std::uint64_t address) {
    HostAddressContext context;
    if (address == 0) {
        return context;
    }

    Dl_info info{};
    if (::dladdr(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)), &info) == 0) {
        return context;
    }

    context.valid = true;
    if (info.dli_fname != nullptr) {
        const std::string_view path{info.dli_fname};
        const std::size_t separator = path.rfind('/');
        context.module = std::string{path.substr(
            separator == std::string_view::npos ? 0 : separator + 1)};
    }
    if (info.dli_sname != nullptr) {
        context.symbol = info.dli_sname;
    }
    if (info.dli_fbase != nullptr) {
        const auto module_address = reinterpret_cast<std::uintptr_t>(info.dli_fbase);
        const auto queried_address = static_cast<std::uintptr_t>(address);
        if (queried_address >= module_address) {
            context.module_offset = queried_address - module_address;
        }
    }
    if (info.dli_saddr != nullptr) {
        const auto symbol_address = reinterpret_cast<std::uintptr_t>(info.dli_saddr);
        const auto queried_address = static_cast<std::uintptr_t>(address);
        if (queried_address >= symbol_address) {
            context.symbol_offset = queried_address - symbol_address;
        }
    }
    return context;
}

}  // namespace tradutorlinux::diagnostics
