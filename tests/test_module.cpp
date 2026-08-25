#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/runtime/advapi.hpp"
#include "tradutorlinux/runtime/msvcrt.hpp"
#include "tradutorlinux/runtime/ole32.hpp"
#include "tradutorlinux/runtime/unwind.hpp"
#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/runtime/wininet.hpp"
#include "tradutorlinux/runtime/wintrust.hpp"

#include <cstdint>
#include <string_view>

#include <gtest/gtest.h>

namespace tradutorlinux::loader {
namespace {

constexpr ExportedFunction kFakeExports[] = {
    {"DoWork", 1, 0x4000000000000001ULL},
    {"CleanUp", 2, 0x4000000000000002ULL},
};

constexpr InternalModule kFakeModule{"FAKE.dll", kFakeExports};

class ModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_modules();
    }

    void TearDown() override {
        clear_modules();
    }
};

TEST_F(ModuleTest, RegistersModuleAndFindsExports) {
    ASSERT_TRUE(register_module(kFakeModule));
    ASSERT_EQ(registered_module_count(), 1U);

    const ExportLookup by_name = find_export(ExportQuery{"FAKE.dll", "DoWork"});
    ASSERT_TRUE(by_name.found);
    EXPECT_EQ(by_name.ordinal, 1U);
    EXPECT_EQ(by_name.address, 0x4000000000000001ULL);

    const ExportLookup by_ordinal = find_export_by_ordinal("FAKE.dll", 2);
    ASSERT_TRUE(by_ordinal.found);
    EXPECT_EQ(by_ordinal.ordinal, 2U);
    EXPECT_EQ(by_ordinal.address, 0x4000000000000002ULL);
}

TEST_F(ModuleTest, DllNamesAreCaseInsensitive) {
    ASSERT_TRUE(register_module(kFakeModule));
    EXPECT_TRUE(find_export(ExportQuery{"fake.dll", "DoWork"}).found);
    EXPECT_TRUE(find_export(ExportQuery{"FAKE.DLL", "DoWork"}).found);
    EXPECT_TRUE(find_export(ExportQuery{"fake.Dll", "DoWork"}).found);
}

TEST_F(ModuleTest, SymbolNamesAreCaseSensitive) {
    ASSERT_TRUE(register_module(kFakeModule));
    EXPECT_FALSE(find_export(ExportQuery{"FAKE.dll", "dowork"}).found);
}

TEST_F(ModuleTest, UnknownDllAndSymbolAreNotFound) {
    ASSERT_TRUE(register_module(kFakeModule));
    EXPECT_FALSE(find_export(ExportQuery{"NOPE.dll", "DoWork"}).found);
    EXPECT_FALSE(find_export(ExportQuery{"FAKE.dll", "Missing"}).found);
    EXPECT_FALSE(find_export_by_ordinal("FAKE.dll", 99).found);
    EXPECT_FALSE(find_export_by_ordinal("NOPE.dll", 1).found);
}

TEST_F(ModuleTest, DuplicateRegistrationIsRejected) {
    ASSERT_TRUE(register_module(kFakeModule));
    EXPECT_FALSE(register_module(kFakeModule));
    EXPECT_FALSE(register_module(InternalModule{"fake.dll", kFakeExports}));
    EXPECT_EQ(registered_module_count(), 1U);
}

TEST_F(ModuleTest, ClearModulesResetsRegistry) {
    ASSERT_TRUE(register_module(kFakeModule));
    clear_modules();
    EXPECT_EQ(registered_module_count(), 0U);
    EXPECT_FALSE(find_export(ExportQuery{"FAKE.dll", "DoWork"}).found);
}

TEST_F(ModuleTest, RegistersBuiltinKernel32Exports) {
    register_builtin_modules();
    ASSERT_EQ(registered_module_count(), 22U);
    EXPECT_TRUE(is_module_registered("KERNEL32.dll"));
    EXPECT_TRUE(is_module_registered("USER32.dll"));
    EXPECT_TRUE(is_module_registered("GDI32.dll"));
    EXPECT_TRUE(is_module_registered("msvcrt.dll"));
    EXPECT_TRUE(is_module_registered("SHELL32.dll"));
    EXPECT_TRUE(is_module_registered("ADVAPI32.dll"));
    EXPECT_TRUE(is_module_registered("WS2_32.dll"));
    EXPECT_TRUE(is_module_registered("WININET.dll"));
    EXPECT_TRUE(is_module_registered("WINTRUST.dll"));

    const ExportLookup std_handle = find_export(ExportQuery{"KERNEL32.dll", "GetStdHandle"});
    ASSERT_TRUE(std_handle.found);
    EXPECT_EQ(std_handle.address,
              reinterpret_cast<std::uintptr_t>(&tl_GetStdHandle));
    EXPECT_EQ(find_export(ExportQuery{"kernel32.dll", "WriteFile"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_WriteFile));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "ExitProcess"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_ExitProcess));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "ReadFile"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_ReadFile));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "VirtualQuery"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_VirtualQuery));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "MultiByteToWideChar"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_MultiByteToWideChar));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "SetUnhandledExceptionFilter"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_SetUnhandledExceptionFilter));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "TlsGetValue"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_TlsGetValue));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "RtlCaptureContext"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_RtlCaptureContext));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "RtlLookupFunctionEntry"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_RtlLookupFunctionEntry));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "RtlVirtualUnwind"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_RtlVirtualUnwind));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "RtlPcToFileHeader"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_RtlPcToFileHeader));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "SetEnvironmentVariableW"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_SetEnvironmentVariableW));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "FlsAlloc"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_FlsAlloc));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "GetLocaleInfoW"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_GetLocaleInfoW));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "EnumSystemLocalesW"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_EnumSystemLocalesW));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "GetDateFormatW"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_GetDateFormatW));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "GetStartupInfoW"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_GetStartupInfoW));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "WriteConsoleW"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_WriteConsoleW));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "InitializeSListHead"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_InitializeSListHead));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "FindFirstFileExW"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_FindFirstFileExW));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "SetFileAttributesW"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_SetFileAttributesW));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "SetFileInformationByHandle"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_SetFileInformationByHandle));
    EXPECT_EQ(find_export_by_ordinal("KERNEL32.dll", 1).address,
              reinterpret_cast<std::uintptr_t>(&tl_GetStdHandle));
    EXPECT_EQ(find_export_by_ordinal("KERNEL32.dll", 24).address,
              reinterpret_cast<std::uintptr_t>(&tl_WideCharToMultiByte));
    EXPECT_EQ(find_export(ExportQuery{"USER32.dll", "MessageBoxA"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_MessageBoxA));
    EXPECT_EQ(find_export(ExportQuery{"GDI32.dll", "GetStockObject"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_GetStockObject));
    EXPECT_EQ(find_export(ExportQuery{"USER32.dll", "FillRect"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_FillRect));
    EXPECT_EQ(find_export(ExportQuery{"GDI32.dll", "Rectangle"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_Rectangle));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "GetFileAttributesW"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_GetFileAttributesW));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "FormatMessageW"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_FormatMessageW));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "FormatMessageA"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_FormatMessageA));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "AreFileApisANSI"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_AreFileApisANSI));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "InitializeCriticalSectionAndSpinCount"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_InitializeCriticalSectionAndSpinCount));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "InitializeCriticalSectionEx"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_InitializeCriticalSectionEx));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "GetTempFileNameW"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_GetTempFileNameW));
    EXPECT_EQ(find_export(ExportQuery{"KERNEL32.dll", "LocalFree"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_LocalFree));
    EXPECT_EQ(find_export(ExportQuery{"SHELL32.dll", "CommandLineToArgvW"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_CommandLineToArgvW));
    EXPECT_EQ(find_export_by_ordinal("KERNEL32.dll", 58).address,
              reinterpret_cast<std::uintptr_t>(&tl_GetFileAttributesW));
    EXPECT_EQ(find_export_by_ordinal("SHELL32.dll", 1).address,
              reinterpret_cast<std::uintptr_t>(&tl_CommandLineToArgvW));
    EXPECT_EQ(find_export(ExportQuery{"ADVAPI32.dll", "RegOpenKeyExA"}).found, true);
    EXPECT_EQ(find_export(ExportQuery{"ADVAPI32.dll", "GetNamedSecurityInfoW"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_GetNamedSecurityInfoW));
    EXPECT_EQ(find_export(ExportQuery{"ADVAPI32.dll", "SetEntriesInAclW"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_SetEntriesInAclW));
    EXPECT_EQ(find_export(ExportQuery{"WININET.dll", "InternetOpenW"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_InternetOpenW));
    EXPECT_EQ(find_export(ExportQuery{"WININET.dll", "HttpSendRequestW"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_HttpSendRequestW));
    EXPECT_EQ(find_export(ExportQuery{"ole32.dll", "CreateStreamOnHGlobal"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_CreateStreamOnHGlobal));
}

TEST_F(ModuleTest, RegistersMsvcrtExports) {
    register_builtin_modules();
    const ExportLookup getmainargs = find_export(ExportQuery{"msvcrt.dll", "__getmainargs"});
    ASSERT_TRUE(getmainargs.found);
    EXPECT_EQ(getmainargs.address,
              reinterpret_cast<std::uintptr_t>(&tl___getmainargs));
    EXPECT_EQ(find_export(ExportQuery{"MSVCRT.dll", "fopen"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_fopen));
    EXPECT_EQ(find_export(ExportQuery{"msvcrt.dll", "___lc_codepage_func"}).address,
              reinterpret_cast<std::uintptr_t>(&tl___lc_codepage_func));
    EXPECT_EQ(find_export_by_ordinal("msvcrt.dll", 57).address,
              reinterpret_cast<std::uintptr_t>(&g_guest_fmode));
    EXPECT_EQ(find_export(ExportQuery{"msvcrt.dll", "_wfopen"}).address,
              reinterpret_cast<std::uintptr_t>(&tl__wfopen));
    EXPECT_EQ(find_export(ExportQuery{"msvcrt.dll", "fwprintf"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_fwprintf));
    EXPECT_EQ(find_export(ExportQuery{"msvcrt.dll", "wcstombs"}).address,
              reinterpret_cast<std::uintptr_t>(&tl_wcstombs));
    EXPECT_EQ(find_export_by_ordinal("msvcrt.dll", 69).address,
              reinterpret_cast<std::uintptr_t>(&tl_realloc));
    EXPECT_EQ(find_export_by_ordinal("msvcrt.dll", 90).address,
              reinterpret_cast<std::uintptr_t>(&tl_fputwc));
}

TEST_F(ModuleTest, RegisterBuiltinModulesIsIdempotent) {
    register_builtin_modules();
    register_builtin_modules();
    EXPECT_EQ(registered_module_count(), 22U);
}

TEST_F(ModuleTest, RegistryOwnsItsStrings) {
    const char* name = "TRANSIENT.dll";
    const char* symbol = "Temp";
    ExportedFunction export_{symbol, 7, 0x5000000000000007ULL};
    const ExportedFunction exports[] = {export_};
    const InternalModule module{name, exports};
    ASSERT_TRUE(register_module(module));
    EXPECT_TRUE(find_export(ExportQuery{"transient.dll", "Temp"}).found);
}

}  // namespace
}  // namespace tradutorlinux::loader
