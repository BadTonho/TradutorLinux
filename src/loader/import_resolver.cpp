#include "tradutorlinux/loader/import_resolver.hpp"

#include "tradutorlinux/loader/dll_overrides.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/module_graph.hpp"

#include "tradutorlinux/util/basics.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <unordered_map>
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

[[nodiscard]] std::optional<std::filesystem::path> find_side_by_side_dll(
    const std::filesystem::path& requester, const std::string_view dll_name) {
    if (requester.empty()) return std::nullopt;
    const std::filesystem::path parent = requester.parent_path();
    if (parent.empty()) return std::nullopt;
    std::error_code ec;
    const std::filesystem::path direct = parent / dll_name;
    if (std::filesystem::is_regular_file(direct, ec)) {
        return direct;
    }
    std::filesystem::directory_iterator it{parent, ec};
    if (ec) return std::nullopt;
    for (const auto& entry : it) {
        if (!entry.is_regular_file(ec)) continue;
        if (util::ascii_iequals(entry.path().filename().string(), dll_name)) {
            return entry.path();
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::vector<std::byte>> read_file_bytes(
    const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) return std::nullopt;
    stream.seekg(0, std::ios::end);
    const std::streamoff end = stream.tellg();
    if (end <= 0) return std::nullopt;
    stream.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) return std::nullopt;
    return bytes;
}

void inspect_group(ResolveResult& result, const std::vector<pe::ImportedDll>& dlls,
                   const ImportMechanism mechanism,
                   const std::filesystem::path& requester) {
    std::unordered_map<std::string, std::optional<pe::PeInfo>> guest_dll_cache;

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
                if (!requester.empty()) {
                    auto cache_it = guest_dll_cache.find(dll.name);
                    if (cache_it == guest_dll_cache.end()) {
                        std::optional<pe::PeInfo> cached_info;
                        if (const auto dll_path = find_side_by_side_dll(requester, dll.name)) {
                            if (const auto file_bytes = read_file_bytes(*dll_path)) {
                                pe::ParseResult parsed = pe::parse_pe(*file_bytes);
                                if (parsed.status == pe::ParseStatus::Success &&
                                    parsed.info.is_dll && parsed.info.is_pe32_plus &&
                                    parsed.info.machine == 0x8664) {
                                    cached_info = std::move(parsed.info);
                                }
                            }
                        }
                        cache_it = guest_dll_cache.emplace(dll.name, std::move(cached_info)).first;
                    }

                    if (cache_it->second.has_value()) {
                        const pe::PeInfo& guest_info = *cache_it->second;
                        const auto export_it = std::find_if(
                            guest_info.exports.begin(), guest_info.exports.end(),
                            [&](const pe::ExportedSymbol& exp) {
                                return symbol.by_ordinal
                                           ? (exp.ordinal == symbol.ordinal)
                                           : (exp.by_name && exp.name == symbol.name);
                            });
                        if (export_it != guest_info.exports.end()) {
                            entry.ordinal = export_it->ordinal;
                            entry.address = 0x10000;
                            entry.support = ExportSupport::Full;
                            entry.provider = "guest";
                            result.imports.push_back(std::move(entry));
                            continue;
                        }
                        fail(result, entry,
                             symbol.by_ordinal ? ImportStatus::UnknownOrdinal
                                               : ImportStatus::UnknownSymbol,
                             symbol.by_ordinal ? "ordinal não exportado pela DLL convidada"
                                               : "símbolo não exportado pela DLL convidada");
                        result.imports.push_back(std::move(entry));
                        continue;
                    }
                }

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
                     lookup.detail.empty()
                         ? (symbol.by_ordinal ? "ordinal não exportado pelo módulo"
                                              : "símbolo não exportado pelo módulo")
                         : lookup.detail);
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
            entry.support = lookup.support;
            result.imports.push_back(std::move(entry));
        }
    }
}

}  // namespace

ResolveResult inspect_imports(const pe::PeInfo& info, const std::filesystem::path& requester) {
    ResolveResult result;
    inspect_group(result, info.imports, ImportMechanism::Static, requester);
    inspect_group(result, info.delay_imports, ImportMechanism::Delay, requester);
    return result;
}

ResolveResult resolve_imports(MappedImage& image, const pe::PeInfo& info) {
    ResolveResult result = inspect_imports(info);
    for (ResolvedImport& entry : result.imports) {
        if (entry.mechanism == ImportMechanism::Delay) {
            if (entry.status == ImportStatus::Resolved && entry.address != 0) {
                patch_address(image, entry);
                if (entry.status != ImportStatus::Resolved) {
                    fail(result, entry, entry.status, entry.detail);
                }
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

ResolveResult resolve_imports(MappedImage& image, const pe::PeInfo& info,
                              GuestModuleGraph& graph,
                              const std::filesystem::path& requester) {
    return graph.resolve_imports(image, info, requester);
}

}  // namespace tradutorlinux::loader
