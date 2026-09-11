#pragma once

#include "tradutorlinux/pe/pe_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tradutorlinux::pe {

struct ResourceTypeSummary {
    std::uint32_t type_id{0};
    std::string type_name;
    std::uint32_t count{0};
};

struct ResourceInspectionResult {
    bool has_resources{false};
    std::uint32_t directory_rva{0};
    std::uint32_t directory_size{0};
    std::vector<ResourceTypeSummary> types;
};

// Mapeia o ID padrão de recurso Win32 para seu nome canônico legível.
[[nodiscard]] std::string standard_resource_type_name(std::uint32_t type_id);

// Inspeciona com segurança a árvore de recursos PE (.rsrc) a partir dos bytes brutos do arquivo.
[[nodiscard]] ResourceInspectionResult inspect_pe_resources(
    std::span<const std::byte> file_bytes,
    const PeInfo& info);

}  // namespace tradutorlinux::pe
