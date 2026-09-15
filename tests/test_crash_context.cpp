#include "tradutorlinux/diagnostics/crash_context.hpp"

#include <dlfcn.h>

#include <gtest/gtest.h>

namespace {

using tradutorlinux::diagnostics::describe_guest_crash;
using tradutorlinux::diagnostics::describe_host_address;
using tradutorlinux::loader::ImportStatus;
using tradutorlinux::loader::MappedImage;
using tradutorlinux::loader::MapRegion;
using tradutorlinux::loader::ResolveResult;
using tradutorlinux::loader::ResolvedImport;

constexpr std::uint64_t kBase = 0x140000000ULL;

MappedImage make_image() {
    MappedImage image;
    image.preferred_base = kBase;
    image.base = kBase;
    image.size = 0x3000;
    image.regions.push_back(MapRegion{".text", 0x1000, 0x800, 0x400, 0x800,
                                      tradutorlinux::loader::SectionPermissions::ReadExecute});
    image.regions.push_back(MapRegion{".rdata", 0x2000, 0x400, 0x1800, 0x400,
                                      tradutorlinux::loader::SectionPermissions::ReadOnly});
    return image;
}

ResolveResult make_imports() {
    ResolveResult imports;
    imports.imports.push_back(
        ResolvedImport{"KERNEL32.dll", false, "ExitProcess", 0, 0x1030, 0, ImportStatus::Resolved, {},
                       tradutorlinux::loader::ImportMechanism::Static,
                       tradutorlinux::loader::ExportSupport::Full, "builtin"});
    imports.imports.push_back(
        ResolvedImport{"USER32.dll", false, "GetMessageA", 0, 0x2040, 0, ImportStatus::Resolved, {},
                       tradutorlinux::loader::ImportMechanism::Static,
                       tradutorlinux::loader::ExportSupport::Full, "builtin"});
    // Entrada não resolvida não pode virar candidata no diagnóstico.
    imports.imports.push_back(
        ResolvedImport{"KERNEL32.dll", false, "TlUnknownSymbolW", 0, 0x1050, 0,
                       ImportStatus::UnknownSymbol, "símbolo desconhecido",
                       tradutorlinux::loader::ImportMechanism::Static,
                       tradutorlinux::loader::ExportSupport::Full, ""});
    return imports;
}

TEST(CrashContextTest, AddressInsideSectionReturnsRvaSectionAndNearestImport) {
    const MappedImage image = make_image();
    const ResolveResult imports = make_imports();
    const auto context = describe_guest_crash(image, imports, kBase + 0x1058);
    ASSERT_TRUE(context.valid);
    EXPECT_EQ(context.rva, 0x1058U);
    EXPECT_EQ(context.section, ".text");
    // A entrada em 0x1050 é UnknownSymbol e é excluída; o slot resolvido mais
    // próximo abaixo é ExitProcess em 0x1030.
    EXPECT_EQ(context.nearest_import, "KERNEL32.dll!ExitProcess");
}

TEST(CrashContextTest, NearestImportPicksHighestSlotBelowFault) {
    const MappedImage image = make_image();
    const ResolveResult imports = make_imports();
    const auto context = describe_guest_crash(image, imports, kBase + 0x2060);
    ASSERT_TRUE(context.valid);
    EXPECT_EQ(context.rva, 0x2060U);
    EXPECT_EQ(context.section, ".rdata");
    EXPECT_EQ(context.nearest_import, "USER32.dll!GetMessageA");
}

TEST(CrashContextTest, AddressInSectionGapHasNoSectionName) {
    const MappedImage image = make_image();
    const ResolveResult imports = make_imports();
    // Intervalo entre o fim de .text (0x1800) e o início de .rdata (0x2000).
    const auto context = describe_guest_crash(image, imports, kBase + 0x1900);
    ASSERT_TRUE(context.valid);
    EXPECT_TRUE(context.section.empty());
    EXPECT_EQ(context.nearest_import, "KERNEL32.dll!ExitProcess");
}

TEST(CrashContextTest, NullAddressIsOutsideTheImage) {
    const MappedImage image = make_image();
    const ResolveResult imports = make_imports();
    const auto context = describe_guest_crash(image, imports, 0);
    EXPECT_FALSE(context.valid);
    EXPECT_TRUE(context.nearest_import.empty());
}

TEST(CrashContextTest, NullHostAddressHasNoContext) {
    const auto context = describe_host_address(0);
    EXPECT_FALSE(context.valid);
    EXPECT_TRUE(context.module.empty());
    EXPECT_TRUE(context.symbol.empty());
}

TEST(CrashContextTest, HostFunctionCanBeResolvedWithoutAbsolutePath) {
    const auto context = describe_host_address(
        reinterpret_cast<std::uintptr_t>(&::dladdr));
    ASSERT_TRUE(context.valid);
    EXPECT_FALSE(context.module.empty());
    ASSERT_FALSE(context.symbol.empty());
    EXPECT_EQ(context.symbol_offset, 0U);
    EXPECT_NE(context.module_offset, 0U);
    EXPECT_EQ(context.module.find('/'), std::string::npos);
}

TEST(CrashContextTest, AddressAtEndOfImageIsOutside) {
    const MappedImage image = make_image();
    const ResolveResult imports = make_imports();
    const auto context = describe_guest_crash(image, imports, kBase + image.size);
    EXPECT_FALSE(context.valid);
}

TEST(CrashContextTest, WithoutResolvedImportsNearestImportStaysEmpty) {
    const MappedImage image = make_image();
    ResolveResult imports = make_imports();
    for (auto& entry : imports.imports) {
        entry.status = ImportStatus::NotImpl;
    }
    const auto context = describe_guest_crash(image, imports, kBase + 0x1100);
    ASSERT_TRUE(context.valid);
    EXPECT_TRUE(context.nearest_import.empty());
}

TEST(CrashContextTest, IdentifiesFaultInGuestStackGuardPageAsStackOverflow) {
    const void* const stack = reinterpret_cast<const void*>(0x70000000);
    constexpr std::size_t guard_size = 4096;

    EXPECT_TRUE(tradutorlinux::diagnostics::is_stack_overflow_fault(0x70000000, stack, guard_size));
    EXPECT_TRUE(tradutorlinux::diagnostics::is_stack_overflow_fault(0x70000800, stack, guard_size));
    EXPECT_TRUE(tradutorlinux::diagnostics::is_stack_overflow_fault(0x70000FFF, stack, guard_size));

    EXPECT_FALSE(tradutorlinux::diagnostics::is_stack_overflow_fault(0x70001000, stack, guard_size));
    EXPECT_FALSE(tradutorlinux::diagnostics::is_stack_overflow_fault(0x6FFFFFFF, stack, guard_size));
    EXPECT_FALSE(tradutorlinux::diagnostics::is_stack_overflow_fault(0x0, stack, guard_size));
    EXPECT_FALSE(tradutorlinux::diagnostics::is_stack_overflow_fault(0x70000000, nullptr, guard_size));
    EXPECT_FALSE(tradutorlinux::diagnostics::is_stack_overflow_fault(0x70000000, stack, 0));
}

}  // namespace
