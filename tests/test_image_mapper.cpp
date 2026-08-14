#include "pe_builder.hpp"
#include "tradutorlinux/loader/image_mapper.hpp"
#include "tradutorlinux/pe/pe_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace tradutorlinux::loader {
namespace {

using namespace tradutorlinux::pe::testutil;

constexpr std::size_t kTestBufferSize = 0x4000;
constexpr std::uint64_t kTestPreferredBase = 0x10000000ULL;

std::vector<std::byte> read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    EXPECT_TRUE(input.good()) << "fixture não encontrada: " << path;
    std::vector<std::byte> bytes;
    std::istreambuf_iterator<char> iterator{input};
    const std::istreambuf_iterator<char> end;
    for (; iterator != end; ++iterator) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(*iterator)));
    }
    return bytes;
}

std::string fixture_path(const std::string& name) {
    return std::string(TL_FIXTURE_OUTPUT_DIRECTORY) + "/" + name + ".exe";
}

void write_le_u64(std::vector<std::byte>& bytes, const std::size_t offset,
                  const std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (8 * index)) & 0xFFULL);
    }
}

void write_le_u32(std::vector<std::byte>& bytes, const std::size_t offset,
                  const std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (8 * index)) & 0xFFU);
    }
}

std::uint64_t read_le_u64(const std::vector<std::byte>& bytes, const std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(bytes[offset + index]))
                 << (8 * index);
    }
    return value;
}

std::uint32_t read_le_u32(const std::vector<std::byte>& bytes, const std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + index]))
                 << (8 * index);
    }
    return value;
}

std::uint64_t read_le_u64_at(const std::byte* memory, const std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(memory[offset + index]))
                 << (8 * index);
    }
    return value;
}

// Bloco de base relocation no formato PE (page_rva + block_size + entries).
std::vector<std::byte> make_reloc_block(const std::uint32_t page_rva,
                                        const std::vector<std::uint16_t>& entries) {
    std::vector<std::byte> block;
    push_u32(block, page_rva);
    push_u32(block, static_cast<std::uint32_t>(8 + entries.size() * 2));
    for (const std::uint16_t entry : entries) {
        push_u16(block, entry);
    }
    return block;
}

// Mapeia `bytes` parseados com um preferred_base ocupado (endereço de pilha),
// forçando o fallback e consequentemente delta != 0.
MapResult map_relocated(const pe::PeInfo& info, const std::vector<std::byte>& bytes) {
    std::uint8_t stack_sentinel = 0;
    const std::uint64_t occupied =
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&stack_sentinel));
    MapOptions options;
    options.preferred_base = occupied;
    return map_image(info, bytes, options);
}

class ImageMapperTest : public ::testing::Test {
protected:
    void SetUp() override {
        file_bytes = read_file(fixture_path("tl_nop"));
        ASSERT_FALSE(file_bytes.empty());
    }

    std::vector<std::byte> file_bytes;
};

class RelocFixtureTest : public ::testing::Test {
protected:
    void SetUp() override {
        file_bytes = read_file(fixture_path("tl_reloc"));
        ASSERT_FALSE(file_bytes.empty());
    }

    std::vector<std::byte> file_bytes;
};

// Procura em /proc/self/maps a linha que cobre o endereço `address` e retorna
// o campo de permissões (ex.: "r-xp"). Retorna std::nullopt se não houver.
std::optional<std::string> maps_permissions_for(const std::uintptr_t address) {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        const std::string::size_type space = line.find(' ');
        if (space == std::string::npos) {
            continue;
        }
        const std::string range = line.substr(0, space);
        const std::string::size_type dash = range.find('-');
        if (dash == std::string::npos) {
            continue;
        }
        const std::uintptr_t start = static_cast<std::uintptr_t>(
            std::stoull(range.substr(0, dash), nullptr, 16));
        const std::uintptr_t end = static_cast<std::uintptr_t>(
            std::stoull(range.substr(dash + 1), nullptr, 16));
        if (address >= start && address < end) {
            return line.substr(space + 1, 4);
        }
    }
    return std::nullopt;
}

TEST(ApplyRelocations, EmptyDirectorySucceeds) {
    std::vector<std::byte> image(kTestBufferSize, std::byte{0});
    const RelocationResult result = apply_relocations(image, RelocationDirectory{0x1000, 0}, 0x1000);
    EXPECT_EQ(result.status, MapStatus::Success);
    EXPECT_EQ(result.applied, 0U);
}

TEST(ApplyRelocations, AppliesDir64AndHighLowAndSkipsAbsolute) {
    std::vector<std::byte> image(kTestBufferSize, std::byte{0});
    const std::vector<std::byte> block =
        make_reloc_block(0x2000, {0xA010, 0x3200, 0x0008});
    std::copy(block.begin(), block.end(), image.begin() + 0x1000);
    write_le_u64(image, 0x2010, 0x140000000ULL);
    write_le_u32(image, 0x2200, 0x10000000U);

    const RelocationResult result = apply_relocations(image, RelocationDirectory{0x1000, static_cast<std::uint32_t>(block.size())}, 0x1000);

    ASSERT_EQ(result.status, MapStatus::Success);
    EXPECT_EQ(result.applied, 2U);
    EXPECT_EQ(read_le_u64(image, 0x2010), 0x140001000ULL);
    EXPECT_EQ(read_le_u32(image, 0x2200), 0x10001000U);
}

TEST(ApplyRelocations, AppliesNegativeDelta) {
    std::vector<std::byte> image(kTestBufferSize, std::byte{0});
    const std::vector<std::byte> block = make_reloc_block(0x2000, {0xA010, 0x3200});
    std::copy(block.begin(), block.end(), image.begin() + 0x1000);
    write_le_u64(image, 0x2010, 0x140000000ULL);
    write_le_u32(image, 0x2200, 0x10000000U);

    const RelocationResult result =
        apply_relocations(image, RelocationDirectory{0x1000, static_cast<std::uint32_t>(block.size())},
                          static_cast<std::int64_t>(-0x1000));

    ASSERT_EQ(result.status, MapStatus::Success);
    EXPECT_EQ(result.applied, 2U);
    EXPECT_EQ(read_le_u64(image, 0x2010), 0x13FFFF000ULL);
    EXPECT_EQ(read_le_u32(image, 0x2200), 0x0FFFF000U);
}

TEST(ApplyRelocations, RejectsUnsupportedType) {
    std::vector<std::byte> image(kTestBufferSize, std::byte{0});
    const std::vector<std::byte> block = make_reloc_block(0x2000, {0x1010});
    std::copy(block.begin(), block.end(), image.begin() + 0x1000);

    const RelocationResult result = apply_relocations(image, RelocationDirectory{0x1000, static_cast<std::uint32_t>(block.size())}, 0x1000);

    EXPECT_EQ(result.status, MapStatus::InvalidImage);
}

TEST(ApplyRelocations, RejectsDirectoryOutsideImage) {
    std::vector<std::byte> image(kTestBufferSize, std::byte{0});
    const RelocationResult result = apply_relocations(image, RelocationDirectory{0x5000, 8}, 0x1000);
    EXPECT_EQ(result.status, MapStatus::InvalidImage);
}

TEST(ApplyRelocations, RejectsTargetOutsideImage) {
    std::vector<std::byte> image(kTestBufferSize, std::byte{0});
    const std::vector<std::byte> block = make_reloc_block(0x3000, {0xAFFF});
    std::copy(block.begin(), block.end(), image.begin() + 0x1000);

    const RelocationResult result = apply_relocations(image, RelocationDirectory{0x1000, static_cast<std::uint32_t>(block.size())}, 0x1000);

    EXPECT_EQ(result.status, MapStatus::InvalidImage);
}

TEST(ApplyRelocations, RejectsBlockSizeBelowHeader) {
    std::vector<std::byte> image(kTestBufferSize, std::byte{0});
    std::vector<std::byte> block = make_reloc_block(0x2000, {0xA000});
    write_le_u32(block, 4, 4);
    std::copy(block.begin(), block.end(), image.begin() + 0x1000);

    const RelocationResult result = apply_relocations(image, RelocationDirectory{0x1000, static_cast<std::uint32_t>(block.size())}, 0x1000);

    EXPECT_EQ(result.status, MapStatus::InvalidImage);
}

TEST(ApplyRelocations, RejectsResidualDirectoryBytes) {
    std::vector<std::byte> image(kTestBufferSize, std::byte{0});
    const std::vector<std::byte> block = make_reloc_block(0x2000, {0xA000});
    std::copy(block.begin(), block.end(), image.begin() + 0x1000);

    const RelocationResult result = apply_relocations(
        image, RelocationDirectory{0x1000, static_cast<std::uint32_t>(block.size() + 2)}, 0x1000);

    EXPECT_EQ(result.status, MapStatus::InvalidImage);
}

TEST(ApplyRelocations, RejectsBlockExceedingDirectory) {
    std::vector<std::byte> image(kTestBufferSize, std::byte{0});
    const std::vector<std::byte> block = make_reloc_block(0x2000, {0xA000});
    std::copy(block.begin(), block.end(), image.begin() + 0x1000);

    const RelocationResult result = apply_relocations(
        image, RelocationDirectory{0x1000, static_cast<std::uint32_t>(block.size() - 2)}, 0x1000);

    EXPECT_EQ(result.status, MapStatus::InvalidImage);
}

TEST_F(ImageMapperTest, MapsMinimalAtPreferredBase) {
    const std::vector<std::byte> bytes = make_minimal();
    const pe::ParseResult parse = pe::parse_pe(bytes);
    ASSERT_EQ(parse.status, pe::ParseStatus::Success);

    MapOptions options;
    options.preferred_base = kTestPreferredBase;
    MapResult result = map_image(parse.info, bytes, options);

    ASSERT_EQ(result.status, MapStatus::Success);
    EXPECT_EQ(result.image.base, kTestPreferredBase);
    EXPECT_EQ(result.image.delta, 0);
    EXPECT_EQ(result.image.applied_relocations, 0U);
    ASSERT_NE(result.image.memory, nullptr);
    EXPECT_EQ(std::to_integer<unsigned char>(result.image.memory[0]), 0x4D);
    EXPECT_EQ(result.image.regions.size(), 2U);
    unmap_image(result.image);
}

TEST_F(ImageMapperTest, CopiesHeadersAndSections) {
    const pe::ParseResult parse = pe::parse_pe(file_bytes);
    ASSERT_EQ(parse.status, pe::ParseStatus::Success);

    MapResult result = map_image(parse.info, file_bytes);

    ASSERT_EQ(result.status, MapStatus::Success);
    ASSERT_NE(result.image.memory, nullptr);
    EXPECT_EQ(std::to_integer<unsigned char>(result.image.memory[0]), 0x4D);
    for (const pe::SectionInfo& section : parse.info.sections) {
        if (section.raw_data_size == 0) {
            continue;
        }
        for (std::size_t index = 0; index < section.raw_data_size; ++index) {
            const auto mapped = std::to_integer<unsigned char>(
                result.image.memory[section.virtual_address + index]);
            const auto original = std::to_integer<unsigned char>(
                file_bytes[section.raw_data_pointer + index]);
            EXPECT_EQ(mapped, original) << "bytes divergem na seção " << section.name;
        }
    }
    unmap_image(result.image);
}

TEST_F(ImageMapperTest, AppliesProtectionsFromSectionCharacteristics) {
    const std::vector<std::byte> bytes = make_minimal();
    const pe::ParseResult parse = pe::parse_pe(bytes);
    ASSERT_EQ(parse.status, pe::ParseStatus::Success);

    MapOptions options;
    options.preferred_base = kTestPreferredBase;
    MapResult result = map_image(parse.info, bytes, options);

    ASSERT_EQ(result.status, MapStatus::Success);
    const std::optional<std::string> header_perms =
        maps_permissions_for(static_cast<std::uintptr_t>(kTestPreferredBase));
    ASSERT_TRUE(header_perms.has_value());
    EXPECT_EQ(*header_perms, "r--p");

    const std::optional<std::string> text_perms =
        maps_permissions_for(static_cast<std::uintptr_t>(kTestPreferredBase) + 0x1000);
    ASSERT_TRUE(text_perms.has_value());
    EXPECT_EQ(*text_perms, "r-xp");

    const std::optional<std::string> rdata_perms =
        maps_permissions_for(static_cast<std::uintptr_t>(kTestPreferredBase) + 0x2000);
    ASSERT_TRUE(rdata_perms.has_value());
    EXPECT_EQ(*rdata_perms, "r--p");

    unmap_image(result.image);
}

TEST_F(ImageMapperTest, DowngradesWritableExecutableSectionToReadWrite) {
    BuildSpec spec;
    spec.section_count = 1;
    spec.section_names = {".text"};
    std::vector<std::byte> bytes = build(spec);
    write_u32(bytes, 0x16C, 0xE0000020);  // characteristics: READ | WRITE | EXEC
    const pe::ParseResult parse = pe::parse_pe(bytes);
    ASSERT_EQ(parse.status, pe::ParseStatus::Success);

    MapOptions options;
    options.preferred_base = kTestPreferredBase;
    MapResult result = map_image(parse.info, bytes, options);

    ASSERT_EQ(result.status, MapStatus::Success);
    const std::optional<std::string> section_perms =
        maps_permissions_for(static_cast<std::uintptr_t>(kTestPreferredBase) + 0x1000);
    ASSERT_TRUE(section_perms.has_value());
    EXPECT_EQ(*section_perms, "rw-p");

    const std::optional<std::string> header_perms =
        maps_permissions_for(static_cast<std::uintptr_t>(kTestPreferredBase));
    ASSERT_TRUE(header_perms.has_value());
    EXPECT_EQ(*header_perms, "r--p");

    unmap_image(result.image);
}

TEST_F(ImageMapperTest, RejectsOverlappingSections) {
    BuildSpec spec;
    spec.section_count = 2;
    spec.section_names = {".text", ".text2"};
    const std::vector<std::byte> bytes = build(spec);
    std::vector<std::byte> patched = bytes;
    write_u32(patched, 0x17C, 0x1000);  // section1 virtual_address sobreposto ao .text
    const pe::ParseResult parse = pe::parse_pe(patched);
    ASSERT_EQ(parse.status, pe::ParseStatus::Success);

    MapResult result = map_image(parse.info, patched);

    EXPECT_EQ(result.status, MapStatus::InvalidImage);
}

TEST_F(ImageMapperTest, RejectsSectionBeyondImage) {
    BuildSpec spec;
    spec.size_of_image = 0x2000;
    const std::vector<std::byte> bytes = build(spec);
    const pe::ParseResult parse = pe::parse_pe(bytes);
    ASSERT_EQ(parse.status, pe::ParseStatus::Success);

    MapResult result = map_image(parse.info, bytes);

    EXPECT_EQ(result.status, MapStatus::InvalidImage);
}

TEST_F(ImageMapperTest, UnmapsImageAndResetsState) {
    const pe::ParseResult parse = pe::parse_pe(file_bytes);
    ASSERT_EQ(parse.status, pe::ParseStatus::Success);

    MapResult result = map_image(parse.info, file_bytes);
    ASSERT_EQ(result.status, MapStatus::Success);
    ASSERT_NE(result.image.memory, nullptr);

    unmap_image(result.image);

    EXPECT_EQ(result.image.memory, nullptr);
    EXPECT_EQ(result.image.size, 0U);
    EXPECT_EQ(result.image.base, 0U);
    EXPECT_TRUE(result.image.regions.empty());
}

TEST_F(RelocFixtureTest, MapsAtPreferredBaseWithoutRelocationsWhenBaseMatches) {
    const pe::ParseResult parse = pe::parse_pe(file_bytes);
    ASSERT_EQ(parse.status, pe::ParseStatus::Success);

    MapOptions options;
    options.preferred_base = parse.info.image_base;
    MapResult result = map_image(parse.info, file_bytes, options);

    ASSERT_EQ(result.status, MapStatus::Success);
    if (result.image.delta == 0) {
        EXPECT_EQ(result.image.base, parse.info.image_base);
        EXPECT_EQ(result.image.applied_relocations, 0U);
    } else {
        EXPECT_GE(result.image.applied_relocations, 1U);
    }
    ASSERT_NE(result.image.memory, nullptr);
    unmap_image(result.image);
}

TEST_F(RelocFixtureTest, AppliesRelocationsWhenRelocatedAndAdjustsPointer) {
    const pe::ParseResult parse = pe::parse_pe(file_bytes);
    ASSERT_EQ(parse.status, pe::ParseStatus::Success);

    MapResult result = map_relocated(parse.info, file_bytes);

    ASSERT_EQ(result.status, MapStatus::Success);
    ASSERT_NE(result.image.memory, nullptr);
    EXPECT_NE(result.image.delta, 0);
    EXPECT_GE(result.image.applied_relocations, 1U);

    const std::uint64_t target = read_le_u64_at(result.image.memory, 0x2000);
    const std::uint64_t expected =
        0x140001000ULL + static_cast<std::uint64_t>(result.image.delta);
    EXPECT_EQ(target, expected);
    unmap_image(result.image);
}

TEST_F(ImageMapperTest, RejectsRelocationTargetOutsideMappedImage) {
    const std::vector<std::byte> block = make_reloc_block(0x3000, {0xA000});
    BuildSpec spec;
    spec.reloc_rva = 0x2000;
    spec.reloc_size = static_cast<std::uint32_t>(block.size());
    spec.section_data = {std::vector<std::byte>(0x10), block};
    const std::vector<std::byte> bytes = build(spec);
    const pe::ParseResult parse = pe::parse_pe(bytes);
    ASSERT_EQ(parse.status, pe::ParseStatus::Success);

    const MapResult result = map_relocated(parse.info, bytes);

    EXPECT_EQ(result.status, MapStatus::InvalidImage);
}

}  // namespace
}  // namespace tradutorlinux::loader
