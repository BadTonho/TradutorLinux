#pragma once

#include "tradutorlinux/loader/image_mapper.hpp"
#include "tradutorlinux/pe/pe_reader.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tradutorlinux::loader {

enum class ImportStatus {
    Resolved,
    UnknownDll,
    UnknownSymbol,
    UnknownOrdinal,
    NotImpl,
    UnsupportedMechanism,
};

struct ResolvedImport {
    std::string dll;
    bool by_ordinal{};
    std::string symbol;
    std::uint16_t ordinal{};
    std::uint32_t iat_rva{};
    std::uintptr_t address{};
    ImportStatus status{ImportStatus::Resolved};
    std::string detail;
};

struct ResolveResult {
    ImportStatus status{ImportStatus::Resolved};
    std::string error_message;
    std::vector<ResolvedImport> imports;
};

// Resolves every import of the image and writes the resolved addresses into the
// import address table of the mapped memory. The image must already be mapped
// with relocations applied and the internal modules registered beforehand.
// On failure the overall status reflects the first problem found, but every
// entry is still reported so the diagnostic is complete.
[[nodiscard]] ResolveResult resolve_imports(MappedImage& image, const pe::PeInfo& info);

}  // namespace tradutorlinux::loader
