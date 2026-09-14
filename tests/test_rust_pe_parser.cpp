#include "pe_builder.hpp"
#include "tradutorlinux/ffi/rust_pe_parser.h"
#include "tradutorlinux/pe/pe_reader.hpp"
#include "../src/pe/rust_pe_parser.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ByteVector = std::vector<std::byte>;
using tradutorlinux::pe::BaseRelocBlock;
using tradutorlinux::pe::ExportedSymbol;
using tradutorlinux::pe::ImportedDll;
using tradutorlinux::pe::ParseResult;
using tradutorlinux::pe::PeInfo;
using tradutorlinux::pe::RuntimeFunction;
using tradutorlinux::pe::UnwindCode;
using tradutorlinux::pe::UnwindEpilog;
using tradutorlinux::pe::UnwindInfo;
using namespace tradutorlinux::pe::testutil;

constexpr std::size_t kHeaderSize = TL_PE_WIRE_HEADER_SIZE;
constexpr std::size_t kDescriptorSize = TL_PE_WIRE_TABLE_DESCRIPTOR_SIZE;
constexpr std::size_t kDescriptorOffset = TL_PE_WIRE_TABLE_DESCRIPTOR_OFFSET;

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

std::uint64_t read_u64(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

struct Descriptor {
    std::uint64_t offset{};
    std::uint64_t count{};
    std::uint32_t stride{};
    std::uint32_t flags{};
};

Descriptor descriptor(const std::vector<std::uint8_t>& bytes, const std::uint32_t table) {
    const std::size_t offset = kDescriptorOffset + static_cast<std::size_t>(table) * kDescriptorSize;
    return {.offset = read_u64(bytes, offset),
            .count = read_u64(bytes, offset + 8U),
            .stride = read_u32(bytes, offset + 16U),
            .flags = read_u32(bytes, offset + 20U)};
}

bool decode_wire(const std::vector<std::uint8_t>& bytes, PeInfo& info) {
    tl_pe_error_v1 error{};
    std::string message;
    return tradutorlinux::pe::decode_tlpe_v1(std::span<const std::uint8_t>{bytes}, info,
                                             error, message);
}
struct RustCall {
    std::uint32_t status{};
    std::uint64_t required{};
    std::vector<std::uint8_t> output;
    tl_pe_error_v1 error{};
    std::string message;
};

RustCall parse_rust(const ByteVector& input) {
    RustCall call;
    std::array<char, 512> message{};
    std::uint64_t message_required = 0;
    const auto* data = reinterpret_cast<const std::uint8_t*>(input.data());
    call.status = tl_pe_parse_v1_size(data, input.size(), &call.required, &call.error,
                                      message.data(), message.size(), &message_required);
    call.message.assign(message.data(), strnlen(message.data(), message.size()));
    if (call.status != TL_PE_STATUS_SUCCESS) return call;
    call.output.resize(static_cast<std::size_t>(call.required));
    message.fill(0);
    message_required = 0;
    call.status = tl_pe_parse_v1_fill(data, input.size(), call.output.data(), call.output.size(),
                                      &call.required, &call.error, message.data(), message.size(),
                                      &message_required);
    call.message.assign(message.data(), strnlen(message.data(), message.size()));
    return call;
}

void expect_equal(const PeInfo& expected, const PeInfo& actual) {
    EXPECT_EQ(expected.is_pe32_plus, actual.is_pe32_plus);
    EXPECT_EQ(expected.is_dll, actual.is_dll);
    EXPECT_EQ(expected.machine, actual.machine);
    EXPECT_EQ(expected.number_of_sections, actual.number_of_sections);
    EXPECT_EQ(expected.address_of_entry_point, actual.address_of_entry_point);
    EXPECT_EQ(expected.image_base, actual.image_base);
    EXPECT_EQ(expected.section_alignment, actual.section_alignment);
    EXPECT_EQ(expected.size_of_image, actual.size_of_image);
    EXPECT_EQ(expected.size_of_headers, actual.size_of_headers);
    EXPECT_EQ(expected.subsystem, actual.subsystem);
    EXPECT_EQ(expected.import_directory_rva, actual.import_directory_rva);
    EXPECT_EQ(expected.import_directory_size, actual.import_directory_size);
    EXPECT_EQ(expected.export_directory_rva, actual.export_directory_rva);
    EXPECT_EQ(expected.export_directory_size, actual.export_directory_size);
    EXPECT_EQ(expected.export_ordinal_base, actual.export_ordinal_base);
    EXPECT_EQ(expected.resource_directory_rva, actual.resource_directory_rva);
    EXPECT_EQ(expected.resource_directory_size, actual.resource_directory_size);
    EXPECT_EQ(expected.exception_directory_rva, actual.exception_directory_rva);
    EXPECT_EQ(expected.exception_directory_size, actual.exception_directory_size);
    EXPECT_EQ(expected.relocation_directory_rva, actual.relocation_directory_rva);
    EXPECT_EQ(expected.relocation_directory_size, actual.relocation_directory_size);
    EXPECT_EQ(expected.delay_import_directory_rva, actual.delay_import_directory_rva);
    EXPECT_EQ(expected.delay_import_directory_size, actual.delay_import_directory_size);
    EXPECT_EQ(expected.tls_directory_rva, actual.tls_directory_rva);
    EXPECT_EQ(expected.tls_directory_size, actual.tls_directory_size);
    ASSERT_EQ(expected.sections.size(), actual.sections.size());
    for (std::size_t index = 0; index < expected.sections.size(); ++index) {
        EXPECT_EQ(expected.sections[index].name, actual.sections[index].name);
        EXPECT_EQ(expected.sections[index].virtual_address, actual.sections[index].virtual_address);
        EXPECT_EQ(expected.sections[index].virtual_size, actual.sections[index].virtual_size);
        EXPECT_EQ(expected.sections[index].raw_data_pointer, actual.sections[index].raw_data_pointer);
        EXPECT_EQ(expected.sections[index].raw_data_size, actual.sections[index].raw_data_size);
        EXPECT_EQ(expected.sections[index].characteristics, actual.sections[index].characteristics);
    }
    const auto compare_dlls = [](const std::vector<ImportedDll>& left,
                                 const std::vector<ImportedDll>& right) {
        ASSERT_EQ(left.size(), right.size());
        for (std::size_t index = 0; index < left.size(); ++index) {
            EXPECT_EQ(left[index].name, right[index].name);
            ASSERT_EQ(left[index].symbols.size(), right[index].symbols.size());
            for (std::size_t symbol = 0; symbol < left[index].symbols.size(); ++symbol) {
                EXPECT_EQ(left[index].symbols[symbol].by_ordinal, right[index].symbols[symbol].by_ordinal);
                EXPECT_EQ(left[index].symbols[symbol].ordinal, right[index].symbols[symbol].ordinal);
                EXPECT_EQ(left[index].symbols[symbol].name, right[index].symbols[symbol].name);
                EXPECT_EQ(left[index].symbols[symbol].iat_rva, right[index].symbols[symbol].iat_rva);
            }
        }
    };
    compare_dlls(expected.imports, actual.imports);
    compare_dlls(expected.delay_imports, actual.delay_imports);
    ASSERT_EQ(expected.exports.size(), actual.exports.size());
    for (std::size_t index = 0; index < expected.exports.size(); ++index) {
        EXPECT_EQ(expected.exports[index].by_name, actual.exports[index].by_name);
        EXPECT_EQ(expected.exports[index].name, actual.exports[index].name);
        EXPECT_EQ(expected.exports[index].ordinal, actual.exports[index].ordinal);
        EXPECT_EQ(expected.exports[index].rva, actual.exports[index].rva);
        EXPECT_EQ(expected.exports[index].forwarder, actual.exports[index].forwarder);
    }
    EXPECT_EQ(expected.tls_info.start_address_of_raw_data, actual.tls_info.start_address_of_raw_data);
    EXPECT_EQ(expected.tls_info.end_address_of_raw_data, actual.tls_info.end_address_of_raw_data);
    EXPECT_EQ(expected.tls_info.address_of_index, actual.tls_info.address_of_index);
    EXPECT_EQ(expected.tls_info.address_of_callbacks, actual.tls_info.address_of_callbacks);
    EXPECT_EQ(expected.tls_info.size_of_zero_fill, actual.tls_info.size_of_zero_fill);
    EXPECT_EQ(expected.tls_info.characteristics, actual.tls_info.characteristics);
    EXPECT_EQ(expected.tls_info.callback_vas, actual.tls_info.callback_vas);
    ASSERT_EQ(expected.runtime_functions.size(), actual.runtime_functions.size());
    for (std::size_t index = 0; index < expected.runtime_functions.size(); ++index) {
        const RuntimeFunction& left = expected.runtime_functions[index];
        const RuntimeFunction& right = actual.runtime_functions[index];
        EXPECT_EQ(left.begin_rva, right.begin_rva);
        EXPECT_EQ(left.end_rva, right.end_rva);
        EXPECT_EQ(left.unwind_info_rva, right.unwind_info_rva);
        const UnwindInfo& lu = left.unwind;
        const UnwindInfo& ru = right.unwind;
        EXPECT_EQ(lu.version, ru.version);
        EXPECT_EQ(lu.flags, ru.flags);
        EXPECT_EQ(lu.prolog_size, ru.prolog_size);
        EXPECT_EQ(lu.frame_register, ru.frame_register);
        EXPECT_EQ(lu.frame_offset, ru.frame_offset);
        EXPECT_EQ(lu.has_extended_set_fpreg, ru.has_extended_set_fpreg);
        EXPECT_EQ(lu.handler_rva, ru.handler_rva);
        EXPECT_EQ(lu.handler_data_rva, ru.handler_data_rva);
        EXPECT_EQ(lu.has_chained_function, ru.has_chained_function);
        EXPECT_EQ(lu.chained_begin_rva, ru.chained_begin_rva);
        EXPECT_EQ(lu.chained_end_rva, ru.chained_end_rva);
        EXPECT_EQ(lu.chained_unwind_info_rva, ru.chained_unwind_info_rva);
        ASSERT_EQ(lu.codes.size(), ru.codes.size());
        for (std::size_t item = 0; item < lu.codes.size(); ++item) {
            EXPECT_EQ(lu.codes[item].code_offset, ru.codes[item].code_offset);
            EXPECT_EQ(lu.codes[item].operation, ru.codes[item].operation);
            EXPECT_EQ(lu.codes[item].operation_info, ru.codes[item].operation_info);
            EXPECT_EQ(lu.codes[item].operand, ru.codes[item].operand);
        }
        ASSERT_EQ(lu.epilogs.size(), ru.epilogs.size());
        for (std::size_t item = 0; item < lu.epilogs.size(); ++item) {
            EXPECT_EQ(lu.epilogs[item].begin_rva, ru.epilogs[item].begin_rva);
            EXPECT_EQ(lu.epilogs[item].end_rva, ru.epilogs[item].end_rva);
        }
    }
    ASSERT_EQ(expected.relocations.size(), actual.relocations.size());
    for (std::size_t index = 0; index < expected.relocations.size(); ++index) {
        EXPECT_EQ(expected.relocations[index].page_rva, actual.relocations[index].page_rva);
        ASSERT_EQ(expected.relocations[index].entries.size(), actual.relocations[index].entries.size());
        for (std::size_t item = 0; item < expected.relocations[index].entries.size(); ++item) {
            EXPECT_EQ(expected.relocations[index].entries[item].type, actual.relocations[index].entries[item].type);
            EXPECT_EQ(expected.relocations[index].entries[item].offset, actual.relocations[index].entries[item].offset);
        }
    }
}

ByteVector make_export_fixture() {
    constexpr std::uint32_t export_rva = 0x2000;
    std::vector<std::byte> data(0xa0, std::byte{0});
    write_u32(data, 12, export_rva + 0x54);
    write_u32(data, 16, 1);
    write_u32(data, 20, 2);
    write_u32(data, 24, 1);
    write_u32(data, 28, export_rva + 0x40);
    write_u32(data, 32, export_rva + 0x48);
    write_u32(data, 36, export_rva + 0x4c);
    write_u32(data, 0x40, 0x1000);
    write_u32(data, 0x44, export_rva + 0x70);
    write_u32(data, 0x48, export_rva + 0x60);
    write_u16(data, 0x4c, 0);
    const auto put = [&data](std::size_t offset, const char* text) {
        const std::size_t length = std::strlen(text) + 1U;
        std::copy_n(reinterpret_cast<const std::byte*>(text), length,
                    data.begin() + static_cast<std::ptrdiff_t>(offset));
    };
    put(0x54, "custom.dll");
    put(0x60, "CustomEntry");
    put(0x70, "KERNEL32.ExitProcess");
    BuildSpec spec;
    spec.coff_characteristics = 0x2022;
    spec.section_names = {".text", ".edata"};
    spec.section_data = {std::vector<std::byte>(0x10), data};
    spec.export_rva = export_rva;
    spec.export_size = 0xa0;
    return build(spec);
}

ByteVector make_unwind_fixture(const bool version2) {
    constexpr std::uint32_t pdata_rva = 0x2000;
    std::vector<std::byte> data;
    push_u32(data, 0x1000);
    push_u32(data, version2 ? 0x1040 : 0x1010);
    push_u32(data, pdata_rva + 12);
    if (!version2) {
        data.insert(data.end(), {std::byte{0x01}, std::byte{4}, std::byte{2}, std::byte{0},
                                  std::byte{4}, std::byte{0x42}, std::byte{1}, std::byte{0x30}});
    } else {
        data.insert(data.end(), {std::byte{0x02}, std::byte{4}, std::byte{4}, std::byte{0},
                                  std::byte{2}, std::byte{0x06}, std::byte{4}, std::byte{0x06},
                                  std::byte{8}, std::byte{0x06}, std::byte{4}, std::byte{0x42}});
    }
    BuildSpec spec;
    spec.section_names = {".text", ".pdata"};
    spec.section_data = {std::vector<std::byte>(version2 ? 0x40 : 0x20), data};
    spec.exception_rva = pdata_rva;
    spec.exception_size = 12;
    return build(spec);
}

ByteVector make_virtual_only_exception_fixture() {
    BuildSpec spec;
    spec.section_names = {".text", ".pdata"};
    spec.section_data = {std::vector<std::byte>(0x10), std::vector<std::byte>(12)};
    spec.exception_rva = 0x2000;
    spec.exception_size = 12;
    std::vector<std::byte> result = build(spec);
    constexpr std::size_t kSecondSectionRawSizeOffset = 0x148 + 40 + 16;
    write_u32(result, kSecondSectionRawSizeOffset, 0);
    return result;
}

ByteVector make_all_unwind_fixture() {
    constexpr std::uint32_t pdata_rva = 0x2000;
    std::vector<std::byte> data;
    push_u32(data, 0x1000); push_u32(data, 0x1040); push_u32(data, pdata_rva + 12);
    data.insert(data.end(), {std::byte{1}, std::byte{24}, std::byte{16}, std::byte{5}});
    const auto code = [&data](std::uint8_t offset, std::uint8_t op, std::uint8_t info) {
        data.push_back(static_cast<std::byte>(offset));
        data.push_back(static_cast<std::byte>((info << 4U) | op));
    };
    code(24, 0, 3); code(23, 1, 0); push_u16(data, 16); code(22, 2, 4);
    code(21, 3, 0); code(20, 4, 12); push_u16(data, 3); code(19, 5, 13);
    push_u32(data, 40); code(18, 8, 6); push_u16(data, 2); code(17, 9, 7);
    push_u32(data, 64); code(16, 10, 1);
    BuildSpec spec;
    spec.section_names = {".text", ".pdata"};
    spec.section_data = {std::vector<std::byte>(0x40), data};
    spec.exception_rva = pdata_rva; spec.exception_size = 12;
    return build(spec);
}

ByteVector make_chained_unwind_fixture() {
    constexpr std::uint32_t pdata_rva = 0x2000;
    std::vector<std::byte> data;
    push_u32(data, 0x1000); push_u32(data, 0x1010); push_u32(data, pdata_rva + 24);
    push_u32(data, 0x1020); push_u32(data, 0x1030); push_u32(data, pdata_rva + 40);
    data.insert(data.end(), {std::byte{0x21}, std::byte{0}, std::byte{0}, std::byte{0}});
    push_u32(data, 0x1020); push_u32(data, 0x1030); push_u32(data, pdata_rva + 40);
    data.insert(data.end(), {std::byte{0x01}, std::byte{0}, std::byte{0}, std::byte{0}});
    BuildSpec spec;
    spec.section_names = {".text", ".pdata"};
    spec.section_data = {std::vector<std::byte>(0x40), data};
    spec.exception_rva = pdata_rva; spec.exception_size = 24;
    return build(spec);
}

ByteVector make_tls_fixture() {
    constexpr std::uint32_t tls_rva = 0x2000;
    std::vector<std::byte> data(56, std::byte{0});
    write_u64(data, 0, 0x140003000ULL);
    write_u64(data, 8, 0x140003008ULL);
    write_u64(data, 16, 0x140003010ULL);
    write_u64(data, 24, 0x140002028ULL);
    write_u32(data, 32, 16);
    write_u32(data, 36, 0x1234);
    write_u64(data, 40, 0x140001111ULL);
    write_u64(data, 48, 0x140001222ULL);
    BuildSpec spec;
    spec.section_names = {".text", ".tls"};
    spec.section_data = {std::vector<std::byte>(0x10), data};
    auto result = build(spec);
    constexpr std::size_t optional_start = kNtOffset + 4U + 20U;
    write_u32(result, optional_start + 112U + 9U * 8U, tls_rva);
    write_u32(result, optional_start + 112U + 9U * 8U + 4U, 40);
    return result;
}

ByteVector make_import_fixture() {
    BuildSpec spec;
    spec.section_names = {".text", ".rdata"};
    spec.section_data = {std::vector<std::byte>(0x10),
                         make_import_data({{"FAKE.dll", {"PrintA"}, {5}}})};
    spec.import_rva = kImportDataRva;
    spec.import_size = 40;
    return build(spec);
}

ByteVector make_delay_import_fixture() {
    BuildSpec spec;
    spec.section_names = {".text", ".didat"};
    spec.section_data = {std::vector<std::byte>(0x10),
                         make_delay_import_data({{"FAKE.dll", {"PrintA"}, {5}}})};
    spec.delay_import_rva = kImportDataRva;
    spec.delay_import_size = 64;
    return build(spec);
}

ByteVector make_reloc_fixture() {
    std::vector<std::byte> data;
    push_u32(data, 0x1000);
    push_u32(data, 12);
    push_u16(data, static_cast<std::uint16_t>((3U << 12U) | 0x10U));
    push_u16(data, static_cast<std::uint16_t>(0x20U));
    BuildSpec spec;
    spec.section_names = {".text", ".rdata"};
    spec.section_data = {std::vector<std::byte>(0x10), data};
    spec.reloc_rva = kImportDataRva;
    spec.reloc_size = 12;
    return build(spec);
}

TEST(RustPeParserTest, DifferentiallyMatchesCppForAllParserTables) {
    const std::array<ByteVector, 10> fixtures{
        make_minimal(),
        make_import_fixture(),
        make_export_fixture(),
        make_delay_import_fixture(),
        make_reloc_fixture(),
        make_tls_fixture(),
        make_unwind_fixture(false),
        make_unwind_fixture(true),
        make_all_unwind_fixture(),
        make_chained_unwind_fixture(),
    };
    for (const ByteVector& fixture : fixtures) {
        const ParseResult cpp = tradutorlinux::pe::parse_pe(fixture);
        ASSERT_EQ(cpp.status, tradutorlinux::pe::ParseStatus::Success);
        const RustCall rust = parse_rust(fixture);
        ASSERT_EQ(rust.status, TL_PE_STATUS_SUCCESS) << rust.message;
        ASSERT_EQ(rust.required, rust.output.size());
        PeInfo decoded;
        ASSERT_TRUE(decode_wire(rust.output, decoded));
        expect_equal(cpp.info, decoded);
    }
}

TEST(RustPeParserTest, ExtendedSetFpRegRepeatsFrameOffset) {
    constexpr std::size_t kUnwindFileOffset = 0x400U + 12U;

    ByteVector accepted = make_all_unwind_fixture();
    accepted[kUnwindFileOffset + 3U] = std::byte{0x45};
    accepted[kUnwindFileOffset + 13U] = std::byte{0x43};
    const ParseResult cpp = tradutorlinux::pe::parse_pe(accepted);
    ASSERT_EQ(cpp.status, tradutorlinux::pe::ParseStatus::Success) << cpp.error_message;
    const RustCall rust = parse_rust(accepted);
    ASSERT_EQ(rust.status, TL_PE_STATUS_SUCCESS) << rust.message;
    PeInfo decoded;
    ASSERT_TRUE(decode_wire(rust.output, decoded));
    expect_equal(cpp.info, decoded);
    ASSERT_FALSE(decoded.runtime_functions.empty());
    EXPECT_TRUE(decoded.runtime_functions[0].unwind.has_extended_set_fpreg);

    ByteVector mismatched = make_all_unwind_fixture();
    mismatched[kUnwindFileOffset + 3U] = std::byte{0x35};
    mismatched[kUnwindFileOffset + 13U] = std::byte{0x23};
    EXPECT_EQ(tradutorlinux::pe::parse_pe(mismatched).status,
              tradutorlinux::pe::ParseStatus::Success);
    const RustCall accepted_gpr = parse_rust(mismatched);
    EXPECT_EQ(accepted_gpr.status, TL_PE_STATUS_SUCCESS) << accepted_gpr.message;

    ByteVector rejected = make_all_unwind_fixture();
    rejected[kUnwindFileOffset + 3U] = std::byte{0x35};
    rejected[kUnwindFileOffset + 13U] = std::byte{0x43};
    EXPECT_EQ(tradutorlinux::pe::parse_pe(rejected).status,
              tradutorlinux::pe::ParseStatus::UnsupportedMechanism);
    const RustCall rejected_rust = parse_rust(rejected);
    EXPECT_EQ(rejected_rust.status, TL_PE_STATUS_UNSUPPORTED_MECHANISM);
    EXPECT_EQ(rejected_rust.error.code, TL_PE_ERROR_UNWIND_DIRECTORY);
}

TEST(RustPeParserTest, RejectsVirtualOnlyExceptionDirectoryLikeCpp) {
    const ByteVector input = make_virtual_only_exception_fixture();
    const ParseResult cpp = tradutorlinux::pe::parse_pe(input);
    ASSERT_EQ(cpp.status, tradutorlinux::pe::ParseStatus::Malformed);

    const RustCall rust = parse_rust(input);
    EXPECT_EQ(rust.status, TL_PE_STATUS_MALFORMED);
    EXPECT_EQ(rust.error.code, TL_PE_ERROR_UNWIND_DIRECTORY);
}

TEST(RustPeParserTest, DifferentiallyMatchesGeneratedPeCorpus) {
#if defined(TL_FIXTURE_OUTPUT_DIRECTORY)
    const std::filesystem::path fixture_directory{TL_FIXTURE_OUTPUT_DIRECTORY};
    std::vector<std::filesystem::path> fixtures;
    std::error_code error;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(fixture_directory, error)) {
        if (!error && entry.is_regular_file(error) &&
            (entry.path().extension() == ".exe" || entry.path().extension() == ".dll")) {
            fixtures.push_back(entry.path());
        }
        error.clear();
    }
    ASSERT_FALSE(error) << "não foi possível enumerar " << fixture_directory;
    ASSERT_FALSE(fixtures.empty()) << "corpus PE gerado vazio em " << fixture_directory;
    std::sort(fixtures.begin(), fixtures.end());

    for (const std::filesystem::path& fixture : fixtures) {
        std::ifstream stream{fixture, std::ios::binary};
        ASSERT_TRUE(stream) << fixture;
        stream.seekg(0, std::ios::end);
        const std::streamoff size = stream.tellg();
        ASSERT_GE(size, 0) << fixture;
        stream.seekg(0, std::ios::beg);
        ByteVector input(static_cast<std::size_t>(size));
        if (!input.empty()) {
            stream.read(reinterpret_cast<char*>(input.data()),
                        static_cast<std::streamsize>(input.size()));
        }
        ASSERT_TRUE(stream) << fixture;

        const ParseResult expected = tradutorlinux::pe::parse_pe(input);
        const tradutorlinux::pe::RustPeParseResult actual =
            tradutorlinux::pe::parse_pe_rust(input);
        ASSERT_EQ(actual.status, expected.status) << fixture << ": " << actual.error_message;
        ASSERT_FALSE(actual.internal_failure) << fixture << ": " << actual.error_message;
        if (expected.status == tradutorlinux::pe::ParseStatus::Success) {
            expect_equal(expected.info, actual.info);
        }
    }
#else
    GTEST_SKIP() << "corpus de fixtures não está disponível neste alvo";
#endif
}

TEST(RustPeParserTest, FfiReportsRequiredOutputAndDoesNotModifySmallBuffer) {
    const ByteVector input = make_minimal();
    const auto* data = reinterpret_cast<const std::uint8_t*>(input.data());
    std::array<char, 64> message{};
    tl_pe_error_v1 error{};
    std::uint64_t message_required = 0;
    std::uint64_t required = 0;
    ASSERT_EQ(tl_pe_parse_v1_size(data, input.size(), &required, &error, message.data(), message.size(),
                                  &message_required), TL_PE_STATUS_SUCCESS);
    ASSERT_GT(required, 1U);
    std::vector<std::uint8_t> output(required, 0xa5U);
    const std::vector<std::uint8_t> before = output;
    ASSERT_EQ(tl_pe_parse_v1_fill(data, input.size(), output.data(), required - 1U, &required, &error,
                                  message.data(), message.size(), &message_required),
              TL_PE_STATUS_BUFFER_TOO_SMALL);
    EXPECT_EQ(required, output.size());
    EXPECT_EQ(output, before);
}

TEST(RustPeParserTest, FfiMapsMalformedAndUnsupportedHeadersToStructuredErrors) {
    const auto check = [](ByteVector bytes, const std::uint32_t expected_status,
                          const std::uint32_t expected_code) {
        const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
        std::array<char, 128> message{};
        tl_pe_error_v1 error{};
        std::uint64_t required = 123;
        std::uint64_t message_required = 0;
        EXPECT_EQ(tl_pe_parse_v1_size(data, bytes.size(), &required, &error, message.data(),
                                      message.size(), &message_required), expected_status);
        EXPECT_EQ(required, 0U);
        EXPECT_EQ(error.code, expected_code);
        EXPECT_NE(message[0], '\0');
        EXPECT_EQ(message[message_required - 1U], '\0');
    };
    ByteVector malformed = make_minimal();
    malformed[0] = std::byte{'X'};
    check(malformed, TL_PE_STATUS_MALFORMED, TL_PE_ERROR_DOS_HEADER);
    ByteVector unsupported = make_minimal();
    write_u16(unsupported, kMachineOffset, 0x14c);
    check(unsupported, TL_PE_STATUS_UNSUPPORTED_ARCHITECTURE, TL_PE_ERROR_COFF_HEADER);
    unsupported = make_minimal();
    write_u16(unsupported, kOptionalMagicOffset, 0x10b);
    check(unsupported, TL_PE_STATUS_UNSUPPORTED_FORMAT, TL_PE_ERROR_OPTIONAL_HEADER);
}

TEST(RustPeParserTest, FfiMapsDirectoryFailuresAndUnsupportedMechanisms) {
    const auto check = [](ByteVector bytes, const std::uint32_t expected_status,
                          const std::uint32_t expected_code) {
        const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
        std::array<char, 256> message{};
        tl_pe_error_v1 error{};
        std::uint64_t required = 0;
        std::uint64_t message_required = 0;
        EXPECT_EQ(tl_pe_parse_v1_size(data, bytes.size(), &required, &error, message.data(),
                                      message.size(), &message_required), expected_status);
        EXPECT_EQ(error.code, expected_code);
        EXPECT_EQ(required, 0U);
    };
    ByteVector imports = make_import_fixture();
    write_u32(imports, 0x400U + 12U, 0);
    check(imports, TL_PE_STATUS_MALFORMED, TL_PE_ERROR_IMPORT_TABLE);

    ByteVector delay = make_delay_import_fixture();
    write_u32(delay, 0x400U, 2);
    check(delay, TL_PE_STATUS_UNSUPPORTED_MECHANISM, TL_PE_ERROR_DELAY_IMPORT_TABLE);

    ByteVector unwind = make_unwind_fixture(false);
    unwind[0x400U + 12U] = std::byte{3};
    check(unwind, TL_PE_STATUS_UNSUPPORTED_MECHANISM, TL_PE_ERROR_UNWIND_DIRECTORY);

    ByteVector reloc = make_reloc_fixture();
    write_u32(reloc, 0x400U + 4U, 7);
    check(reloc, TL_PE_STATUS_MALFORMED, TL_PE_ERROR_RELOCATION_DIRECTORY);

    check({}, TL_PE_STATUS_TRUNCATED, TL_PE_ERROR_DOS_HEADER);
}

TEST(RustPeParserTest, StringTableIsStableAndDeduplicated) {
    BuildSpec spec;
    spec.section_names = {".same", ".same"};
    spec.section_data = {std::vector<std::byte>(0x10),
                         make_import_data({{"FAKE.dll", {"PrintA"}, {}},
                                           {"FAKE.dll", {"PrintA"}, {}}})};
    spec.import_rva = kImportDataRva;
    spec.import_size = 60;
    const RustCall rust = parse_rust(build(spec));
    ASSERT_EQ(rust.status, TL_PE_STATUS_SUCCESS) << rust.message;
    const Descriptor strings = descriptor(rust.output, TL_PE_WIRE_TABLE_STRINGS);
    ASSERT_EQ(strings.count, 3U);
    std::vector<std::string> values;
    std::uint64_t cursor = strings.offset;
    for (std::uint64_t index = 0; index < strings.count; ++index) {
        const std::uint32_t length = read_u32(rust.output, static_cast<std::size_t>(cursor));
        values.emplace_back(reinterpret_cast<const char*>(rust.output.data() + cursor + 8U), length);
        cursor += (8U + length + 7U) & ~UINT64_C(7);
    }
    EXPECT_EQ(values, std::vector<std::string>({".same", "FAKE.dll", "PrintA"}));
}

TEST(RustPeParserTest, FfiRejectsInvalidArgumentsAndPreservesStatelessResults) {
    std::array<char, 128> message{};
    tl_pe_error_v1 error{};
    std::uint64_t required = 0;
    std::uint64_t message_required = 0;
    EXPECT_EQ(tl_pe_parse_v1_size(nullptr, 0, &required, &error, message.data(), message.size(),
                                  &message_required), TL_PE_STATUS_TRUNCATED);
    EXPECT_EQ(tl_pe_parse_v1_size(nullptr, 1, &required, &error, message.data(), message.size(),
                                  &message_required), TL_PE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(tl_pe_parse_v1_size(nullptr, 0, &required, &error, message.data(), message.size(),
                                  nullptr), TL_PE_STATUS_INVALID_ARGUMENT);

    const ByteVector input = make_minimal();
    const auto* data = reinterpret_cast<const std::uint8_t*>(input.data());
    std::uint64_t first_required = 0;
    ASSERT_EQ(tl_pe_parse_v1_size(data, input.size(), &first_required, &error, message.data(),
                                  message.size(), &message_required), TL_PE_STATUS_SUCCESS);
    std::uint64_t second_required = 0;
    ASSERT_EQ(tl_pe_parse_v1_size(data, input.size(), &second_required, &error, message.data(),
                                  message.size(), &message_required), TL_PE_STATUS_SUCCESS);
    EXPECT_EQ(first_required, second_required);
}

TEST(RustPeParserTest, FfiHonorsCallerOwnedErrorAndNullOutputRules) {
    const ByteVector malformed = [] {
        ByteVector bytes = make_minimal();
        bytes[0] = std::byte{'X'};
        return bytes;
    }();
    const auto* data = reinterpret_cast<const std::uint8_t*>(malformed.data());
    std::array<char, 4> short_message{0x7f, 0x7f, 0x7f, 0x7f};
    tl_pe_error_v1 error{};
    std::uint64_t required = 0;
    std::uint64_t message_required = 0;
    EXPECT_EQ(tl_pe_parse_v1_size(data, malformed.size(), &required, &error, short_message.data(),
                                  short_message.size(), &message_required),
              TL_PE_STATUS_BUFFER_TOO_SMALL);
    EXPECT_EQ(error.code, TL_PE_ERROR_DOS_HEADER);
    EXPECT_EQ(short_message.back(), '\0');
    EXPECT_GT(message_required, short_message.size());

    const ByteVector valid = make_minimal();
    const auto* valid_data = reinterpret_cast<const std::uint8_t*>(valid.data());
    ASSERT_EQ(tl_pe_parse_v1_size(valid_data, valid.size(), &required, &error, short_message.data(),
                                  short_message.size(), &message_required), TL_PE_STATUS_SUCCESS);
    EXPECT_EQ(tl_pe_parse_v1_fill(valid_data, valid.size(), nullptr, 0, &required, &error,
                                  short_message.data(), short_message.size(), &message_required),
              TL_PE_STATUS_BUFFER_TOO_SMALL);
    EXPECT_GT(required, 0U);
}

TEST(RustPeParserTest, FfiIsSafeForConcurrentIndependentCalls) {
    const ByteVector input = make_minimal();
    std::array<std::thread, 4> workers;
    std::array<bool, 4> passed{};
    for (std::size_t index = 0; index < workers.size(); ++index) {
        workers[index] = std::thread([&input, &passed, index] {
            const RustCall call = parse_rust(input);
            passed[index] = call.status == TL_PE_STATUS_SUCCESS && call.required == call.output.size();
        });
    }
    for (std::thread& worker : workers) worker.join();
    EXPECT_TRUE(std::all_of(passed.begin(), passed.end(), [](bool value) { return value; }));
}

TEST(RustPeParserTest, ProductionAdapterMatchesCppForEveryWireFixture) {
    const std::array<ByteVector, 9> fixtures{
        make_minimal(), make_import_fixture(), make_delay_import_fixture(),
        make_export_fixture(), make_reloc_fixture(), make_tls_fixture(),
        make_unwind_fixture(false), make_unwind_fixture(true), make_chained_unwind_fixture(),
    };
    for (const ByteVector& input : fixtures) {
        const ParseResult expected = tradutorlinux::pe::parse_pe(input);
        const tradutorlinux::pe::RustPeParseResult actual =
            tradutorlinux::pe::parse_pe_rust(input);
        ASSERT_EQ(actual.status, tradutorlinux::pe::ParseStatus::Success);
        ASSERT_FALSE(actual.internal_failure) << actual.error_message;
        expect_equal(expected.info, actual.info);
    }
}

TEST(RustPeParserTest, ProductionDecoderRejectsWireHeaderAndRecordMutations) {
    const RustCall call = parse_rust(make_minimal());
    ASSERT_EQ(call.status, TL_PE_STATUS_SUCCESS) << call.message;
    ASSERT_FALSE(call.output.empty());
    const auto rejects = [](const std::vector<std::uint8_t>& wire) {
        PeInfo info;
        tl_pe_error_v1 error{};
        std::string message;
        return !tradutorlinux::pe::decode_tlpe_v1(std::span<const std::uint8_t>{wire}, info,
                                                   error, message) &&
               error.code == TL_PE_ERROR_WIRE_FORMAT &&
               error.phase == TL_PE_ERROR_PHASE_WIRE;
    };
    const auto mutate = [&call](const std::size_t offset, const std::uint8_t value) {
        std::vector<std::uint8_t> wire = call.output;
        wire[offset] = value;
        return wire;
    };
    EXPECT_TRUE(rejects(mutate(TL_PE_WIRE_HEADER_MAGIC_OFFSET, 0)));
    EXPECT_TRUE(rejects(mutate(TL_PE_WIRE_HEADER_MAJOR_OFFSET, 2)));
    EXPECT_TRUE(rejects(mutate(TL_PE_WIRE_TABLE_DESCRIPTOR_OFFSET + 16U, 1)));
    EXPECT_TRUE(rejects(mutate(TL_PE_WIRE_HEADER_TOTAL_SIZE_OFFSET, 0)));
    const Descriptor info_table = descriptor(call.output, TL_PE_WIRE_TABLE_INFO);
    EXPECT_TRUE(rejects(mutate(static_cast<std::size_t>(info_table.offset) + 34U, 1)));
}

}  // namespace
