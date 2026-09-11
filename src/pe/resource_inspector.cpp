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
    }

    return result;
}

}  // namespace tradutorlinux::pe

