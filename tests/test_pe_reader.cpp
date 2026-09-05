#include "pe_builder.hpp"
#include "tradutorlinux/pe/pe_reader.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

namespace tradutorlinux::pe {
namespace {
using namespace tradutorlinux::pe::testutil;

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

std::vector<std::byte> make_delay_import_pe() {
    const std::vector<std::byte> data =
        make_delay_import_data({{"FAKE.dll", {"PrintA"}, {5}}});
    BuildSpec spec;
    spec.section_names = {".text", ".didat"};
    spec.section_data = {std::vector<std::byte>(0x10), data};
    spec.delay_import_rva = kImportDataRva;
    spec.delay_import_size = 64;
    return build(spec);
}

std::vector<std::byte> make_export_pe() {
    constexpr std::uint32_t kExportRva = 0x2000;
    std::vector<std::byte> data(0xA0, std::byte{0});
    // IMAGE_EXPORT_DIRECTORY: dois exports, um nomeado e um ordinal-only
    // forwarder para KERNEL32.ExitProcess.
    write_u32(data, 12, kExportRva + 0x54);  // DLL name
    write_u32(data, 16, 1);                  // ordinal base
    write_u32(data, 20, 2);                  // number of functions
    write_u32(data, 24, 1);                  // number of names
    write_u32(data, 28, kExportRva + 0x40);  // functions
    write_u32(data, 32, kExportRva + 0x48);  // names
    write_u32(data, 36, kExportRva + 0x4C);  // name ordinals
    write_u32(data, 0x40, 0x1000);           // ordinal 1 -> .text
    write_u32(data, 0x44, kExportRva + 0x70);  // ordinal 2 -> forwarder
    write_u32(data, 0x48, kExportRva + 0x60); // name pointer
    write_u16(data, 0x4C, 0);                 // name maps to ordinal index 0
    const auto put_string = [&data](const std::size_t offset, const char* value) {
        const std::size_t length = std::strlen(value) + 1U;
        ASSERT_LE(offset + length, data.size());
        std::copy_n(reinterpret_cast<const std::byte*>(value), length,
                    data.begin() + static_cast<std::ptrdiff_t>(offset));
    };
    put_string(0x54, "custom.dll");
    put_string(0x60, "CustomEntry");
    put_string(0x70, "KERNEL32.ExitProcess");

    BuildSpec spec;
    spec.coff_characteristics = 0x2022;  // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE | DLL
    spec.section_names = {".text", ".edata"};
    spec.section_data = {std::vector<std::byte>(0x10), data};
    spec.export_rva = kExportRva;
    spec.export_size = 0xA0;
    return build(spec);
}

std::vector<std::byte> make_unwind_pe() {
    constexpr std::uint32_t kPdataRva = 0x2000;
    std::vector<std::byte> data;
    // RUNTIME_FUNCTION: [0x1000, 0x1010), UNWIND_INFO em 0x200c.
    push_u32(data, 0x1000);
    push_u32(data, 0x1010);
    push_u32(data, kPdataRva + 12);
    // Version=1, Prolog=4, duas operações: alloc 40 + push RBX.
    data.push_back(std::byte{0x01});
    data.push_back(std::byte{4});
    data.push_back(std::byte{2});
    data.push_back(std::byte{0});
    data.push_back(std::byte{4});
    data.push_back(std::byte{0x42});  // UWOP_ALLOC_SMALL, OpInfo=4 => 40.
    data.push_back(std::byte{1});
    data.push_back(std::byte{0x30});  // UWOP_PUSH_NONVOL RBX.

    BuildSpec spec;
    spec.section_names = {".text", ".pdata"};
    spec.section_data = {std::vector<std::byte>(0x20), data};
    spec.exception_rva = kPdataRva;
    spec.exception_size = 12;
    return build(spec);
}

std::vector<std::byte> make_unwind_v2_pe() {
    constexpr std::uint32_t kPdataRva = 0x2000;
    std::vector<std::byte> data;
    // RUNTIME_FUNCTION: [0x1000, 0x1040), UNWIND_INFO V2 em 0x200c.
    push_u32(data, 0x1000);
    push_u32(data, 0x1040);
    push_u32(data, kPdataRva + 12);
    // V2, prólogo de 4 bytes, 4 slots. O primeiro descritor informa que
    // todos os epílogos têm dois bytes; os offsets 4 e 8 são relativos ao
    // fim da função. O último slot é UWOP_ALLOC_SMALL(40).
    data.push_back(std::byte{0x02});
    data.push_back(std::byte{4});
    data.push_back(std::byte{4});
    data.push_back(std::byte{0});
    data.push_back(std::byte{2});
    data.push_back(std::byte{0x06});
    data.push_back(std::byte{4});
    data.push_back(std::byte{0x06});
    data.push_back(std::byte{8});
    data.push_back(std::byte{0x06});
    data.push_back(std::byte{4});
    data.push_back(std::byte{0x42});

    BuildSpec spec;
    spec.section_names = {".text", ".pdata"};
    spec.section_data = {std::vector<std::byte>(0x40), data};
    spec.exception_rva = kPdataRva;
    spec.exception_size = 12;
    return build(spec);
}

std::vector<std::byte> make_all_unwind_opcodes_pe() {
    constexpr std::uint32_t kPdataRva = 0x2000;
    std::vector<std::byte> data;
    push_u32(data, 0x1000);
    push_u32(data, 0x1040);
    push_u32(data, kPdataRva + 12);
    // Version=1, prólogo de 24 bytes, 16 slots e RBP como frame register.
    data.push_back(std::byte{0x01});
    data.push_back(std::byte{24});
    data.push_back(std::byte{16});
    data.push_back(std::byte{0x05});
    const auto push_code = [&data](const std::uint8_t offset, const std::uint8_t operation,
                                   const std::uint8_t info) {
        data.push_back(static_cast<std::byte>(offset));
        data.push_back(static_cast<std::byte>((info << 4U) | operation));
    };
    push_code(24, 0, 3);   // UWOP_PUSH_NONVOL RBX
    push_code(23, 1, 0);   // UWOP_ALLOC_LARGE, u16 * 8
    push_u16(data, 16);
    push_code(22, 2, 4);   // UWOP_ALLOC_SMALL
    push_code(21, 3, 0);   // UWOP_SET_FPREG
    push_code(20, 4, 12);  // UWOP_SAVE_NONVOL R12, u16 * 8
    push_u16(data, 3);
    push_code(19, 5, 13);  // UWOP_SAVE_NONVOL_FAR R13, u32
    push_u32(data, 40);
    push_code(18, 8, 6);   // UWOP_SAVE_XMM128 XMM6, u16 * 16
    push_u16(data, 2);
    push_code(17, 9, 7);   // UWOP_SAVE_XMM128_FAR XMM7, u32
    push_u32(data, 64);
    push_code(16, 10, 1);  // UWOP_PUSH_MACHFRAME com código de erro

    BuildSpec spec;
    spec.section_names = {".text", ".pdata"};
    spec.section_data = {std::vector<std::byte>(0x40), data};
    spec.exception_rva = kPdataRva;
    spec.exception_size = 12;
    return build(spec);
}

std::vector<std::byte> make_chained_unwind_pe(const bool cycle = false) {
    constexpr std::uint32_t kPdataRva = 0x2000;
    std::vector<std::byte> data;
    // Duas entradas ordenadas. A primeira encadeia na segunda.
    push_u32(data, 0x1000);
    push_u32(data, 0x1010);
    push_u32(data, kPdataRva + 24);
    push_u32(data, 0x1020);
    push_u32(data, 0x1030);
    push_u32(data, kPdataRva + 40);
    // UNWIND_INFO[0]: Version=1 + CHAININFO, sem códigos.
    data.push_back(std::byte{0x21});
    data.push_back(std::byte{0});
    data.push_back(std::byte{0});
    data.push_back(std::byte{0});
    push_u32(data, 0x1020);
    push_u32(data, 0x1030);
    push_u32(data, kPdataRva + 40);
    // UNWIND_INFO[1]. No caso cíclico ele volta para a primeira função.
    data.push_back(cycle ? std::byte{0x21} : std::byte{0x01});
    data.push_back(std::byte{0});
    data.push_back(std::byte{0});
    data.push_back(std::byte{0});
    if (cycle) {
        push_u32(data, 0x1000);
        push_u32(data, 0x1010);
        push_u32(data, kPdataRva + 24);
    }

    BuildSpec spec;
    spec.section_names = {".text", ".pdata"};
    spec.section_data = {std::vector<std::byte>(0x40), data};
    spec.exception_rva = kPdataRva;
    spec.exception_size = 24;
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

TEST(PeReaderTest, ParsesDllExportsByNameOrdinalAndForwarder) {
    const ParseResult result = parse_pe(make_export_pe());

    ASSERT_EQ(result.status, ParseStatus::Success) << result.error_message;
    EXPECT_TRUE(result.info.is_dll);
    ASSERT_EQ(result.info.exports.size(), 2U);
    EXPECT_TRUE(result.info.exports[0].by_name);
    EXPECT_EQ(result.info.exports[0].name, "CustomEntry");
    EXPECT_EQ(result.info.exports[0].ordinal, 1U);
    EXPECT_EQ(result.info.exports[0].rva, 0x1000U);
    EXPECT_FALSE(result.info.exports[1].by_name);
    EXPECT_EQ(result.info.exports[1].ordinal, 2U);
    EXPECT_EQ(result.info.exports[1].forwarder, "KERNEL32.ExitProcess");
}

TEST(PeReaderTest, ParsesDelayImportsByNameAndOrdinal) {
    const std::vector<std::byte> bytes = make_delay_import_pe();

    const ParseResult result = parse_pe(bytes);

    ASSERT_EQ(result.status, ParseStatus::Success) << result.error_message;
    EXPECT_TRUE(result.info.imports.empty());
    ASSERT_EQ(result.info.delay_imports.size(), 1U);
    const ImportedDll& dll = result.info.delay_imports[0];
    EXPECT_EQ(dll.name, "FAKE.dll");
    ASSERT_EQ(dll.symbols.size(), 2U);
    EXPECT_FALSE(dll.symbols[0].by_ordinal);
    EXPECT_EQ(dll.symbols[0].name, "PrintA");
    EXPECT_TRUE(dll.symbols[1].by_ordinal);
    EXPECT_EQ(dll.symbols[1].ordinal, 5);
    EXPECT_NE(dll.symbols[0].iat_rva, 0U);
}

TEST(PeReaderTest, ParsesRuntimeFunctionAndUnwindCodes) {
    const ParseResult result = parse_pe(make_unwind_pe());

    ASSERT_EQ(result.status, ParseStatus::Success) << result.error_message;
    EXPECT_EQ(result.info.exception_directory_rva, 0x2000U);
    ASSERT_EQ(result.info.runtime_functions.size(), 1U);
    const RuntimeFunction& function = result.info.runtime_functions[0];
    EXPECT_EQ(function.begin_rva, 0x1000U);
    EXPECT_EQ(function.end_rva, 0x1010U);
    EXPECT_EQ(function.unwind.version, 1U);
    ASSERT_EQ(function.unwind.codes.size(), 2U);
    EXPECT_EQ(function.unwind.codes[0].operation, UnwindOperation::AllocSmall);
    EXPECT_EQ(function.unwind.codes[0].operand, 40U);
    EXPECT_EQ(function.unwind.codes[1].operation, UnwindOperation::PushNonVol);
    EXPECT_EQ(function.unwind.codes[1].operation_info, 3U);
}

TEST(PeReaderTest, ParsesV2EpilogsAndKeepsUnwindTailAligned) {
    const ParseResult result = parse_pe(make_unwind_v2_pe());

    ASSERT_EQ(result.status, ParseStatus::Success) << result.error_message;
    ASSERT_EQ(result.info.runtime_functions.size(), 1U);
    const RuntimeFunction& function = result.info.runtime_functions[0];
    EXPECT_EQ(function.unwind.version, 2U);
    ASSERT_EQ(function.unwind.epilogs.size(), 2U);
    EXPECT_EQ(function.unwind.epilogs[0].begin_rva, 0x1038U);
    EXPECT_EQ(function.unwind.epilogs[0].end_rva, 0x103AU);
    EXPECT_EQ(function.unwind.epilogs[1].begin_rva, 0x103CU);
    EXPECT_EQ(function.unwind.epilogs[1].end_rva, 0x103EU);
    ASSERT_EQ(function.unwind.codes.size(), 1U);
    EXPECT_EQ(function.unwind.codes[0].operation, UnwindOperation::AllocSmall);

    std::vector<std::byte> handler_bytes = make_unwind_v2_pe();
    // V2 + EHANDLER. A cauda começa após os quatro slots, em 0x2018.
    handler_bytes[0x400 + 12] = std::byte{0x0A};
    write_u32(handler_bytes, 0x400 + 24, 0x1000);
    const ParseResult handler = parse_pe(handler_bytes);
    ASSERT_EQ(handler.status, ParseStatus::Success) << handler.error_message;
    EXPECT_EQ(handler.info.runtime_functions[0].unwind.handler_rva, 0x1000U);
    EXPECT_EQ(handler.info.runtime_functions[0].unwind.handler_data_rva, 0x201CU);

    std::vector<std::byte> chained_bytes = make_chained_unwind_pe();
    // A cauda de V2 sem códigos continua no mesmo limite alinhado e contém a
    // RUNTIME_FUNCTION encadeada.
    chained_bytes[0x400 + 24] = std::byte{0x22};  // Version 2 + CHAININFO.
    const ParseResult chained = parse_pe(chained_bytes);
    ASSERT_EQ(chained.status, ParseStatus::Success) << chained.error_message;
    EXPECT_EQ(chained.info.runtime_functions[0].unwind.version, 2U);
    EXPECT_TRUE(chained.info.runtime_functions[0].unwind.has_chained_function);

    std::vector<std::byte> at_end_bytes = make_unwind_v2_pe();
    // Descritor de dois bytes no fim, padding UOP_Epilog e ALLOC_SMALL.
    at_end_bytes[0x400 + 12 + 2] = std::byte{3};
    at_end_bytes[0x400 + 12 + 4] = std::byte{2};
    at_end_bytes[0x400 + 12 + 5] = std::byte{0x16};
    at_end_bytes[0x400 + 12 + 6] = std::byte{0};
    at_end_bytes[0x400 + 12 + 7] = std::byte{0x06};
    at_end_bytes[0x400 + 12 + 8] = std::byte{4};
    at_end_bytes[0x400 + 12 + 9] = std::byte{0x42};
    const ParseResult at_end = parse_pe(at_end_bytes);
    ASSERT_EQ(at_end.status, ParseStatus::Success) << at_end.error_message;
    ASSERT_EQ(at_end.info.runtime_functions[0].unwind.epilogs.size(), 1U);
    EXPECT_EQ(at_end.info.runtime_functions[0].unwind.epilogs[0].begin_rva, 0x103EU);
    EXPECT_EQ(at_end.info.runtime_functions[0].unwind.epilogs[0].end_rva, 0x1040U);
}

TEST(PeReaderTest, ParsesEveryAmd64V1UnwindOpcode) {
    const ParseResult result = parse_pe(make_all_unwind_opcodes_pe());

    ASSERT_EQ(result.status, ParseStatus::Success) << result.error_message;
    const std::vector<UnwindCode>& codes = result.info.runtime_functions[0].unwind.codes;
    ASSERT_EQ(codes.size(), 9U);
    EXPECT_EQ(codes[0].operation, UnwindOperation::PushNonVol);
    EXPECT_EQ(codes[1].operation, UnwindOperation::AllocLarge);
    EXPECT_EQ(codes[1].operand, 128U);
    EXPECT_EQ(codes[2].operation, UnwindOperation::AllocSmall);
    EXPECT_EQ(codes[3].operation, UnwindOperation::SetFpReg);
    EXPECT_EQ(codes[4].operation, UnwindOperation::SaveNonVol);
    EXPECT_EQ(codes[4].operand, 24U);
    EXPECT_EQ(codes[5].operation, UnwindOperation::SaveNonVolFar);
    EXPECT_EQ(codes[5].operand, 40U);
    EXPECT_EQ(codes[6].operation, UnwindOperation::SaveXmm128);
    EXPECT_EQ(codes[6].operand, 32U);
    EXPECT_EQ(codes[7].operation, UnwindOperation::SaveXmm128Far);
    EXPECT_EQ(codes[7].operand, 64U);
    EXPECT_EQ(codes[8].operation, UnwindOperation::PushMachFrame);
    EXPECT_EQ(codes[8].operation_info, 1U);
}

TEST(PeReaderTest, ParsesUnwindHandlerAndRejectsBadChains) {
    std::vector<std::byte> handler_bytes = make_unwind_pe();
    // Flags EHANDLER, handler RVA (0x1000) depois dos dois UNWIND_CODEs.
    handler_bytes[0x400 + 12] = std::byte{0x09};
    write_u32(handler_bytes, 0x400 + 20, 0x1000);
    const ParseResult handler = parse_pe(handler_bytes);
    ASSERT_EQ(handler.status, ParseStatus::Success) << handler.error_message;
    EXPECT_EQ(handler.info.runtime_functions[0].unwind.handler_rva, 0x1000U);
    EXPECT_EQ(handler.info.runtime_functions[0].unwind.handler_data_rva, 0x2018U);

    handler_bytes = make_unwind_pe();
    handler_bytes[0x400 + 12] = std::byte{0x11};  // Version 1 + UHANDLER.
    write_u32(handler_bytes, 0x400 + 20, 0x1000);
    const ParseResult uhandler = parse_pe(handler_bytes);
    ASSERT_EQ(uhandler.status, ParseStatus::Success) << uhandler.error_message;
    EXPECT_EQ(uhandler.info.runtime_functions[0].unwind.flags, 2U);

    std::vector<std::byte> chained_bytes = make_unwind_pe();
    chained_bytes[0x400 + 12] = std::byte{0x21};  // Version 1 + CHAININFO.
    EXPECT_EQ(parse_pe(chained_bytes).status, ParseStatus::Malformed);

    const ParseResult chained = parse_pe(make_chained_unwind_pe());
    ASSERT_EQ(chained.status, ParseStatus::Success) << chained.error_message;
    ASSERT_EQ(chained.info.runtime_functions.size(), 2U);
    EXPECT_TRUE(chained.info.runtime_functions[0].unwind.has_chained_function);
    EXPECT_EQ(chained.info.runtime_functions[0].unwind.chained_begin_rva, 0x1020U);
    EXPECT_EQ(parse_pe(make_chained_unwind_pe(true)).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, RejectsMalformedAndUnsupportedUnwindMetadata) {
    std::vector<std::byte> bytes = make_unwind_pe();
    write_u32(bytes, 0x58 + 112 + 3 * 8 + 4, 11);
    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);

    bytes = make_unwind_pe();
    write_u32(bytes, 0x400 + 4, 0x1000);
    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);

    bytes = make_unwind_pe();
    bytes[0x400 + 12] = std::byte{0x03};
    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::UnsupportedMechanism);

    bytes = make_unwind_pe();
    bytes[0x400 + 14] = std::byte{0xFF};
    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);

    bytes = make_all_unwind_opcodes_pe();
    // UWOP_SET_FPREG fica no slot 4 (há um slot extra no ALLOC_LARGE). OpInfo 4 (RSP) é inválido.
    bytes[0x400 + 12 + 4 + 4 * 2 + 1] = std::byte{0x43};
    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::UnsupportedMechanism);
}

TEST(PeReaderTest, ValidatesV2EpilogDescriptorsAndExtendedSetFpReg) {
    constexpr std::size_t kUnwindFileOffset = 0x400 + 12;

    std::vector<std::byte> bytes = make_unwind_v2_pe();
    bytes[kUnwindFileOffset + 2] = std::byte{1};
    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);

    bytes = make_unwind_v2_pe();
    bytes[kUnwindFileOffset + 5] = std::byte{0x26};
    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::UnsupportedMechanism);

    bytes = make_unwind_v2_pe();
    bytes[kUnwindFileOffset + 4] = std::byte{0};
    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);

    bytes = make_unwind_v2_pe();
    bytes[kUnwindFileOffset + 6] = std::byte{0x50};
    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);

    bytes = make_unwind_v2_pe();
    bytes[kUnwindFileOffset + 8] = std::byte{5};
    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);

    bytes = make_unwind_v2_pe();
    // No formato com epílogo ao fim, um UOP_Epilog nulo serve de padding.
    // Qualquer distância não nula nesse slot passa a ser outro epílogo e
    // precisa obedecer os limites da função.
    bytes[kUnwindFileOffset + 2] = std::byte{3};
    bytes[kUnwindFileOffset + 4] = std::byte{2};
    bytes[kUnwindFileOffset + 5] = std::byte{0x16};
    bytes[kUnwindFileOffset + 6] = std::byte{1};
    bytes[kUnwindFileOffset + 7] = std::byte{0x06};
    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);

    bytes = make_unwind_v2_pe();
    bytes[kUnwindFileOffset] = std::byte{0x03};
    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::UnsupportedMechanism);

    bytes = make_all_unwind_opcodes_pe();
    bytes[kUnwindFileOffset + 3] = std::byte{0x35};
    bytes[kUnwindFileOffset + 13] = std::byte{0x33};
    const ParseResult extended = parse_pe(bytes);
    ASSERT_EQ(extended.status, ParseStatus::Success) << extended.error_message;
    EXPECT_TRUE(extended.info.runtime_functions[0].unwind.has_extended_set_fpreg);

    bytes[kUnwindFileOffset + 13] = std::byte{0x23};
    const ParseResult extended2 = parse_pe(bytes);
    EXPECT_EQ(extended2.status, ParseStatus::Success);
    EXPECT_TRUE(extended2.info.runtime_functions[0].unwind.has_extended_set_fpreg);
}

TEST(PeReaderTest, RejectsDelayImportDirectoryWithoutTerminator) {
    std::vector<std::byte> bytes = make_delay_import_pe();
    constexpr std::size_t kDelayDirectorySizeOffset = 0x58 + 112 + 13 * 8 + 4;
    write_u32(bytes, kDelayDirectorySizeOffset, 32);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, RejectsDelayImportAttributesOutsideRvaForm) {
    std::vector<std::byte> bytes = make_delay_import_pe();
    write_u32(bytes, 0x400, 0);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::UnsupportedMechanism);
}

TEST(PeReaderTest, RejectsDelayImportNameIntAndIatOutsideImage) {
    for (const std::size_t field_offset : {4U, 12U, 16U}) {
        std::vector<std::byte> bytes = make_delay_import_pe();
        write_u32(bytes, 0x400 + field_offset, 0x9000);
        EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
    }
}

TEST(PeReaderTest, RejectsDelayImportThunkTableWithoutTerminator) {
    std::vector<std::byte> bytes = make_delay_import_pe();
    write_u32(bytes, 0x400 + 16, 0x21F8);
    write_u64(bytes, 0x400 + 0x1F8, 0x2000);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, RejectsImportDirectoryWithoutDescriptorTerminator) {
    std::vector<std::byte> bytes = make_import_pe();
    // Optional header starts at 0x58; import directory size is directory[1].Size.
    write_u32(bytes, 0x58 + 112 + 8 + 4, 20);

    EXPECT_EQ(parse_pe(bytes).status, ParseStatus::Malformed);
}

TEST(PeReaderTest, RejectsFirstThunkOutsideImage) {
    std::vector<std::byte> bytes = make_import_pe();
    // Corrompe o FirstThunk do descritor (campo +16, dentro do .rdata com raw
    // pointer 0x400) para um valor próximo de UINT32_MAX. Sem validação, o
    // cálculo da IAT em uint32 estoura e aceita um descritor hostil.
    write_u32(bytes, 0x400 + 16, 0xFFFFFFF8);

    const ParseResult result = parse_pe(bytes);

    EXPECT_EQ(result.status, ParseStatus::Malformed);
    EXPECT_NE(result.error_message.find("IAT em RVA"), std::string::npos);
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
    EXPECT_EQ(result.info.number_of_sections, 4);
    ASSERT_EQ(result.info.sections.size(), 4);
    EXPECT_EQ(result.info.sections[0].name, ".text");
    EXPECT_EQ(result.info.sections[0].virtual_address, 0x1000);
    ASSERT_EQ(result.info.imports.size(), 1);
    EXPECT_EQ(result.info.imports[0].name, "KERNEL32.dll");
    ASSERT_EQ(result.info.imports[0].symbols.size(), 3);
    EXPECT_EQ(result.info.imports[0].symbols[0].name, "ExitProcess");
    EXPECT_EQ(result.info.imports[0].symbols[1].name, "GetStdHandle");
    EXPECT_EQ(result.info.imports[0].symbols[2].name, "WriteFile");
    EXPECT_FALSE(result.info.relocations.empty());
}

TEST(PeReaderTest, ParsesNopFixtureWithoutImports) {
    const std::filesystem::path path =
        std::filesystem::path{TL_FIXTURE_OUTPUT_DIRECTORY} / "tl_nop.exe";
    const std::vector<std::byte> bytes = read_file(path);

    ASSERT_FALSE(bytes.empty());
    const ParseResult result = parse_pe(bytes);

    ASSERT_EQ(result.status, ParseStatus::Success) << result.error_message;
    EXPECT_EQ(result.info.number_of_sections, 4);
    EXPECT_TRUE(result.info.imports.empty());
    EXPECT_FALSE(result.info.relocations.empty());
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
