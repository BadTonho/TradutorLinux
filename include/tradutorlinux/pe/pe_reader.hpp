#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tradutorlinux::pe {

enum class ParseStatus {
    Success,
    Truncated,
    Malformed,
    UnsupportedArchitecture,
    UnsupportedFormat,
};

struct SectionInfo {
    std::string name;
    std::uint32_t virtual_address{};
    std::uint32_t virtual_size{};
    std::uint32_t raw_data_pointer{};
    std::uint32_t raw_data_size{};
    std::uint32_t characteristics{};
};

struct ImportedSymbol {
    bool by_ordinal{};
    std::uint16_t ordinal{};
    std::string name;
};

struct ImportedDll {
    std::string name;
    std::vector<ImportedSymbol> symbols;
};

struct BaseRelocEntry {
    std::uint16_t type{};
    std::uint16_t offset{};
};

struct BaseRelocBlock {
    std::uint32_t page_rva{};
    std::vector<BaseRelocEntry> entries;
};

struct PeInfo {
    bool is_pe32_plus{};
    std::uint16_t machine{};
    std::uint16_t number_of_sections{};
    std::uint32_t address_of_entry_point{};
    std::uint64_t image_base{};
    std::uint32_t size_of_image{};
    std::uint16_t subsystem{};
    std::uint32_t import_directory_rva{};
    std::uint32_t import_directory_size{};
    std::uint32_t relocation_directory_rva{};
    std::uint32_t relocation_directory_size{};
    std::vector<SectionInfo> sections;
    std::vector<ImportedDll> imports;
    std::vector<BaseRelocBlock> relocations;
};

struct ParseResult {
    ParseStatus status{ParseStatus::Success};
    std::string error_message;
    PeInfo info;
};

[[nodiscard]] ParseResult parse_pe(std::span<const std::byte> data);

}  // namespace tradutorlinux::pe
