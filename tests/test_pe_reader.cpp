#include "tradutorlinux/pe/pe_reader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace tradutorlinux::pe {
namespace {

// ---------------------------------------------------------------------------
// Helpers to build and mutate synthetic PE images in memory.
// ---------------------------------------------------------------------------

constexpr std::size_t kDosHeaderSize = 64;
constexpr std::size_t kLfanewOffset = 60;
constexpr std::size_t kNtOffset = 0x40;
constexpr std::size_t kMachineOffset = 0x44;
constexpr std::size_t kSectionCountOffset = 0x46;
constexpr std::size_t kOptionalSizeOffset = 0x54;
constexpr std::size_t kOptionalMagicOffset = 0x58;
constexpr std::size_t kRelocDirSizeOffset = 0xF4;
constexpr std::size_t kSection0RawSizeOffset = 0x148 + 16;

constexpr std::uint16_t kMachineAmd64 = 0x8664;

void push_u16(std::vector<std::byte>& out, const std::uint16_t value) {
    out.push_back(static_cast<std::byte>(value & 0xFF));
    out.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
}

void push_u32(std::vector<std::byte>& out, const std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFF));
    }
}

void push_u64(std::vector<std::byte>& out, const std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFF));
    }
}

void push_cstr(std::vector<std::byte>& out, const char* text) {
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(*cursor)));
    }
    out.push_back(std::byte{0});
}

void write_u16(std::vector<std::byte>& bytes, const std::size_t offset,
               const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::byte>((value >> 8) & 0xFF);
}

void write_u32(std::vector<std::byte>& bytes, const std::size_t offset,
               const std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        bytes[offset + static_cast<std::size_t>(shift / 8)] =
            static_cast<std::byte>((value >> shift) & 0xFF);
    }
}

struct BuildSpec {
    std::uint16_t machine{kMachineAmd64};
    std::uint16_t section_count{2};
    std::uint16_t optional_size{240};
    std::uint32_t entry_point{0x1000};
    std::uint64_t image_base{0x140000000ULL};
    std::uint32_t size_of_image{0x3000};
    std::uint32_t number_of_rva_and_sizes{16};
    std::uint32_t import_rva{};
    std::uint32_t import_size{};
    std::uint32_t reloc_rva{};
    std::uint32_t reloc_size{};
    std::vector<std::string> section_names{".text", ".rdata"};
    std::vector<std::vector<std::byte>> section_data;
};

std::vector<std::byte> build(const BuildSpec& spec) {
    std::vector<std::byte> out;
    out.resize(kDosHeaderSize, std::byte{0});
    write_u16(out, 0, 0x5A4D);
    write_u32(out, kLfanewOffset, kNtOffset);

    push_u32(out, 0x00004550);  // "PE\0\0" signature
    push_u16(out, spec.machine);
    push_u16(out, spec.section_count);
    push_u32(out, 0);
    push_u32(out, 0);
    push_u32(out, 0);
    push_u16(out, spec.optional_size);
    push_u16(out, 0x22);

    const std::size_t opt_start = out.size();
    push_u16(out, 0x20B);
    push_u16(out, 0);
    push_u32(out, 0);
    push_u32(out, 0);
    push_u32(out, 0);
    push_u32(out, spec.entry_point);
    push_u32(out, 0x1000);
    push_u64(out, spec.image_base);
    push_u32(out, 0x1000);
    push_u32(out, 0x200);
    push_u16(out, 0);
    push_u16(out, 0);
    push_u16(out, 0);
    push_u16(out, 0);
    push_u16(out, 5);
    push_u16(out, 2);
    push_u32(out, 0);
    push_u32(out, spec.size_of_image);
    push_u32(out, 0x200);
    push_u32(out, 0);
    push_u16(out, 3);
    push_u16(out, 0);
    push_u64(out, 0x100000);
    push_u64(out, 0x1000);
    push_u64(out, 0x100000);
    push_u64(out, 0x1000);
    push_u32(out, 0);
    push_u32(out, spec.number_of_rva_and_sizes);
    for (std::size_t index = 0; index < 16; ++index) {
        push_u32(out, 0);
        push_u32(out, 0);
    }
    if (spec.import_rva != 0 || spec.import_size != 0) {
        write_u32(out, opt_start + 112 + 8, spec.import_rva);
        write_u32(out, opt_start + 112 + 8 + 4, spec.import_size);
    }
    if (spec.reloc_rva != 0 || spec.reloc_size != 0) {
        write_u32(out, opt_start + 112 + 40, spec.reloc_rva);
        write_u32(out, opt_start + 112 + 40 + 4, spec.reloc_size);
    }
    if (out.size() - opt_start < spec.optional_size) {
        out.resize(opt_start + spec.optional_size, std::byte{0});
    }

    for (std::size_t index = 0; index < spec.section_count; ++index) {
        const std::string name = index < spec.section_names.size() ? spec.section_names[index] : "";
        for (std::size_t byte_index = 0; byte_index < 8; ++byte_index) {
            out.push_back(byte_index < name.size()
                              ? static_cast<std::byte>(static_cast<unsigned char>(name[byte_index]))
                              : std::byte{0});
        }
        const std::uint32_t data_size =
            index < spec.section_data.size()
                ? static_cast<std::uint32_t>(spec.section_data[index].size())
                : 0;
        push_u32(out, data_size);
        push_u32(out, 0x1000 + static_cast<std::uint32_t>(index) * 0x1000);
        push_u32(out, 0x200);
        push_u32(out, 0x200 + static_cast<std::uint32_t>(index) * 0x200);
        push_u32(out, 0);
        push_u32(out, 0);
        push_u16(out, 0);
        push_u16(out, 0);
        push_u32(out, index == 0 ? 0x60000020 : 0x40000040);
    }
    out.resize(0x200 + static_cast<std::size_t>(spec.section_count) * 0x200, std::byte{0});

    for (std::size_t index = 0; index < spec.section_count; ++index) {
        const std::size_t raw_pointer = 0x200 + index * 0x200;
        if (index < spec.section_data.size()) {
            const std::vector<std::byte>& data = spec.section_data[index];
            std::copy(data.begin(), data.end(), out.begin() + static_cast<std::ptrdiff_t>(raw_pointer));
        }
    }
    return out;
}

std::vector<std::byte> make_minimal() {
    return build(BuildSpec{});
}

// A valid PE whose .rdata section carries one import descriptor for FAKE.dll
// with one by-name symbol (PrintA, hint 0x1234) and one by-ordinal (5).
std::vector<std::byte> make_import_pe() {
    constexpr std::uint32_t kDataRva = 0x2000;
    std::vector<std::byte> data;
    // descriptor0
    push_u32(data, kDataRva + 40);  // OriginalFirstThunk -> thunk table
    push_u32(data, 0);              // TimeDateStamp
    push_u32(data, 0);              // ForwarderChain
    push_u32(data, kDataRva + 73);  // Name -> "FAKE.dll"
    push_u32(data, kDataRva + 82);  // FirstThunk -> IAT
    // descriptor1 (terminator)
    push_u32(data, 0);
    push_u32(data, 0);
    push_u32(data, 0);
    push_u32(data, 0);
    push_u32(data, 0);
    // thunk table
    push_u32(data, kDataRva + 64);       // thunk0 -> by-name symbol
    push_u32(data, 0);
    push_u64(data, 0x8000000000000005ULL);  // thunk1 -> ordinal 5
    push_u64(data, 0);                      // thunk2 terminator
    // by-name symbol: hint then "PrintA\0"
    push_u16(data, 0x1234);
    push_cstr(data, "PrintA");
    // dll name
    push_cstr(data, "FAKE.dll");
    // IAT
    push_u32(data, kDataRva + 64);
    push_u32(data, 0);
    push_u64(data, 0x8000000000000005ULL);
    push_u64(data, 0);

    BuildSpec spec;
    spec.section_names = {".text", ".rdata"};
    spec.section_data = {std::vector<std::byte>(0x10), data};
    spec.import_rva = kDataRva;
    spec.import_size = 40;
    return build(spec);
}

std::vector<std::byte> make_reloc_pe() {
    constexpr std::uint32_t kDataRva = 0x2000;
    std::vector<std::byte> data(12, std::byte{0});
    write_u32(data, 0, 0x1000);  // page RVA
    write_u32(data, 4, 12);      // block size
    write_u16(data, 8, static_cast<std::uint16_t>((3U << 12) | 0x10));
    write_u16(data, 10, static_cast<std::uint16_t>((0U << 12) | 0x20));

    BuildSpec spec;
    spec.section_names = {".text", ".rdata"};
    spec.section_data = {std::vector<std::byte>(0x10), data};
    spec.reloc_rva = kDataRva;
    spec.reloc_size = 12;
    return build(spec);
}

std::vector<std::byte> read_file(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    std::vector<std::byte> bytes;
    std::istreambuf_iterator<char> iterator{stream};
    const std::istreambuf_iterator<char> end;
    for (; iterator != end; ++iterator) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(*iterator)));
    }
    return bytes;
}

// ---------------------------------------------------------------------------
// Valid inputs
// ---------------------------------------------------------------------------

TEST(PeReaderTest, ParsesMinimalSyntheticImage) {
    const std::vector<std::byte> bytes = make_minimal();

    const ParseResult result = parse_pe(bytes);

    ASSERT_EQ(result.status, ParseStatus::Success) << result.error_message;
    EXPECT_TRUE(result.info.is_pe32_plus);
    EXPECT_EQ(result.info.machine, kMachineAmd64);
    EXPECT_EQ(result.info.address_of_entry_point, 0x1000);
    EXPECT_EQ(result.info.image_base, 0x140000000ULL);
    EXPECT_EQ(result.info.number_of_sections, 2);
    EXPECT_EQ(result.info.sections.size(), 2);
    EXPECT_EQ(result.info.sections[0].name, ".text");
    EXPECT_EQ(result.info.sections[1].name, ".rdata");
    EXPECT_TRUE(result.info.imports.empty());
    EXPECT_TRUE(result.info.relocations.empty());
}

TEST(PeReaderTest, ParsesImportsByNameAndOrdinal) {
    const std::vector<std::byte> bytes = make_import_pe();

    const ParseResult result = parse_pe(bytes);

    ASSERT_EQ(result.status, ParseStatus::Success) << result.error_message;
    ASSERT_EQ(result.info.imports.size(), 1);
    const ImportedDll& dll = result.info.imports[0];
    EXPECT_EQ(dll.name, "FAKE.dll");
    ASSERT_EQ(dll.symbols.size(), 2);
    EXPECT_FALSE(dll.symbols[0].by_ordinal);
    EXPECT_EQ(dll.symbols[0].name, "PrintA");
    EXPECT_TRUE(dll.symbols[1].by_ordinal);
    EXPECT_EQ(dll.symbols[1].ordinal, 5);
}

TEST(PeReaderTest, ParsesBaseRelocations) {
    const std::vector<std::byte> bytes = make_reloc_pe();

    const ParseResult result = parse_pe(bytes);

    ASSERT_EQ(result.status, ParseStatus::Success) << result.error_message;
    ASSERT_EQ(result.info.relocations.size(), 1);
    const BaseRelocBlock& block = result.info.relocations[0];
    EXPECT_EQ(block.page_rva, 0x1000);
    ASSERT_EQ(block.entries.size(), 2);
    EXPECT_EQ(block.entries[0].type, 3);
    EXPECT_EQ(block.entries[0].offset, 0x10);
    EXPECT_EQ(block.entries[1].type, 0);
    EXPECT_EQ(block.entries[1].offset, 0x20);
}

TEST(PeReaderTest, ParsesHelloFixture) {
    const std::filesystem::path path =
        std::filesystem::path{TL_FIXTURE_OUTPUT_DIRECTORY} / "tl_hello.exe";
    const std::vector<std::byte> bytes = read_file(path);

    ASSERT_FALSE(bytes.empty());
    const ParseResult result = parse_pe(bytes);

    ASSERT_EQ(result.status, ParseStatus::Success) << result.error_message;
    EXPECT_EQ(result.info.machine, kMachineAmd64);
    EXPECT_EQ(result.info.address_of_entry_point, 0x1000);
    EXPECT_EQ(result.info.image_base, 0x140000000ULL);
    EXPECT_EQ(result.info.number_of_sections, 3);
    ASSERT_EQ(result.info.sections.size(), 3);
    EXPECT_EQ(result.info.sections[0].name, ".text");
    EXPECT_EQ(result.info.sections[0].virtual_address, 0x1000);
    ASSERT_EQ(result.info.imports.size(), 1);
    EXPECT_EQ(result.info.imports[0].name, "KERNEL32.dll");
    ASSERT_EQ(result.info.imports[0].symbols.size(), 3);
    EXPECT_EQ(result.info.imports[0].symbols[0].name, "ExitProcess");
    EXPECT_EQ(result.info.imports[0].symbols[1].name, "GetStdHandle");
    EXPECT_EQ(result.info.imports[0].symbols[2].name, "WriteFile");
    EXPECT_TRUE(result.info.relocations.empty());
}

TEST(PeReaderTest, ParsesNopFixtureWithoutImports) {
    const std::filesystem::path path =
        std::filesystem::path{TL_FIXTURE_OUTPUT_DIRECTORY} / "tl_nop.exe";
    const std::vector<std::byte> bytes = read_file(path);

    ASSERT_FALSE(bytes.empty());
    const ParseResult result = parse_pe(bytes);

    ASSERT_EQ(result.status, ParseStatus::Success) << result.error_message;
    EXPECT_EQ(result.info.number_of_sections, 3);
    EXPECT_TRUE(result.info.imports.empty());
    EXPECT_TRUE(result.info.relocations.empty());
}

// ---------------------------------------------------------------------------
// Malformed and unsupported inputs
// ---------------------------------------------------------------------------

TEST(PeReaderTest, RejectsEmptyInputAsTruncated) {
    const std::vector<std::byte> bytes;

    const ParseResult result = parse_pe(bytes);

    EXPECT_EQ(result.status, ParseStatus::Truncated);
}

TEST(PeReaderTest, RejectsShortInputAsTruncated) {
    std::vector<std::byte> bytes = make_minimal();
    bytes.resize(30);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Truncated);
}

TEST(PeReaderTest, RejectsBadDosMagic) {
    std::vector<std::byte> bytes = make_minimal();
    write_u16(bytes, 0, 0);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, RejectsLfanewOutsideFile) {
    std::vector<std::byte> bytes = make_minimal();
    write_u32(bytes, kLfanewOffset, 0x1000);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Truncated);
}

TEST(PeReaderTest, RejectsBadPeSignature) {
    std::vector<std::byte> bytes = make_minimal();
    bytes[kNtOffset] = std::byte{'X'};
    bytes[kNtOffset + 1] = std::byte{'Y'};
    bytes[kNtOffset + 2] = std::byte{'Z'};
    bytes[kNtOffset + 3] = std::byte{'W'};

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, RejectsNonAmd64Machine) {
    std::vector<std::byte> bytes = make_minimal();
    write_u16(bytes, kMachineOffset, 0x14C);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::UnsupportedArchitecture);
}

TEST(PeReaderTest, RejectsPe32OptionalHeader) {
    std::vector<std::byte> bytes = make_minimal();
    write_u16(bytes, kOptionalMagicOffset, 0x10B);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::UnsupportedFormat);
}

TEST(PeReaderTest, RejectsUnknownOptionalMagic) {
    std::vector<std::byte> bytes = make_minimal();
    write_u16(bytes, kOptionalMagicOffset, 0x1234);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, RejectsTinyOptionalHeader) {
    std::vector<std::byte> bytes = make_minimal();
    write_u16(bytes, kOptionalSizeOffset, 64);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, RejectsSectionTableBeyondFile) {
    std::vector<std::byte> bytes = make_minimal();
    write_u16(bytes, kSectionCountOffset, 0xFFFF);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Truncated);
}

TEST(PeReaderTest, RejectsSectionRawDataBeyondFile) {
    std::vector<std::byte> bytes = make_minimal();
    write_u32(bytes, kSection0RawSizeOffset, 0x2000);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, AcceptsImageWithoutDataDirectories) {
    BuildSpec spec;
    spec.number_of_rva_and_sizes = 0;
    spec.section_data = {std::vector<std::byte>(0x10), std::vector<std::byte>(0x10)};
    const std::vector<std::byte> bytes = build(spec);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Success);
}

TEST(PeReaderTest, RejectsTinyImportDirectory) {
    BuildSpec spec;
    spec.section_data = {std::vector<std::byte>(0x10), std::vector<std::byte>(0x10)};
    spec.import_rva = 0x2000;
    spec.import_size = 8;
    const std::vector<std::byte> bytes = build(spec);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, RejectsImportDirectoryOutsideImage) {
    BuildSpec spec;
    spec.section_data = {std::vector<std::byte>(0x10), std::vector<std::byte>(0x10)};
    spec.import_rva = 0x9000;
    spec.import_size = 20;
    const std::vector<std::byte> bytes = build(spec);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, AcceptsEmptyImportTable) {
    BuildSpec spec;
    spec.section_data = {std::vector<std::byte>(0x10), std::vector<std::byte>(0x20)};
    spec.import_rva = 0x2000;
    spec.import_size = 20;
    const std::vector<std::byte> bytes = build(spec);

    const ParseResult result = parse_pe(bytes);

    ASSERT_EQ(result.status, ParseStatus::Success) << result.error_message;
    EXPECT_TRUE(result.info.imports.empty());
}

TEST(PeReaderTest, RejectsImportNameOutsideImage) {
    std::vector<std::byte> bytes = make_import_pe();
    write_u32(bytes, 0x400 + 12, 0x9000);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, RejectsSymbolNameOutsideImage) {
    std::vector<std::byte> bytes = make_import_pe();
    write_u32(bytes, 0x400 + 40, 0x9999);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, RejectsSymbolHintBeyondSection) {
    std::vector<std::byte> bytes = make_import_pe();
    write_u32(bytes, 0x400 + 40, 0x2000 + 0x1FF);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, RejectsTinyRelocationDirectory) {
    BuildSpec spec;
    spec.section_data = {std::vector<std::byte>(0x10), std::vector<std::byte>(0x20)};
    spec.reloc_rva = 0x2000;
    spec.reloc_size = 4;
    const std::vector<std::byte> bytes = build(spec);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, RejectsRelocationBlockTooSmall) {
    std::vector<std::byte> bytes = make_reloc_pe();
    write_u32(bytes, 0x400 + 4, 4);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, RejectsRelocationBlockExceedingDirectory) {
    std::vector<std::byte> bytes = make_reloc_pe();
    write_u32(bytes, 0x400 + 4, 100);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, RejectsRelocationDirectoryWithResidualBytes) {
    std::vector<std::byte> bytes = make_reloc_pe();
    write_u32(bytes, kRelocDirSizeOffset, 13);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, RejectsRelocationEntriesNotWordAligned) {
    constexpr std::uint32_t kDataRva = 0x2000;
    std::vector<std::byte> data(8, std::byte{0});
    write_u32(data, 0, 0x1000);  // page RVA
    write_u32(data, 4, 9);       // block size leaving one residual byte

    BuildSpec spec;
    spec.section_names = {".text", ".rdata"};
    spec.section_data = {std::vector<std::byte>(0x10), data};
    spec.reloc_rva = kDataRva;
    spec.reloc_size = 9;
    const std::vector<std::byte> bytes = build(spec);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

}  // namespace
}  // namespace tradutorlinux::pe
