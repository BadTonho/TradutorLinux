#include "pe_builder.hpp"
#include "tradutorlinux/loader/import_resolver.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/pe/pe_reader.hpp"
#include "tradutorlinux/runtime/winapi.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>
#include <gtest/gtest.h>

namespace tradutorlinux::loader {
namespace {

using namespace tradutorlinux::pe::testutil;

constexpr std::uint64_t kTestPreferredBase = 0x10000000ULL;

std::uint64_t read_le_u64(const MappedImage& image, const std::uint32_t rva) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(
                     std::to_integer<unsigned char>(image.memory[rva + index]))
                 << (8 * index);
    }
    return value;
}

std::uint32_t align_page_down(const std::uint32_t value, const std::uint32_t page) {
    return value - value % page;
}

// Procura em /proc/self/maps a linha que cobre `address` e retorna as
// permissões ("r--p"); std::nullopt se não houver.
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

// Anexa um bloco de relocations vazio (válido) à última seção: permite o
// mapeamento em base alternativa quando a base preferencial estiver ocupada.
void add_empty_reloc(BuildSpec& spec) {
    std::vector<std::byte>& reloc_blob = spec.section_data.back();
    const auto reloc_rva =
        kImportDataRva + static_cast<std::uint32_t>(reloc_blob.size());
    push_u32(reloc_blob, 0);  // PageRVA
    push_u32(reloc_blob, 8);  // SizeOfBlock sem entradas
    spec.reloc_rva = reloc_rva;
    spec.reloc_size = 8;
}

class ImportResolverTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_modules();
        const ExportedFunction exports[] = {
            {"DoWork", 1, reinterpret_cast<std::uintptr_t>(&tl_GetStdHandle)},
            {"WriteFile", 2, reinterpret_cast<std::uintptr_t>(&tl_WriteFile)},
        };
        const InternalModule module{"FAKE.dll", exports};
        ASSERT_TRUE(register_module(module));
    }

    void TearDown() override {
        clear_modules();
    }

    // Monta um PE32+ sintético com a import table em data e resolve.
    ResolveResult resolve(const std::vector<ImportSpec>& dlls) {
        const std::vector<std::byte> data = make_import_data(dlls);
        BuildSpec spec;
        spec.section_data.push_back({});
        spec.section_data.push_back(data);
        spec.import_rva = kImportDataRva;
        spec.import_size = static_cast<std::uint32_t>((dlls.size() + 1) * 20);
        add_empty_reloc(spec);
        const std::vector<std::byte> bytes = build(spec);
        const pe::ParseResult parse_result = pe::parse_pe(bytes);
        EXPECT_EQ(parse_result.status, pe::ParseStatus::Success);
        MapResult map_result = map_image(parse_result.info, bytes,
                                         {.preferred_base = kTestPreferredBase});
        EXPECT_EQ(map_result.status, MapStatus::Success);
        mapped = std::move(map_result.image);
        return resolve_imports(mapped, parse_result.info);
    }

    MappedImage mapped;
};

TEST_F(ImportResolverTest, ResolvesByNameAndWritesIat) {
    const ResolveResult result = resolve({{"FAKE.dll", {"DoWork"}, {}}});
    ASSERT_EQ(result.status, ImportStatus::Resolved);
    ASSERT_EQ(result.imports.size(), 1U);
    const ResolvedImport& entry = result.imports[0];
    EXPECT_EQ(entry.dll, "FAKE.dll");
    EXPECT_FALSE(entry.by_ordinal);
    EXPECT_EQ(entry.symbol, "DoWork");
    EXPECT_EQ(entry.status, ImportStatus::Resolved);
    EXPECT_NE(entry.iat_rva, 0U);
    EXPECT_EQ(read_le_u64(mapped, entry.iat_rva),
              reinterpret_cast<std::uintptr_t>(&tl_GetStdHandle));
}

TEST_F(ImportResolverTest, ResolvesByOrdinalAndWritesIat) {
    const ResolveResult result = resolve({{"FAKE.dll", {}, {2}}});
    ASSERT_EQ(result.status, ImportStatus::Resolved);
    ASSERT_EQ(result.imports.size(), 1U);
    const ResolvedImport& entry = result.imports[0];
    EXPECT_TRUE(entry.by_ordinal);
    EXPECT_EQ(entry.ordinal, 2U);
    EXPECT_EQ(entry.status, ImportStatus::Resolved);
    EXPECT_EQ(read_le_u64(mapped, entry.iat_rva),
              reinterpret_cast<std::uintptr_t>(&tl_WriteFile));
}

TEST_F(ImportResolverTest, PatchesEveryIatSlotInOrder) {
    const ResolveResult result = resolve({{"FAKE.dll", {"DoWork", "WriteFile"}, {}}});
    ASSERT_EQ(result.status, ImportStatus::Resolved);
    ASSERT_EQ(result.imports.size(), 2U);
    EXPECT_EQ(result.imports[0].address,
              reinterpret_cast<std::uintptr_t>(&tl_GetStdHandle));
    EXPECT_EQ(result.imports[1].address,
              reinterpret_cast<std::uintptr_t>(&tl_WriteFile));
    EXPECT_NE(result.imports[0].iat_rva, result.imports[1].iat_rva);
}

TEST_F(ImportResolverTest, ReportsUnknownDll) {
    const ResolveResult result = resolve({{"NOPE.dll", {"DoWork"}, {}}});
    EXPECT_EQ(result.status, ImportStatus::UnknownDll);
    ASSERT_EQ(result.imports.size(), 1U);
    EXPECT_EQ(result.imports[0].status, ImportStatus::UnknownDll);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(ImportResolverTest, ReportsUnknownSymbol) {
    const ResolveResult result = resolve({{"FAKE.dll", {"Missing"}, {}}});
    EXPECT_EQ(result.status, ImportStatus::UnknownSymbol);
    ASSERT_EQ(result.imports.size(), 1U);
    EXPECT_EQ(result.imports[0].status, ImportStatus::UnknownSymbol);
}

TEST_F(ImportResolverTest, ReportsUnknownOrdinal) {
    const ResolveResult result = resolve({{"FAKE.dll", {}, {99}}});
    EXPECT_EQ(result.status, ImportStatus::UnknownOrdinal);
    ASSERT_EQ(result.imports.size(), 1U);
    EXPECT_EQ(result.imports[0].status, ImportStatus::UnknownOrdinal);
}

TEST_F(ImportResolverTest, ReportsKnownSymbolWithoutImplementation) {
    clear_modules();
    const ExportedFunction exports[] = {
        {"Unavailable", 7, 0},
    };
    ASSERT_TRUE(register_module(InternalModule{"FAKE.dll", exports}));

    const ResolveResult result = resolve({{"FAKE.dll", {"Unavailable"}, {}}});
    EXPECT_EQ(result.status, ImportStatus::NotImpl);
    ASSERT_EQ(result.imports.size(), 1U);
    EXPECT_EQ(result.imports[0].status, ImportStatus::NotImpl);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(ImportResolverTest, PropagatesInvalidIatSlotToOverallStatus) {
    const std::vector<std::byte> data = make_import_data({{"FAKE.dll", {"DoWork"}, {}}});
    BuildSpec spec;
    spec.section_data.push_back({});
    spec.section_data.push_back(data);
    spec.import_rva = kImportDataRva;
    spec.import_size = 40;
    add_empty_reloc(spec);
    std::vector<std::byte> bytes = build(spec);

    const pe::ParseResult parse_result = pe::parse_pe(bytes);
    ASSERT_EQ(parse_result.status, pe::ParseStatus::Success);
    MapResult map_result = map_image(parse_result.info, bytes,
                                     {.preferred_base = kTestPreferredBase});
    ASSERT_EQ(map_result.status, MapStatus::Success);
    mapped = std::move(map_result.image);

    // Corrompe somente a informação consumida pelo resolvedor, mantendo a
    // imagem mapeada válida para exercitar a proteção contra IAT inválida.
    pe::PeInfo info = parse_result.info;
    ASSERT_EQ(info.imports.size(), 1U);
    ASSERT_EQ(info.imports[0].symbols.size(), 1U);
    info.imports[0].symbols[0].iat_rva = info.size_of_image + 8;

    const ResolveResult result = resolve_imports(mapped, info);
    EXPECT_EQ(result.status, ImportStatus::UnsupportedMechanism);
    ASSERT_EQ(result.imports.size(), 1U);
    EXPECT_EQ(result.imports[0].status, ImportStatus::UnsupportedMechanism);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(ImportResolverTest, ReportsEveryFailureAndKeepsFirstAsOverall) {
    const ResolveResult result = resolve({{"NOPE.dll", {"A"}, {}}, {"FAKE.dll", {"Missing"}, {}}});
    EXPECT_EQ(result.status, ImportStatus::UnknownDll);
    ASSERT_EQ(result.imports.size(), 2U);
    EXPECT_EQ(result.imports[0].status, ImportStatus::UnknownDll);
    EXPECT_EQ(result.imports[1].status, ImportStatus::UnknownSymbol);
}

TEST_F(ImportResolverTest, RejectsDelayImportDirectory) {
    const std::vector<std::byte> data = make_import_data({{"FAKE.dll", {"DoWork"}, {}}});
    BuildSpec spec;
    spec.section_data.push_back({});
    spec.section_data.push_back(data);
    spec.import_rva = kImportDataRva;
    spec.import_size = 40;
    add_empty_reloc(spec);
    std::vector<std::byte> bytes = build(spec);
    // Diretório de dados 13 (delay import): entrada com RVA não nulo.
    constexpr std::size_t kOptionalStart = 0x58;
    constexpr std::size_t kDirectorySize = 8;
    write_u32(bytes, kOptionalStart + 112 + 13 * kDirectorySize, kImportDataRva);
    write_u32(bytes, kOptionalStart + 112 + 13 * kDirectorySize + 4, kDirectorySize);

    const pe::ParseResult parse_result = pe::parse_pe(bytes);
    ASSERT_EQ(parse_result.status, pe::ParseStatus::Success);
    MapResult map_result = map_image(parse_result.info, bytes,
                                     {.preferred_base = kTestPreferredBase});
    ASSERT_EQ(map_result.status, MapStatus::Success);
    mapped = std::move(map_result.image);

    const ResolveResult result = resolve_imports(mapped, parse_result.info);
    EXPECT_EQ(result.status, ImportStatus::UnsupportedMechanism);
    EXPECT_TRUE(result.imports.empty());
}

TEST_F(ImportResolverTest, EmptyImportsResolveCleanly) {
    BuildSpec spec;
    spec.section_data.push_back({});
    spec.section_data.push_back({});
    add_empty_reloc(spec);
    const std::vector<std::byte> bytes = build(spec);
    const pe::ParseResult parse_result = pe::parse_pe(bytes);
    ASSERT_EQ(parse_result.status, pe::ParseStatus::Success);
    MapResult map_result = map_image(parse_result.info, bytes,
                                     {.preferred_base = kTestPreferredBase});
    ASSERT_EQ(map_result.status, MapStatus::Success);
    mapped = std::move(map_result.image);

    const ResolveResult result = resolve_imports(mapped, parse_result.info);
    EXPECT_EQ(result.status, ImportStatus::Resolved);
    EXPECT_TRUE(result.imports.empty());
}

TEST_F(ImportResolverTest, RestoresIatPagePermissionsAfterPatch) {
    const std::vector<std::byte> data = make_import_data({{"FAKE.dll", {"DoWork"}, {}}});
    BuildSpec spec;
    spec.section_data.push_back({});
    spec.section_data.push_back(data);
    spec.import_rva = kImportDataRva;
    spec.import_size = 40;
    add_empty_reloc(spec);
    const std::vector<std::byte> bytes = build(spec);
    const pe::ParseResult parse_result = pe::parse_pe(bytes);
    ASSERT_EQ(parse_result.status, pe::ParseStatus::Success);
    MapResult map_result = map_image(parse_result.info, bytes,
                                     {.preferred_base = kTestPreferredBase});
    ASSERT_EQ(map_result.status, MapStatus::Success);
    mapped = std::move(map_result.image);

    const ResolveResult result = resolve_imports(mapped, parse_result.info);
    ASSERT_EQ(result.status, ImportStatus::Resolved);
    ASSERT_FALSE(result.imports.empty());

    const long page_value = sysconf(_SC_PAGESIZE);
    const std::uint32_t page =
        page_value > 0 ? static_cast<std::uint32_t>(page_value) : 0x1000U;
    EXPECT_EQ(mapped.regions[1].permissions, SectionPermissions::ReadOnly);
    const std::uintptr_t page_address = static_cast<std::uintptr_t>(
        mapped.base + static_cast<std::uint64_t>(align_page_down(result.imports[0].iat_rva, page)));
    EXPECT_EQ(maps_permissions_for(page_address), "r--p");
}

}  // namespace
}  // namespace tradutorlinux::loader
