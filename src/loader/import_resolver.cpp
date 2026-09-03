#include "tradutorlinux/loader/import_resolver.hpp"

#include "tradutorlinux/loader/dll_overrides.hpp"
#include "tradutorlinux/loader/module.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace tradutorlinux::loader {
namespace {

constexpr std::size_t kPointerSize = 8;

void patch_address(MappedImage& image, ResolvedImport& entry) {
    const std::array<std::byte, kPointerSize> address_le{
        static_cast<std::byte>(entry.address & 0xFFU),
        static_cast<std::byte>((entry.address >> 8) & 0xFFU),
        static_cast<std::byte>((entry.address >> 16) & 0xFFU),
        static_cast<std::byte>((entry.address >> 24) & 0xFFU),
        static_cast<std::byte>((entry.address >> 32) & 0xFFU),
        static_cast<std::byte>((entry.address >> 40) & 0xFFU),
        static_cast<std::byte>((entry.address >> 48) & 0xFFU),
        static_cast<std::byte>((entry.address >> 56) & 0xFFU),
    };
    if (entry.iat_rva == 0) {
        entry.status = ImportStatus::UnsupportedMechanism;
        entry.detail = "RVA do slot na IAT nulo";
        return;
    }
    const PatchStatus patch =
        write_image_bytes(image, entry.iat_rva, address_le.data(), address_le.size());
    if (patch != PatchStatus::Success) {
        entry.status = ImportStatus::UnsupportedMechanism;
        entry.detail = patch == PatchStatus::InvalidAddress
                           ? "slot da IAT fora das seções mapeadas"
                           : "não foi possível tornar o slot da IAT gravável";
    }
}

void fail(ResolveResult& result, ResolvedImport& entry, const ImportStatus status,
          std::string detail) {
    entry.status = status;
    entry.detail = std::move(detail);
    if (result.status == ImportStatus::Resolved) {
        result.status = status;
        const std::string symbol = entry.by_ordinal
                                       ? "ordinal(" + std::to_string(entry.ordinal) + ")"
                                       : entry.symbol;
        result.error_message = entry.dll + "!" + symbol + ": " + entry.detail;
    }
}

void inspect_group(ResolveResult& result, const std::vector<pe::ImportedDll>& dlls,
                   const ImportMechanism mechanism) {
    for (const pe::ImportedDll& dll : dlls) {
        for (const pe::ImportedSymbol& symbol : dll.symbols) {
            ResolvedImport entry;
            entry.dll = dll.name;
            entry.mechanism = mechanism;
            entry.by_ordinal = symbol.by_ordinal;
            entry.symbol = symbol.by_ordinal ? std::string{} : symbol.name;
            entry.ordinal = symbol.by_ordinal ? symbol.ordinal : 0;
            entry.iat_rva = symbol.iat_rva;

            const bool forwarded_known = is_module_registered_forwarded(dll.name);
            if (!is_module_registered(dll.name) && !forwarded_known) {
                fail(result, entry, ImportStatus::UnknownDll, "módulo não registrado");
                result.imports.push_back(std::move(entry));
                continue;
            }
            const ExportLookup lookup =
                symbol.by_ordinal ? find_export_by_ordinal_forwarded(dll.name, symbol.ordinal)
                                  : find_export_forwarded(ExportQuery{dll.name, symbol.name});
            if (!lookup.found) {
                fail(result, entry,
                     symbol.by_ordinal ? ImportStatus::UnknownOrdinal : ImportStatus::UnknownSymbol,
                     symbol.by_ordinal ? "ordinal não exportado pelo módulo"
                                       : "símbolo não exportado pelo módulo");
                result.imports.push_back(std::move(entry));
                continue;
            }
            entry.ordinal = lookup.ordinal;
            if (lookup.address == 0) {
                fail(result, entry, ImportStatus::NotImpl,
                     "símbolo conhecido sem implementação");
                result.imports.push_back(std::move(entry));
                continue;
            }
            entry.address = lookup.address;
            result.imports.push_back(std::move(entry));
        }
    }
}

}  // namespace

ResolveResult inspect_imports(const pe::PeInfo& info) {
    ResolveResult result;
    inspect_group(result, info.imports, ImportMechanism::Static);
    inspect_group(result, info.delay_imports, ImportMechanism::Delay);
    return result;
}

ResolveResult resolve_imports(MappedImage& image, const pe::PeInfo& info) {
    ResolveResult result = inspect_imports(info);
    for (ResolvedImport& entry : result.imports) {
        if (entry.mechanism == ImportMechanism::Delay) {
            if (entry.status == ImportStatus::Resolved && entry.address != 0) {
                patch_address(image, entry);
            }
            continue;
        }
        if (entry.status != ImportStatus::Resolved) {
            continue;
        }
        patch_address(image, entry);
        if (entry.status != ImportStatus::Resolved) {
            fail(result, entry, entry.status, entry.detail);
        }
    }
    return result;
}

}  // namespace tradutorlinux::loader
