#include "pe_builder.hpp"
#include "tradutorlinux/pe/pe_reader.hpp"
#include "tradutorlinux/pe/resource_inspector.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace tradutorlinux::pe {
namespace {

using namespace tradutorlinux::pe::testutil;

// Constrói uma seção .rsrc sintética válida com três tipos:
// - RT_ICON (3) com 2 ícones (IDs 1 e 2)
// - RT_VERSION (16) com 1 versão (ID 1)
// - RT_MANIFEST (24) com 1 manifesto (ID 1)
std::vector<std::byte> make_synthetic_rsrc_pe() {
    constexpr std::uint32_t kRsrcRva = 0x2000;
    std::vector<std::byte> rsrc;

    // --- Root Directory (Level 1: Types) ---
    // Header (16 bytes): offset 0
    push_u32(rsrc, 0);  // Characteristics
    push_u32(rsrc, 0);  // TimeDateStamp
    push_u16(rsrc, 0);  // MajorVersion
    push_u16(rsrc, 0);  // MinorVersion
    push_u16(rsrc, 0);  // NumberOfNamedEntries
    push_u16(rsrc, 3);  // NumberOfIdEntries (Icon=3, Version=16, Manifest=24)

    // Entries (3 entries * 8 bytes = 24 bytes): offsets 16, 24, 32
    // Directory offset L2 Dir 1 (Icon): 40
    // Directory offset L2 Dir 2 (Version): 40 + 16 + 2 * 8 = 72
    // Directory offset L2 Dir 3 (Manifest): 72 + 16 + 1 * 8 = 96
    constexpr std::uint32_t kL2IconOffset = 40;
    constexpr std::uint32_t kL2VersionOffset = 72;
    constexpr std::uint32_t kL2ManifestOffset = 96;

    // Entry 0: RT_ICON (3)
    push_u32(rsrc, 3);
    push_u32(rsrc, 0x80000000U | kL2IconOffset);

    // Entry 1: RT_VERSION (16)
    push_u32(rsrc, 16);
    push_u32(rsrc, 0x80000000U | kL2VersionOffset);

    // Entry 2: RT_MANIFEST (24)
    push_u32(rsrc, 24);
    push_u32(rsrc, 0x80000000U | kL2ManifestOffset);

    // --- Level 2: Icon Subdirectory (offset 40) ---
    push_u32(rsrc, 0);
    push_u32(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 0);  // named
    push_u16(rsrc, 2);  // id: 2 icon instances
    // Icon instance 1 (ID 1)
    push_u32(rsrc, 1);
    push_u32(rsrc, 0);  // leaf data offset (placeholder)
    // Icon instance 2 (ID 2)
    push_u32(rsrc, 2);
    push_u32(rsrc, 0);

    // --- Level 2: Version Subdirectory (offset 72) ---
    push_u32(rsrc, 0);
    push_u32(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 1);  // 1 version instance
    push_u32(rsrc, 1);
    push_u32(rsrc, 0);

    // --- Level 2: Manifest Subdirectory (offset 96) ---
    push_u32(rsrc, 0);
    push_u32(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 1);  // 1 manifest instance
    push_u32(rsrc, 1);
    push_u32(rsrc, 0);

    BuildSpec spec;
    spec.section_names = {".text", ".rsrc"};
    spec.section_data = {std::vector<std::byte>(0x10), rsrc};
    spec.resource_rva = kRsrcRva;
    spec.resource_size = static_cast<std::uint32_t>(rsrc.size());
    return build(spec);
}

// Constrói um PE com recurso nomeado (Level 1 named entry)
std::vector<std::byte> make_named_resource_pe() {
    constexpr std::uint32_t kRsrcRva = 0x2000;
    std::vector<std::byte> rsrc;

    // Root directory
    push_u32(rsrc, 0);
    push_u32(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 1);  // 1 named entry
    push_u16(rsrc, 0);  // 0 id entries

    // Named entry: offset da string = 64 (0x40)
    constexpr std::uint32_t kStringOffset = 64;
    push_u32(rsrc, 0x80000000U | kStringOffset);
    // Subdiretório offset 24
    push_u32(rsrc, 0x80000000U | 24);

    // Subdiretório (offset 24, tamanho 16 + 3 * 8 = 40 bytes -> termina no offset 64)
    push_u32(rsrc, 0);
    push_u32(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 3);  // 3 instances
    push_u32(rsrc, 1);
    push_u32(rsrc, 0);
    push_u32(rsrc, 2);
    push_u32(rsrc, 0);
    push_u32(rsrc, 3);
    push_u32(rsrc, 0);

    // String UTF-16LE no offset 64: "CUSTOM" (length 6)
    while (rsrc.size() < kStringOffset) {
        rsrc.push_back(std::byte{0});
    }
    push_u16(rsrc, 6);  // length in chars
    const char* str = "CUSTOM";
    for (int i = 0; i < 6; ++i) {
        push_u16(rsrc, static_cast<std::uint16_t>(str[i]));
    }

    BuildSpec spec;
    spec.section_names = {".text", ".rsrc"};
    spec.section_data = {std::vector<std::byte>(0x10), rsrc};
    spec.resource_rva = kRsrcRva;
    spec.resource_size = static_cast<std::uint32_t>(rsrc.size());
    return build(spec);
}

}  // namespace

TEST(ResourceInspectorTest, StandardTypeNamesMapping) {
    EXPECT_EQ(standard_resource_type_name(1), "cursor");
    EXPECT_EQ(standard_resource_type_name(2), "bitmap");
    EXPECT_EQ(standard_resource_type_name(3), "icon");
    EXPECT_EQ(standard_resource_type_name(4), "menu");
    EXPECT_EQ(standard_resource_type_name(5), "dialog");
    EXPECT_EQ(standard_resource_type_name(6), "string");
    EXPECT_EQ(standard_resource_type_name(9), "accelerator");
    EXPECT_EQ(standard_resource_type_name(10), "rcdata");
    EXPECT_EQ(standard_resource_type_name(12), "group_cursor");
    EXPECT_EQ(standard_resource_type_name(14), "group_icon");
    EXPECT_EQ(standard_resource_type_name(16), "version");
    EXPECT_EQ(standard_resource_type_name(24), "manifest");
    EXPECT_EQ(standard_resource_type_name(999), "type_999");
}

TEST(ResourceInspectorTest, EmptyPeHasNoResources) {
    const std::vector<std::byte> pe_data = build(BuildSpec{});
    const ParseResult parse_result = parse_pe(pe_data);
    ASSERT_EQ(parse_result.status, ParseStatus::Success);

    const ResourceInspectionResult result = inspect_pe_resources(pe_data, parse_result.info);
    EXPECT_FALSE(result.has_resources);
    EXPECT_TRUE(result.types.empty());
}

TEST(ResourceInspectorTest, InspectsValidResourceDirectory) {
    const std::vector<std::byte> pe_data = make_synthetic_rsrc_pe();
    const ParseResult parse_result = parse_pe(pe_data);
    ASSERT_EQ(parse_result.status, ParseStatus::Success);

    const ResourceInspectionResult result = inspect_pe_resources(pe_data, parse_result.info);
    EXPECT_TRUE(result.has_resources);
    ASSERT_EQ(result.types.size(), 3U);

    EXPECT_EQ(result.types[0].type_id, 3U);
    EXPECT_EQ(result.types[0].type_name, "icon");
    EXPECT_EQ(result.types[0].count, 2U);

    EXPECT_EQ(result.types[1].type_id, 16U);
    EXPECT_EQ(result.types[1].type_name, "version");
    EXPECT_EQ(result.types[1].count, 1U);

    EXPECT_EQ(result.types[2].type_id, 24U);
    EXPECT_EQ(result.types[2].type_name, "manifest");
    EXPECT_EQ(result.types[2].count, 1U);
}

TEST(ResourceInspectorTest, InspectsNamedResourceType) {
    const std::vector<std::byte> pe_data = make_named_resource_pe();
    const ParseResult parse_result = parse_pe(pe_data);
    ASSERT_EQ(parse_result.status, ParseStatus::Success);

    const ResourceInspectionResult result = inspect_pe_resources(pe_data, parse_result.info);
    EXPECT_TRUE(result.has_resources);
    ASSERT_EQ(result.types.size(), 1U);
    EXPECT_EQ(result.types[0].type_name, "CUSTOM");
    EXPECT_EQ(result.types[0].count, 3U);
}

TEST(ResourceInspectorTest, HandlesTruncatedResourceDirectorySafely) {
    std::vector<std::byte> pe_data = make_synthetic_rsrc_pe();
    const ParseResult parse_result = parse_pe(pe_data);
    ASSERT_EQ(parse_result.status, ParseStatus::Success);

    ASSERT_GE(parse_result.info.sections.size(), 2U);

    // Trunca o arquivo no meio da seção de recursos (menos de 16 bytes do cabeçalho de diretório)
    const std::size_t rsrc_raw_offset = parse_result.info.sections[1].raw_data_pointer;
    pe_data.resize(rsrc_raw_offset + 8);

    const ResourceInspectionResult result = inspect_pe_resources(pe_data, parse_result.info);
    // Não deve travar nem causar buffer overrun; retorna resultado seguro
    EXPECT_FALSE(result.has_resources);
}

TEST(ResourceInspectorTest, HandlesEmptyBytesSafely) {
    PeInfo info{};
    info.resource_directory_rva = 0x2000;
    info.resource_directory_size = 100;

    const ResourceInspectionResult result = inspect_pe_resources({}, info);
    EXPECT_FALSE(result.has_resources);
    EXPECT_TRUE(result.types.empty());
}

}  // namespace tradutorlinux::pe

