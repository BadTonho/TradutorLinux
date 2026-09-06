#include "pe_builder.hpp"
#include "tradutorlinux/compat/profile.hpp"
#include "tradutorlinux/loader/image_mapper.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/module_graph.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/runtime/guest_context.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <unistd.h>

namespace tradutorlinux::loader {
namespace {

using namespace tradutorlinux::pe::testutil;

std::vector<std::byte> make_export_dll(const bool attach_succeeds = true) {
    constexpr std::uint32_t export_rva = 0x2000;
    std::vector<std::byte> data(0xA0, std::byte{0});
    write_u32(data, 12, export_rva + 0x54);
    write_u32(data, 16, 1);
    write_u32(data, 20, 2);
    write_u32(data, 24, 1);
    write_u32(data, 28, export_rva + 0x40);
    write_u32(data, 32, export_rva + 0x48);
    write_u32(data, 36, export_rva + 0x4C);
    write_u32(data, 0x40, 0x1000);
    write_u32(data, 0x44, export_rva + 0x70);
    write_u32(data, 0x48, export_rva + 0x60);
    write_u16(data, 0x4C, 0);
    const auto put = [&data](const std::size_t offset, const char* value) {
        const std::size_t size = std::strlen(value) + 1U;
        std::copy_n(reinterpret_cast<const std::byte*>(value), size,
                    data.begin() + static_cast<std::ptrdiff_t>(offset));
    };
    put(0x54, "shim.dll");
    put(0x60, "CustomEntry");
    put(0x70, "KERNEL32.ExitProcess");
    write_u32(data, 0x90, 0x2000);
    write_u32(data, 0x94, 12);
    write_u16(data, 0x98, 0);
    write_u16(data, 0x9A, 0);

    BuildSpec spec;
    spec.coff_characteristics = 0x2022;
    std::vector<std::byte> text(0x10, std::byte{0});
    text[0] = std::byte{0xB8};  // mov eax, 1
    text[1] = std::byte{0x01};
    text[2] = std::byte{0x00};
    text[3] = std::byte{0x00};
    text[4] = std::byte{0x00};
    text[5] = std::byte{0xC3};  // ret
    text[6] = std::byte{0xB8};  // mov eax, attach_succeeds ? 1 : 0
    text[7] = attach_succeeds ? std::byte{0x01} : std::byte{0x00};
    text[8] = std::byte{0x00};
    text[9] = std::byte{0x00};
    text[10] = std::byte{0x00};
    text[11] = std::byte{0xC3};  // ret
    spec.section_names = {".text", ".edata"};
    spec.entry_point = 0x1006;
    spec.section_data = {std::move(text), data};
    spec.export_rva = export_rva;
    spec.export_size = 0xA0;
    spec.reloc_rva = export_rva + 0x90;
    spec.reloc_size = 12;
    return build(spec);
}

std::vector<std::byte> make_importing_executable() {
    const std::vector<std::byte> data = make_import_data({
        {"KERNEL32.dll", {"CustomEntry", "GetStdHandle"}, {}},
    });
    std::vector<std::byte> relocations(12, std::byte{0});
    write_u32(relocations, 0, 0x1000);
    write_u32(relocations, 4, 12);
    write_u16(relocations, 8, 0xA000);
    BuildSpec spec;
    spec.section_count = 3;
    spec.size_of_image = 0x4000;
    spec.section_names = {".text", ".idata", ".reloc"};
    spec.section_data = {std::vector<std::byte>(0x20), data, relocations};
    spec.import_rva = kImportDataRva;
    spec.import_size = 40;
    spec.reloc_rva = 0x3000;
    spec.reloc_size = 12;
    return build(spec);
}

class ModuleGraphTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() /
                ("tl-module-graph-" + std::to_string(static_cast<unsigned long long>(::getpid())));
        std::filesystem::remove_all(root_);
        ASSERT_TRUE(prefix::initialize_prefix(root_));
        runtime::GuestContextScope scope(context_);
        register_builtin_modules();
        std::ofstream output(compat::dlls_directory(root_) / "shim.dll",
                             std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output);
        const std::vector<std::byte> bytes = make_export_dll();
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    void TearDown() override {
        std::filesystem::remove_all(root_);
    }

    std::filesystem::path root_;
    runtime::GuestContext context_;
};

TEST_F(ModuleGraphTest, ResolvesProfileExportAndFallsBackPerExport) {
    runtime::GuestContextScope scope(context_);
    register_builtin_modules();
    compat::Profile profile;
    profile.schema = 2;
    profile.app_id = "fixture";
    profile.dlls.push_back({"KERNEL32.dll", "shim.dll"});
    GuestModuleGraph graph(root_, profile, root_ / "app.exe", false);

    const std::vector<std::byte> executable = make_importing_executable();
    const pe::ParseResult dll_parsed = pe::parse_pe(make_export_dll());
    ASSERT_EQ(dll_parsed.status, pe::ParseStatus::Success) << dll_parsed.error_message;
    ASSERT_TRUE(dll_parsed.info.is_dll);
    ASSERT_TRUE(dll_parsed.info.is_pe32_plus);
    const pe::ParseResult parsed = pe::parse_pe(executable);
    ASSERT_EQ(parsed.status, pe::ParseStatus::Success) << parsed.error_message;
    MapResult mapped = map_image(parsed.info, executable);
    ASSERT_EQ(mapped.status, MapStatus::Success) << mapped.error_message;

    ResolveResult resolved = graph.resolve_imports(mapped.image, parsed.info, root_ / "app.exe");
    ASSERT_EQ(resolved.status, ImportStatus::Resolved) << resolved.error_message;
    ASSERT_EQ(resolved.imports.size(), 2U);
    EXPECT_EQ(resolved.imports[0].provider, "profile");
    EXPECT_EQ(resolved.imports[1].provider, "builtin");
    EXPECT_NE(resolved.imports[0].address, 0U);
    EXPECT_NE(resolved.imports[1].address, 0U);

    void* const handle = graph.get_module_handle("KERNEL32.dll");
    ASSERT_NE(handle, nullptr);
    const GraphExportLookup custom = graph.get_proc_address(handle, "CustomEntry");
    ASSERT_TRUE(custom.lookup.found);
    EXPECT_EQ(custom.provider, ModuleProvider::Profile);
    EXPECT_EQ(custom.lookup.forwarder, "");
    EXPECT_TRUE(graph.is_valid_module_handle(handle));

    unmap_image(mapped.image);
}

TEST_F(ModuleGraphTest, LoadsAndUnloadsProfileDllWithReferenceCounting) {
    runtime::GuestContextScope scope(context_);
    register_builtin_modules();
    compat::Profile profile;
    profile.schema = 2;
    profile.app_id = "fixture";
    profile.dlls.push_back({"KERNEL32.dll", "shim.dll"});
    GuestModuleGraph graph(root_, profile, root_ / "app.exe", false);

    void* const handle = graph.load_library("KERNEL32.dll");
    ASSERT_NE(handle, nullptr);
    ASSERT_TRUE(graph.is_valid_module_handle(handle));
    const GraphExportLookup entry = graph.get_proc_address(handle, "CustomEntry");
    ASSERT_TRUE(entry.lookup.found);
    ASSERT_NE(entry.lookup.address, 0U);
    using EntryPoint = TL_MSABI int (*)();
    EXPECT_EQ(reinterpret_cast<EntryPoint>(entry.lookup.address)(), 1);

    EXPECT_TRUE(graph.free_library(handle));
    EXPECT_FALSE(graph.is_valid_module_handle(handle));
}

TEST_F(ModuleGraphTest, MissingProfileDllFallsBackToBuiltinProvider) {
    runtime::GuestContextScope scope(context_);
    register_builtin_modules();
    compat::Profile profile;
    profile.schema = 2;
    profile.app_id = "fixture";
    profile.dlls.push_back({"KERNEL32.dll", "missing.dll"});
    GuestModuleGraph graph(root_, profile, root_ / "app.exe", false);

    const std::vector<std::byte> executable = make_importing_executable();
    const pe::ParseResult parsed = pe::parse_pe(executable);
    ASSERT_EQ(parsed.status, pe::ParseStatus::Success) << parsed.error_message;
    MapResult mapped = map_image(parsed.info, executable);
    ASSERT_EQ(mapped.status, MapStatus::Success) << mapped.error_message;

    const ResolveResult resolved =
        graph.resolve_imports(mapped.image, parsed.info, root_ / "app.exe");
    ASSERT_EQ(resolved.status, ImportStatus::UnknownSymbol);
    ASSERT_EQ(resolved.imports.size(), 2U);
    EXPECT_EQ(resolved.imports[1].provider, "builtin");
    unmap_image(mapped.image);
}

TEST_F(ModuleGraphTest, KnownBuiltinMissingExportReportsUnknownSymbol) {
    runtime::GuestContextScope scope(context_);
    register_builtin_modules();
    ASSERT_TRUE(is_module_registered("USER32.dll"));
    compat::Profile profile;
    profile.schema = 2;
    profile.app_id = "fixture";
    GuestModuleGraph graph(root_, profile, root_ / "app.exe", false);

    const std::vector<std::byte> import_data = make_import_data({
        {"USER32.dll", {"TlUnknownSymbolW"}, {}},
    });
    std::vector<std::byte> relocations(8, std::byte{0});
    write_u32(relocations, 4, 8);
    BuildSpec spec;
    spec.section_count = 3;
    spec.size_of_image = 0x4000;
    spec.section_names = {".text", ".idata", ".reloc"};
    spec.section_data = {std::vector<std::byte>(0x20), import_data, relocations};
    spec.import_rva = kImportDataRva;
    spec.import_size = 40;
    spec.reloc_rva = 0x3000;
    spec.reloc_size = 8;
    const std::vector<std::byte> image_bytes = build(spec);
    const pe::ParseResult parsed = pe::parse_pe(image_bytes);
    ASSERT_EQ(parsed.status, pe::ParseStatus::Success) << parsed.error_message;
    MapResult mapped = map_image(parsed.info, image_bytes);
    ASSERT_EQ(mapped.status, MapStatus::Success) << mapped.error_message;

    const ResolveResult resolved =
        graph.resolve_imports(mapped.image, parsed.info, root_ / "app.exe");
    ASSERT_EQ(resolved.status, ImportStatus::UnknownSymbol);
    ASSERT_EQ(resolved.imports.size(), 1U);
    EXPECT_EQ(resolved.imports[0].status, ImportStatus::UnknownSymbol);
    EXPECT_EQ(resolved.imports[0].detail, "símbolo não exportado pelo módulo");
    unmap_image(mapped.image);
}

TEST_F(ModuleGraphTest, FailedProfileAttachIsReplacedByDriveProvider) {
    runtime::GuestContextScope scope(context_);
    register_builtin_modules();
    const auto paths = prefix::get_environment_paths(root_);
    std::ofstream drive_dll(paths.drive_c / "KERNEL32.dll",
                            std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(drive_dll);
    const std::vector<std::byte> drive_bytes = make_export_dll(true);
    drive_dll.write(reinterpret_cast<const char*>(drive_bytes.data()),
                    static_cast<std::streamsize>(drive_bytes.size()));
    drive_dll.close();
    std::ofstream profile_dll(compat::dlls_directory(root_) / "shim.dll",
                              std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(profile_dll);
    const std::vector<std::byte> profile_bytes = make_export_dll(false);
    profile_dll.write(reinterpret_cast<const char*>(profile_bytes.data()),
                      static_cast<std::streamsize>(profile_bytes.size()));
    profile_dll.close();

    compat::Profile profile;
    profile.schema = 2;
    profile.app_id = "fixture";
    profile.dlls.push_back({"KERNEL32.dll", "shim.dll"});
    GuestModuleGraph graph(root_, profile, root_ / "app.exe", true);
    const std::vector<std::byte> executable = make_importing_executable();
    const pe::ParseResult parsed = pe::parse_pe(executable);
    ASSERT_EQ(parsed.status, pe::ParseStatus::Success) << parsed.error_message;
    MapResult mapped = map_image(parsed.info, executable);
    ASSERT_EQ(mapped.status, MapStatus::Success) << mapped.error_message;
    const ResolveResult resolved =
        graph.resolve_imports(mapped.image, parsed.info, root_ / "app.exe");
    ASSERT_EQ(resolved.status, ImportStatus::Resolved) << resolved.error_message;
    ASSERT_TRUE(graph.process_attach());

    void* const drive_handle = graph.get_module_handle("KERNEL32.dll");
    ASSERT_NE(drive_handle, nullptr);
    std::uint64_t custom_address = 0;
    std::memcpy(&custom_address, mapped.image.memory + resolved.imports[0].iat_rva,
                sizeof(custom_address));
    EXPECT_EQ(custom_address, reinterpret_cast<std::uintptr_t>(drive_handle) + 0x1000U);
    graph.process_detach();
    unmap_image(mapped.image);
}

}  // namespace
}  // namespace tradutorlinux::loader
