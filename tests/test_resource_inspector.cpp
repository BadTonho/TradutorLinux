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

// Constrói um PE com RT_MANIFEST contendo XML de manifesto real
std::vector<std::byte> make_pe_with_manifest(const std::string& manifest_xml) {
    constexpr std::uint32_t kRsrcRva = 0x2000;
    std::vector<std::byte> rsrc;

    // --- Root Directory (Level 1) ---
    push_u32(rsrc, 0);
    push_u32(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 0);  // 0 named
    push_u16(rsrc, 1);  // 1 ID entry: RT_MANIFEST (24)

    constexpr std::uint32_t kL2Offset = 24;
    push_u32(rsrc, 24);
    push_u32(rsrc, 0x80000000U | kL2Offset);

    // --- Level 2 Directory (Manifest instance ID 1) ---
    push_u32(rsrc, 0);
    push_u32(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 0);
    push_u16(rsrc, 1);  // 1 ID entry (ID 1)

    constexpr std::uint32_t kDataEntryOffset = 48;
    push_u32(rsrc, 1);
    push_u32(rsrc, kDataEntryOffset);  // Bit 31 = 0: points to IMAGE_RESOURCE_DATA_ENTRY

    // --- IMAGE_RESOURCE_DATA_ENTRY (offset 48, 16 bytes) ---
    constexpr std::uint32_t kXmlOffsetInRsrc = 64;
    const std::uint32_t xml_rva = kRsrcRva + kXmlOffsetInRsrc;
    const std::uint32_t xml_size = static_cast<std::uint32_t>(manifest_xml.size());

    push_u32(rsrc, xml_rva);   // OffsetToData (RVA)
    push_u32(rsrc, xml_size);  // Size
    push_u32(rsrc, 0);         // CodePage
    push_u32(rsrc, 0);         // Reserved

    // Append manifest XML at offset 64
    for (const char c : manifest_xml) {
        rsrc.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
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

TEST(ResourceInspectorTest, ManifestXmlParsingFull) {
    constexpr std::string_view kXml =
        R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
    <security>
      <requestedPrivileges>
        <requestedExecutionLevel level="asInvoker" uiAccess="false"/>
      </requestedPrivileges>
    </security>
  </trustInfo>
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
    </windowsSettings>
  </application>
  <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1">
    <application>
      <!-- Windows 10/11 -->
      <supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/>
      <!-- Windows 7 -->
      <supportedOS Id="{35138b9a-5d96-4fbd-8e2d-a2440225f93a}"/>
    </application>
  </compatibility>
</assembly>)";

    const ManifestInfo info = parse_manifest_xml(kXml);
    EXPECT_TRUE(info.has_manifest);
    EXPECT_EQ(info.requested_execution_level, "asInvoker");
    EXPECT_EQ(info.ui_access, "false");
    EXPECT_EQ(info.dpi_aware, "PerMonitorV2");
    ASSERT_EQ(info.supported_os.size(), 2U);
    EXPECT_EQ(info.supported_os[0], "Windows 10/11");
    EXPECT_EQ(info.supported_os[1], "Windows 7");
}

TEST(ResourceInspectorTest, ManifestXmlParsingElevation) {
    constexpr std::string_view kXml =
        R"(<assembly><trustInfo><security><requestedPrivileges>
<requestedExecutionLevel level='requireAdministrator' uiAccess='true'/>
</requestedPrivileges></security></trustInfo></assembly>)";

    const ManifestInfo info = parse_manifest_xml(kXml);
    EXPECT_TRUE(info.has_manifest);
    EXPECT_EQ(info.requested_execution_level, "requireAdministrator");
    EXPECT_EQ(info.ui_access, "true");
}

TEST(ResourceInspectorTest, ManifestXmlParsingBomAndDpiAware) {
    const std::string kXml =
        "\xEF\xBB\xBF<assembly><windowsSettings><dpiAware>true</dpiAware></windowsSettings></assembly>";

    const ManifestInfo info = parse_manifest_xml(kXml);
    EXPECT_TRUE(info.has_manifest);
    EXPECT_EQ(info.dpi_aware, "true");
}

TEST(ResourceInspectorTest, ManifestXmlParsingMalformedSafely) {
    const ManifestInfo empty_info = parse_manifest_xml("");
    EXPECT_FALSE(empty_info.has_manifest);

    const ManifestInfo malformed = parse_manifest_xml("<assembly><requestedExecutionLevel level=");
    EXPECT_TRUE(malformed.has_manifest);
    EXPECT_TRUE(malformed.requested_execution_level.empty());
}

TEST(ResourceInspectorTest, InspectsPeWithEmbeddedManifest) {
    const std::string kXml =
        "<assembly manifestVersion=\"1.0\"><trustInfo><security><requestedPrivileges>"
        "<requestedExecutionLevel level=\"asInvoker\" uiAccess=\"false\"/>"
        "</requestedPrivileges></security></trustInfo><application><windowsSettings>"
        "<dpiAwareness>PerMonitorV2</dpiAwareness></windowsSettings></application>"
        "<compatibility><application><supportedOS Id=\"{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}\"/>"
        "</application></compatibility></assembly>";

    const std::vector<std::byte> pe_data = make_pe_with_manifest(kXml);
    const ParseResult parse_result = parse_pe(pe_data);
    ASSERT_EQ(parse_result.status, ParseStatus::Success);

    const ResourceInspectionResult result = inspect_pe_resources(pe_data, parse_result.info);
    EXPECT_TRUE(result.has_resources);
    ASSERT_EQ(result.types.size(), 1U);
    EXPECT_EQ(result.types[0].type_name, "manifest");
    EXPECT_EQ(result.types[0].count, 1U);

    EXPECT_TRUE(result.manifest.has_manifest);
    EXPECT_EQ(result.manifest.requested_execution_level, "asInvoker");
    EXPECT_EQ(result.manifest.ui_access, "false");
    EXPECT_EQ(result.manifest.dpi_aware, "PerMonitorV2");
    ASSERT_EQ(result.manifest.supported_os.size(), 1U);
    EXPECT_EQ(result.manifest.supported_os[0], "Windows 10/11");
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

TEST(ResourceInspectorTest, ParseVersionInfoFull) {
    std::vector<std::byte> data;
    // Cabeçalho VS_VERSIONINFO
    push_u16(data, 0);  // wLength placeholder
    push_u16(data, 52); // wValueLength (sizeof VS_FIXEDFILEINFO)
    push_u16(data, 0);  // wType (binary)

    // szKey = "VS_VERSION_INFO"
    auto push_utf16_str = [](std::vector<std::byte>& out, std::string_view text) {
        for (char c : text) {
            out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
            out.push_back(std::byte{0});
        }
        out.push_back(std::byte{0});
        out.push_back(std::byte{0});
    };

    push_utf16_str(data, "VS_VERSION_INFO");
    while ((data.size() % 4) != 0) {
        data.push_back(std::byte{0});
    }

    // VS_FIXEDFILEINFO (52 bytes)
    push_u32(data, 0xFEEF04BD);              // dwSignature
    push_u32(data, 0x00010000);              // dwStrucVersion
    push_u32(data, (7 << 16) | 1);           // dwFileVersionMS: 7.1
    push_u32(data, (2 << 16) | 3);           // dwFileVersionLS: 2.3
    push_u32(data, (7 << 16) | 0);           // dwProductVersionMS: 7.0
    push_u32(data, 0);                       // dwProductVersionLS: 0.0
    // Restante do fixed file info (8 * 4 = 32 bytes)
    for (int i = 0; i < 8; ++i) {
        push_u32(data, 0);
    }
    while ((data.size() % 4) != 0) {
        data.push_back(std::byte{0});
    }

    auto push_string_struct = [&](std::string_view key, std::string_view value) {
        const std::size_t start = data.size();
        push_u16(data, 0);
        push_u16(data, static_cast<std::uint16_t>(value.size() + 1));
        push_u16(data, 1);
        push_utf16_str(data, key);
        while ((data.size() % 4) != 0) {
            data.push_back(std::byte{0});
        }
        push_utf16_str(data, value);
        while ((data.size() % 4) != 0) {
            data.push_back(std::byte{0});
        }
        write_u16(data, start, static_cast<std::uint16_t>(data.size() - start));
    };

    push_string_struct("ProductName", "TradutorLinux App");
    push_string_struct("ProductVersion", "7.10.2");
    push_string_struct("CompanyName", "TradutorLinux Team");
    push_string_struct("FileDescription", "Compatibility App");

    write_u16(data, 0, static_cast<std::uint16_t>(data.size()));

    const ProductVersionInfo vinfo = parse_version_info(data);
    EXPECT_TRUE(vinfo.has_version_info);
    EXPECT_EQ(vinfo.product_name, "TradutorLinux App");
    EXPECT_EQ(vinfo.product_version, "7.10.2");
    EXPECT_EQ(vinfo.file_version, "7.1.2.3");
    EXPECT_EQ(vinfo.company_name, "TradutorLinux Team");
    EXPECT_EQ(vinfo.file_description, "Compatibility App");
}

TEST(ResourceInspectorTest, ParseVersionInfoFixedOnly) {
    std::vector<std::byte> data(64, std::byte{0});
    // Signature at offset 4
    write_u32(data, 4, 0xFEEF04BD);
    // dwFileVersionMS at offset 12: 3.4
    write_u32(data, 12, (3 << 16) | 4);
    // dwFileVersionLS at offset 16: 5.6
    write_u32(data, 16, (5 << 16) | 6);

    const ProductVersionInfo vinfo = parse_version_info(data);
    EXPECT_TRUE(vinfo.has_version_info);
    EXPECT_EQ(vinfo.file_version, "3.4.5.6");
    EXPECT_TRUE(vinfo.product_name.empty());
}

TEST(ResourceInspectorTest, ParseVersionInfoEmptySafely) {
    EXPECT_FALSE(parse_version_info({}).has_version_info);
    std::vector<std::byte> short_data(20, std::byte{0});
    EXPECT_FALSE(parse_version_info(short_data).has_version_info);
}

}  // namespace tradutorlinux::pe


