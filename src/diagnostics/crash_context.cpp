#include "tradutorlinux/diagnostics/crash_context.hpp"

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

}  // namespace tradutorlinux::diagnostics
