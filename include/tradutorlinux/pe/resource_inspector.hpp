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

struct ManifestInfo {
    bool has_manifest{false};
    std::string requested_execution_level;
    std::string ui_access;
    std::string dpi_aware;
    std::vector<std::string> supported_os;
};

struct ProductVersionInfo {
    bool has_version_info{false};
    std::string product_name;
    std::string product_version;
    std::string file_version;
    std::string company_name;
    std::string file_description;
};

struct ResourceInspectionResult {
    bool has_resources{false};
    std::uint32_t directory_rva{0};
    std::uint32_t directory_size{0};
    std::vector<ResourceTypeSummary> types;
    ManifestInfo manifest;
    ProductVersionInfo version_info;
};

// Mapeia o ID padrão de recurso Win32 para seu nome canônico legível.
[[nodiscard]] std::string standard_resource_type_name(std::uint32_t type_id);

// Analisa lexicalmente o texto XML de um RT_MANIFEST de forma segura.
[[nodiscard]] ManifestInfo parse_manifest_xml(std::string_view xml);

// Analisa os bytes binários de um recurso RT_VERSION (VS_VERSIONINFO) de forma segura.
[[nodiscard]] ProductVersionInfo parse_version_info(std::span<const std::byte> version_data);

// Inspeciona com segurança a árvore de recursos PE (.rsrc) a partir dos bytes brutos do arquivo.
[[nodiscard]] ResourceInspectionResult inspect_pe_resources(
    std::span<const std::byte> file_bytes,
    const PeInfo& info);

}  // namespace tradutorlinux::pe
