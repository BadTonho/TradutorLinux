#include "tradutorlinux/pe/resource_inspector.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tradutorlinux::pe {

namespace {

inline std::uint16_t read_u16(std::span<const std::byte> data, const std::size_t offset) noexcept {
    if (offset + 2 > data.size()) {
        return 0;
    }
    return static_cast<std::uint16_t>(
        static_cast<std::uint8_t>(data[offset]) |
        (static_cast<std::uint8_t>(data[offset + 1]) << 8));
}

inline std::uint32_t read_u32(std::span<const std::byte> data, const std::size_t offset) noexcept {
    if (offset + 4 > data.size()) {
        return 0;
    }
    return static_cast<std::uint32_t>(
        static_cast<std::uint8_t>(data[offset]) |
        (static_cast<std::uint8_t>(data[offset + 1]) << 8) |
        (static_cast<std::uint8_t>(data[offset + 2]) << 16) |
        (static_cast<std::uint8_t>(data[offset + 3]) << 24));
}

bool contains_ci(const std::string_view haystack, const std::string_view needle) noexcept {
    const auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                                [](const char a, const char b) {
                                    return std::tolower(static_cast<unsigned char>(a)) ==
                                           std::tolower(static_cast<unsigned char>(b));
                                });
    return it != haystack.end();
}

[[nodiscard]] std::string extract_attribute_value(const std::string_view xml,
                                                  const std::size_t start_pos,
                                                  const std::string_view attr_name) {
    const std::size_t attr_pos = xml.find(attr_name, start_pos);
    if (attr_pos == std::string_view::npos) {
        return {};
    }
    const std::size_t eq_pos = xml.find('=', attr_pos + attr_name.size());
    if (eq_pos == std::string_view::npos || eq_pos > attr_pos + attr_name.size() + 2) {
        return {};
    }
    std::size_t val_start = eq_pos + 1;
    while (val_start < xml.size() && (xml[val_start] == ' ' || xml[val_start] == '\t')) {
        ++val_start;
    }
    if (val_start >= xml.size()) {
        return {};
    }
    const char quote = xml[val_start];
    if (quote != '"' && quote != '\'') {
        return {};
    }
    ++val_start;
    const std::size_t val_end = xml.find(quote, val_start);
    if (val_end == std::string_view::npos) {
        return {};
    }
    return std::string(xml.substr(val_start, val_end - val_start));
}

[[nodiscard]] std::string extract_tag_content(const std::string_view xml,
                                              const std::string_view tag_name) {
    const std::string open_tag = "<" + std::string(tag_name);
    const std::size_t tag_pos = xml.find(open_tag);
    if (tag_pos == std::string_view::npos) {
        return {};
    }
    const std::size_t close_bracket = xml.find('>', tag_pos + open_tag.size());
    if (close_bracket == std::string_view::npos || close_bracket >= xml.size() - 1) {
        return {};
    }
    if (xml[close_bracket - 1] == '/') {
        return {};
    }
    const std::string end_tag = "</" + std::string(tag_name) + ">";
    const std::size_t end_tag_pos = xml.find(end_tag, close_bracket + 1);
    if (end_tag_pos == std::string_view::npos) {
        return {};
    }
    std::string_view content = xml.substr(close_bracket + 1, end_tag_pos - (close_bracket + 1));
    while (!content.empty() && (content.front() == ' ' || content.front() == '\t' ||
                                content.front() == '\r' || content.front() == '\n')) {
        content.remove_prefix(1);
    }
    while (!content.empty() && (content.back() == ' ' || content.back() == '\t' ||
                                content.back() == '\r' || content.back() == '\n')) {
        content.remove_suffix(1);
    }
    return std::string(content);
}

[[nodiscard]] std::string utf16le_to_utf8(std::span<const std::byte> data,
                                          const std::size_t offset,
                                          const std::size_t length_chars) {
    std::string out;
    out.reserve(length_chars);
    for (std::size_t i = 0; i < length_chars; ++i) {
        const std::uint16_t ch = read_u16(data, offset + i * 2);
        if (ch == 0) {
            break;
        }
        if (ch < 0x80) {
            out.push_back(static_cast<char>(ch));
        } else if (ch < 0x800) {
            out.push_back(static_cast<char>(0xC0 | ((ch >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else {
            out.push_back('?');
        }
    }
    return out;
}

[[nodiscard]] std::optional<std::span<const std::byte>> resolve_resource_span(
    std::span<const std::byte> file_bytes,
    const PeInfo& info) noexcept {
    if (info.resource_directory_rva == 0 || info.resource_directory_size < 16) {
        return std::nullopt;
    }
    const std::uint32_t rva = info.resource_directory_rva;
    const std::uint32_t size = info.resource_directory_size;

    // Headers: RVA [0, SizeOfHeaders)
    if (static_cast<std::uint64_t>(rva) + size <= info.size_of_headers &&
        static_cast<std::uint64_t>(rva) + size <= file_bytes.size()) {
        return file_bytes.subspan(rva, size);
    }

    // Procura na tabela de seções
    for (const SectionInfo& sec : info.sections) {
        const std::uint64_t span = std::max<std::uint64_t>(sec.virtual_size, sec.raw_data_size);
        const std::uint64_t sec_end = static_cast<std::uint64_t>(sec.virtual_address) + span;
        if (static_cast<std::uint64_t>(rva) >= sec.virtual_address && rva < sec_end) {
            const std::uint64_t delta = static_cast<std::uint64_t>(rva) - sec.virtual_address;
            if (delta + size > sec.raw_data_size) {
                return std::nullopt;
            }
            const std::uint64_t file_offset = static_cast<std::uint64_t>(sec.raw_data_pointer) + delta;
            if (file_offset + size > file_bytes.size()) {
                return std::nullopt;
            }
            return file_bytes.subspan(static_cast<std::size_t>(file_offset),
                                      static_cast<std::size_t>(size));
        }
    }
    return std::nullopt;
}

}  // namespace

std::string standard_resource_type_name(const std::uint32_t type_id) {
    switch (type_id) {
        case 1: return "cursor";
        case 2: return "bitmap";
        case 3: return "icon";
        case 4: return "menu";
        case 5: return "dialog";
        case 6: return "string";
        case 7: return "fontdir";
        case 8: return "font";
        case 9: return "accelerator";
        case 10: return "rcdata";
        case 11: return "messagetable";
        case 12: return "group_cursor";
        case 14: return "group_icon";
        case 16: return "version";
        case 17: return "dlginclude";
        case 19: return "plugplay";
        case 20: return "vxd";
        case 21: return "anicursor";
        case 22: return "aniicon";
        case 23: return "html";
        case 24: return "manifest";
        default: return "type_" + std::to_string(type_id);
    }
}

ManifestInfo parse_manifest_xml(std::string_view xml) {
    ManifestInfo info;
    if (xml.empty()) {
        return info;
    }

    if (xml.size() >= 3 &&
        static_cast<unsigned char>(xml[0]) == 0xEF &&
        static_cast<unsigned char>(xml[1]) == 0xBB &&
        static_cast<unsigned char>(xml[2]) == 0xBF) {
        xml.remove_prefix(3);
    }

    info.has_manifest = true;

    const std::size_t req_pos = xml.find("requestedExecutionLevel");
    if (req_pos != std::string_view::npos) {
        info.requested_execution_level = extract_attribute_value(xml, req_pos, "level");
        info.ui_access = extract_attribute_value(xml, req_pos, "uiAccess");
    }

    const std::string dpi_awareness = extract_tag_content(xml, "dpiAwareness");
    if (!dpi_awareness.empty()) {
        info.dpi_aware = dpi_awareness;
    } else {
        const std::string dpi_aware = extract_tag_content(xml, "dpiAware");
        if (!dpi_aware.empty()) {
            info.dpi_aware = dpi_aware;
        }
    }

    struct OsMapping {
        const char* guid;
        const char* label;
    };
    constexpr OsMapping kSupportedOsMap[] = {
        {"8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a", "Windows 10/11"},
        {"1f676c76-80e1-4239-95bb-83d0f6d0da78", "Windows 8.1"},
        {"4a2f28e3-53b9-4441-ba9c-d69d4a4a6e38", "Windows 8"},
        {"35138b9a-5d96-4fbd-8e2d-a2440225f93a", "Windows 7"},
        {"e2011457-1546-43c5-a5fe-008deee3d3f0", "Windows Vista"},
    };

    for (const auto& mapping : kSupportedOsMap) {
        if (contains_ci(xml, mapping.guid)) {
            info.supported_os.emplace_back(mapping.label);
        }
    }

    return info;
}

ProductVersionInfo parse_version_info(const std::span<const std::byte> version_data) {
    ProductVersionInfo result{};
    if (version_data.size() < 40) {
        return result;
    }

    // Procura a assinatura do cabeçalho fixo VS_FIXEDFILEINFO: 0xFEEF04BD (little-endian: 0xBD, 0x04, 0xEF, 0xFE)
    const std::array<std::byte, 4> kFixedSig{std::byte{0xBD}, std::byte{0x04}, std::byte{0xEF}, std::byte{0xFE}};
    const auto sig_it = std::search(version_data.begin(), version_data.end(), kFixedSig.begin(), kFixedSig.end());
    if (sig_it != version_data.end()) {
        const std::size_t sig_offset = static_cast<std::size_t>(std::distance(version_data.begin(), sig_it));
        if (sig_offset + 52 <= version_data.size()) {
            result.has_version_info = true;
            const std::uint32_t fv_ms = read_u32(version_data, sig_offset + 8);
            const std::uint32_t fv_ls = read_u32(version_data, sig_offset + 12);
            const std::uint32_t pv_ms = read_u32(version_data, sig_offset + 16);
            const std::uint32_t pv_ls = read_u32(version_data, sig_offset + 20);

            if (fv_ms != 0 || fv_ls != 0) {
                result.file_version = std::to_string(fv_ms >> 16) + "." +
                                      std::to_string(fv_ms & 0xFFFF) + "." +
                                      std::to_string(fv_ls >> 16) + "." +
                                      std::to_string(fv_ls & 0xFFFF);
            }
            if (pv_ms != 0 || pv_ls != 0) {
                result.product_version = std::to_string(pv_ms >> 16) + "." +
                                         std::to_string(pv_ms & 0xFFFF) + "." +
                                         std::to_string(pv_ls >> 16) + "." +
                                         std::to_string(pv_ls & 0xFFFF);
            }
        }
    }

    // Helper para extrair valores UTF-16LE de StringFileInfo / StringTable
    auto extract_utf16_val = [&](std::string_view key_ascii) -> std::string {
        std::vector<std::byte> key_bytes;
        key_bytes.reserve((key_ascii.size() + 1) * 2);
        for (const char c : key_ascii) {
            key_bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
            key_bytes.push_back(std::byte{0});
        }
        key_bytes.push_back(std::byte{0});
        key_bytes.push_back(std::byte{0});

        const auto it = std::search(version_data.begin(), version_data.end(), key_bytes.begin(), key_bytes.end());
        if (it == version_data.end()) {
            return {};
        }
        const std::size_t key_offset = static_cast<std::size_t>(std::distance(version_data.begin(), it));
        const std::size_t after_key = key_offset + key_bytes.size();
        const std::size_t val_offset = (after_key + 3) & ~std::size_t{3};
        if (val_offset >= version_data.size()) {
            return {};
        }

        std::size_t char_count = 0;
        constexpr std::size_t kMaxChars = 256;
        for (std::size_t pos = val_offset; pos + 1 < version_data.size() && char_count < kMaxChars; pos += 2) {
            const std::uint16_t ch = read_u16(version_data, pos);
            if (ch == 0) {
                break;
            }
            ++char_count;
        }
        if (char_count == 0) {
            return {};
        }
        return utf16le_to_utf8(version_data, val_offset, char_count);
    };

    const std::string prod_name = extract_utf16_val("ProductName");
    if (!prod_name.empty()) {
        result.has_version_info = true;
        result.product_name = prod_name;
    }
    const std::string prod_ver = extract_utf16_val("ProductVersion");
    if (!prod_ver.empty()) {
        result.has_version_info = true;
        result.product_version = prod_ver;
    }
    const std::string file_ver = extract_utf16_val("FileVersion");
    if (!file_ver.empty()) {
        result.has_version_info = true;
        result.file_version = file_ver;
    }
    const std::string company = extract_utf16_val("CompanyName");
    if (!company.empty()) {
        result.has_version_info = true;
        result.company_name = company;
    }
    const std::string desc = extract_utf16_val("FileDescription");
    if (!desc.empty()) {
        result.has_version_info = true;
        result.file_description = desc;
    }

    return result;
}

ResourceInspectionResult inspect_pe_resources(
    std::span<const std::byte> file_bytes,
    const PeInfo& info) {
    ResourceInspectionResult result;
    result.directory_rva = info.resource_directory_rva;
    result.directory_size = info.resource_directory_size;

    const auto rsrc_opt = resolve_resource_span(file_bytes, info);
    if (!rsrc_opt.has_value()) {
        return result;
    }

    const std::span<const std::byte> rsrc = *rsrc_opt;
    if (rsrc.size() < 16) {
        return result;
    }

    const std::uint16_t num_named = read_u16(rsrc, 12);
    const std::uint16_t num_id = read_u16(rsrc, 14);
    const std::uint32_t total_entries = static_cast<std::uint32_t>(num_named) + num_id;

    // Limites estritos de segurança
    if (total_entries == 0 || total_entries > 1024) {
        return result;
    }
    if (16 + total_entries * 8 > rsrc.size()) {
        return result;
    }

    result.has_resources = true;

    auto find_leaf_rva_and_size = [&](const std::uint32_t off_data) -> std::pair<std::optional<std::uint32_t>, std::optional<std::uint32_t>> {
        std::optional<std::uint32_t> data_rva;
        std::optional<std::uint32_t> data_size;

        if ((off_data & 0x80000000U) != 0) {
            const std::size_t l2_offset = off_data & 0x7FFFFFFFU;
            if (l2_offset + 16 <= rsrc.size()) {
                const std::uint16_t l2_named = read_u16(rsrc, l2_offset + 12);
                const std::uint16_t l2_id = read_u16(rsrc, l2_offset + 14);
                const std::uint32_t l2_total = static_cast<std::uint32_t>(l2_named) + l2_id;
                if (l2_total > 0 && l2_offset + 16 + 8 <= rsrc.size()) {
                    const std::uint32_t l2_target = read_u32(rsrc, l2_offset + 16 + 4);
                    if ((l2_target & 0x80000000U) != 0) {
                        const std::size_t l3_offset = l2_target & 0x7FFFFFFFU;
                        if (l3_offset + 16 <= rsrc.size()) {
                            const std::uint16_t l3_named = read_u16(rsrc, l3_offset + 12);
                            const std::uint16_t l3_id = read_u16(rsrc, l3_offset + 14);
                            const std::uint32_t l3_total = static_cast<std::uint32_t>(l3_named) + l3_id;
                            if (l3_total > 0 && l3_offset + 16 + 8 <= rsrc.size()) {
                                const std::uint32_t l3_target = read_u32(rsrc, l3_offset + 16 + 4);
                                if ((l3_target & 0x80000000U) == 0 && l3_target + 16 <= rsrc.size()) {
                                    data_rva = read_u32(rsrc, l3_target);
                                    data_size = read_u32(rsrc, l3_target + 4);
                                }
                            }
                        }
                    } else if (l2_target + 16 <= rsrc.size()) {
                        data_rva = read_u32(rsrc, l2_target);
                        data_size = read_u32(rsrc, l2_target + 4);
                    }
                }
            }
        } else if (off_data + 16 <= rsrc.size()) {
            data_rva = read_u32(rsrc, off_data);
            data_size = read_u32(rsrc, off_data + 4);
        }
        return {data_rva, data_size};
    };

    auto resolve_span_from_rva = [&](const std::uint32_t rva, const std::uint32_t len) -> std::span<const std::byte> {
        for (const SectionInfo& sec : info.sections) {
            const std::uint64_t span = std::max<std::uint64_t>(sec.virtual_size, sec.raw_data_size);
            const std::uint64_t sec_end = static_cast<std::uint64_t>(sec.virtual_address) + span;
            if (static_cast<std::uint64_t>(rva) >= sec.virtual_address && rva < sec_end) {
                const std::uint64_t delta = static_cast<std::uint64_t>(rva) - sec.virtual_address;
                if (delta < sec.raw_data_size) {
                    const std::uint64_t file_offset = static_cast<std::uint64_t>(sec.raw_data_pointer) + delta;
                    if (file_offset < file_bytes.size()) {
                        const std::uint64_t avail = std::min({
                            static_cast<std::uint64_t>(len),
                            sec.raw_data_size - delta,
                            file_bytes.size() - file_offset
                        });
                        if (avail > 0) {
                            return file_bytes.subspan(static_cast<std::size_t>(file_offset),
                                                      static_cast<std::size_t>(avail));
                        }
                    }
                }
                break;
            }
        }
        return {};
    };

    for (std::uint32_t i = 0; i < total_entries; ++i) {
        const std::size_t entry_offset = 16 + i * 8;
        const std::uint32_t name_or_id = read_u32(rsrc, entry_offset);
        const std::uint32_t offset_to_data = read_u32(rsrc, entry_offset + 4);

        std::uint32_t type_id = 0;
        std::string type_name;

        if (i < num_named || (name_or_id & 0x80000000U) != 0) {
            const std::size_t str_offset = name_or_id & 0x7FFFFFFFU;
            if (str_offset + 2 <= rsrc.size()) {
                const std::uint16_t str_len = read_u16(rsrc, str_offset);
                if (str_offset + 2 + static_cast<std::size_t>(str_len) * 2 <= rsrc.size()) {
                    type_name = utf16le_to_utf8(rsrc, str_offset + 2, str_len);
                }
            }
            if (type_name.empty()) {
                type_name = "named_resource";
            }
        } else {
            type_id = name_or_id;
            type_name = standard_resource_type_name(type_id);
        }

        std::uint32_t count = 0;
        if ((offset_to_data & 0x80000000U) != 0) {
            const std::size_t l2_offset = offset_to_data & 0x7FFFFFFFU;
            if (l2_offset + 16 <= rsrc.size()) {
                const std::uint16_t l2_named = read_u16(rsrc, l2_offset + 12);
                const std::uint16_t l2_id = read_u16(rsrc, l2_offset + 14);
                count = static_cast<std::uint32_t>(l2_named) + l2_id;
            }
        } else {
            count = 1;
        }

        result.types.push_back(ResourceTypeSummary{
            .type_id = type_id,
            .type_name = std::move(type_name),
            .count = count,
        });

        // Se for RT_MANIFEST (24), extrai e inspeciona o XML
        if (type_id == 24) {
            const auto [data_rva, data_size] = find_leaf_rva_and_size(offset_to_data);
            if (data_rva.has_value() && data_size.has_value() && *data_size > 0) {
                constexpr std::uint32_t kMaxManifestSize = 64 * 1024;
                const auto manifest_bytes = resolve_span_from_rva(*data_rva, std::min(*data_size, kMaxManifestSize));
                if (!manifest_bytes.empty()) {
                    const std::string_view xml_view(
                        reinterpret_cast<const char*>(manifest_bytes.data()),
                        manifest_bytes.size());
                    result.manifest = parse_manifest_xml(xml_view);
                }
            }
        } else if (type_id == 16) { // RT_VERSION
            const auto [data_rva, data_size] = find_leaf_rva_and_size(offset_to_data);
            if (data_rva.has_value() && data_size.has_value() && *data_size > 0) {
                constexpr std::uint32_t kMaxVersionSize = 64 * 1024;
                const auto ver_bytes = resolve_span_from_rva(*data_rva, std::min(*data_size, kMaxVersionSize));
                if (!ver_bytes.empty()) {
                    result.version_info = parse_version_info(ver_bytes);
                }
            }
        }
    }

    return result;
}

}  // namespace tradutorlinux::pe

