#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/runtime/advapi.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <unistd.h>
#include <sys/stat.h>

namespace tradutorlinux {
namespace {

using abi::GuestMemoryBasicInformation;

TEST(Win32CodePageTest, Cp1252ConvertsByte80ToEuroSign) {
    const char input[] = {'c', 'a', 'f', static_cast<char>(0xE9), static_cast<char>(0x80), '\0'};
    std::uint16_t output[8]{};
    const int written =
        tl_MultiByteToWideChar(abi::kCp1252, 0, input, -1, output, 8);
    ASSERT_EQ(written, 6);
    EXPECT_EQ(output[0], 'c');
    EXPECT_EQ(output[3], 0x00E9);
    EXPECT_EQ(output[4], 0x20AC);
    EXPECT_EQ(output[5], 0);
}

TEST(Win32CodePageTest, CpAcpIsAnAliasForCp1252) {
    const char input[] = {static_cast<char>(0x9F), '\0'};
    std::uint16_t output[2]{};
    const int written =
        tl_MultiByteToWideChar(abi::kCpAcp, 0, input, 1, output, 2);
    ASSERT_EQ(written, 1);
    EXPECT_EQ(output[0], 0x0178);
}

TEST(Win32CodePageTest, Utf8ExpandsSurrogatePairIntoTwoUnits) {
    const char input[] = {static_cast<char>(0xF0), static_cast<char>(0x9F),
                          static_cast<char>(0x98), static_cast<char>(0x80), '\0'};
    std::uint16_t output[3]{};
    const int written =
        tl_MultiByteToWideChar(abi::kCpUtf8, 0, input, -1, output, 3);
    ASSERT_EQ(written, 3);
    EXPECT_EQ(output[0], 0xD83D);
    EXPECT_EQ(output[1], 0xDE00);
    EXPECT_EQ(output[2], 0);
}

TEST(Win32CodePageTest, CountOnlyCallReportsRequiredUnits) {
    const char input[] = {'A', static_cast<char>(0xC3), static_cast<char>(0xA7), '\0'};
    const int needed = tl_MultiByteToWideChar(abi::kCpUtf8, 0, input, -1, nullptr, 0);
    EXPECT_EQ(needed, 3);
}

TEST(Win32CodePageTest, InsufficientBufferFailsWithError122) {
    const char input[] = {'A', 'B', 'C', '\0'};
    std::uint16_t output[2]{};
    EXPECT_EQ(tl_MultiByteToWideChar(abi::kCpUtf8, 0, input, -1, output, 2), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInsufficientBuffer);
}

TEST(Win32CodePageTest, RejectsUnsupportedCodePage) {
    const char input[] = {'A', '\0'};
    std::uint16_t output[4]{};
    EXPECT_EQ(tl_MultiByteToWideChar(874, 0, input, -1, output, 4), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32CodePageTest, InvalidUtf8WithStrictFlagFails) {
    const char input[] = {static_cast<char>(0xC3), 'A', '\0'};
    std::uint16_t output[4]{};
    EXPECT_EQ(tl_MultiByteToWideChar(abi::kCpUtf8, abi::kMbErrInvalidChars, input, -1,
                                     output, 4),
              0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNoUnicodeTranslation);
}

TEST(Win32CodePageTest, WideToUtf8EncodesEuroSignInThreeBytes) {
    const std::uint16_t input[] = {0x20AC, 0};
    char output[8]{};
    const int written = tl_WideCharToMultiByte(abi::kCpUtf8, 0, input, -1, output, 8,
                                               nullptr, nullptr);
    ASSERT_EQ(written, 4);
    EXPECT_EQ(static_cast<unsigned char>(output[0]), 0xE2);
    EXPECT_EQ(static_cast<unsigned char>(output[1]), 0x82);
    EXPECT_EQ(static_cast<unsigned char>(output[2]), 0xAC);
    EXPECT_EQ(output[3], '\0');
}

TEST(Win32CodePageTest, WideToCp1252UsesBestFitAndReportsDefault) {
    const std::uint16_t input[] = {0x20AC, 0xD83D, 0xDE00, 0};
    char output[4]{};
    int used_default = -1;
    const int written = tl_WideCharToMultiByte(abi::kCp1252, 0, input, -1, output, 4,
                                               nullptr, &used_default);
    ASSERT_EQ(written, 3);
    EXPECT_EQ(static_cast<unsigned char>(output[0]), 0x80);
    EXPECT_EQ(output[1], '?');
    EXPECT_EQ(output[2], '\0');
    EXPECT_EQ(used_default, 1);
}

TEST(Win32CodePageTest, Utf8RoundTripPreservesSurrogatePair) {
    const std::uint16_t input[] = {0xD83D, 0xDE00, 0};
    char utf8[16]{};
    const int utf8_len = tl_WideCharToMultiByte(abi::kCpUtf8, 0, input, -1, utf8, 16,
                                                nullptr, nullptr);
    std::uint16_t back[4]{};
    const int wide_len =
        tl_MultiByteToWideChar(abi::kCpUtf8, 0, utf8, utf8_len - 1, back, 4);
    ASSERT_EQ(wide_len, 2);
    EXPECT_EQ(back[0], 0xD83D);
    EXPECT_EQ(back[1], 0xDE00);
}

TEST(Win32VirtualTest, QueryDescribesAnAnonymousAllocation) {
    void* memory = tl_VirtualAlloc(nullptr, 0x2000, abi::kMemCommit | abi::kMemReserve,
                                   abi::kPageReadWrite);
    ASSERT_NE(memory, nullptr);
    GuestMemoryBasicInformation info{};
    const std::uintptr_t returned = tl_VirtualQuery(memory, &info, sizeof(info));
    EXPECT_EQ(returned, sizeof(GuestMemoryBasicInformation));
    EXPECT_EQ(info.base_address, memory);
    EXPECT_EQ(info.allocation_base, memory);
    EXPECT_EQ(info.state, abi::kMemCommit);
    EXPECT_EQ(info.type, abi::kMemPrivate);
    EXPECT_EQ(info.protect, abi::kPageReadWrite);
    EXPECT_GE(info.region_size, 0x2000U);
    EXPECT_EQ(tl_VirtualFree(memory, 0, abi::kMemRelease), 1);
}

TEST(Win32VirtualTest, QueryRejectsUnmappedAddress) {
    GuestMemoryBasicInformation info{};
    EXPECT_EQ(tl_VirtualQuery(nullptr, &info, sizeof(info)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidAddress);
}

TEST(Win32VirtualTest, ProtectChangesPermissionsAndReportsOldOnes) {
    void* memory = tl_VirtualAlloc(nullptr, 0x1000, abi::kMemCommit | abi::kMemReserve,
                                   abi::kPageReadWrite);
    ASSERT_NE(memory, nullptr);
    std::uint32_t old_protection = 0;
    ASSERT_EQ(tl_VirtualProtect(memory, 0x1000, abi::kPageReadOnly, &old_protection), 1);
    EXPECT_EQ(old_protection, abi::kPageReadWrite);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
    EXPECT_EQ(static_cast<const volatile char*>(memory)[0], 0);
    ASSERT_EQ(tl_VirtualProtect(memory, 0x1000, abi::kPageReadWrite, &old_protection), 1);
    EXPECT_EQ(old_protection, abi::kPageReadOnly);
    EXPECT_EQ(tl_VirtualFree(memory, 0, abi::kMemRelease), 1);
}

TEST(Win32TlsTest, GetValueReturnsNullForUnusedSlot) {
    EXPECT_EQ(tl_TlsGetValue(0), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
}

TEST(Win32TlsTest, GetValueRejectsIndexOutsideTable) {
    EXPECT_EQ(tl_TlsGetValue(64), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32ConsoleTest, GetConsoleModeRejectsNullModePointer) {
    EXPECT_EQ(tl_GetConsoleMode(reinterpret_cast<const void*>(0x1), nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32ConsoleTest, GetConsoleModeRejectsUnknownHandle) {
    std::uint32_t mode = 0;
    EXPECT_EQ(tl_GetConsoleMode(reinterpret_cast<const void*>(0x1), &mode), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidHandle);
}

TEST(Win32ConsoleTest, SetConsoleModeRejectsUnknownHandle) {
    EXPECT_EQ(tl_SetConsoleMode(reinterpret_cast<const void*>(0x1), 0x3), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidHandle);
}

TEST(Win32ConsoleTest, IsDBCSLeadByteExAlwaysReturnsFalse) {
    EXPECT_EQ(tl_IsDBCSLeadByteEx(abi::kCp1252, 0x81), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
}

TEST(Win32CriticalSectionTest, LifecycleWithValidPointerIsTrivial) {
    char critical_section[8]{};
    tl_InitializeCriticalSection(critical_section);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
    tl_EnterCriticalSection(critical_section);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
    tl_LeaveCriticalSection(critical_section);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
    tl_DeleteCriticalSection(critical_section);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
}

TEST(Win32CriticalSectionTest, NullPointerIsRejected) {
    tl_InitializeCriticalSection(nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    tl_EnterCriticalSection(nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32UnhandledExceptionFilterTest, ReturnsPreviousHandler) {
    const std::uintptr_t first = tl_SetUnhandledExceptionFilter(0x1234);
    EXPECT_EQ(first, 0U);
    const std::uintptr_t second = tl_SetUnhandledExceptionFilter(0x5678);
    EXPECT_EQ(second, 0x1234U);
    const std::uintptr_t third = tl_SetUnhandledExceptionFilter(0);
    EXPECT_EQ(third, 0x5678U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
}

TEST(Win32SleepTest, SleepsZeroAndOneMillisecond) {
    tl_Sleep(0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
    tl_Sleep(1);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
}

TEST(Win32ModuleTest, GetModuleHandleAKnownDllReturnsNonZero) {
    EXPECT_NE(tl_GetModuleHandleA("kernel32.dll"), nullptr);
    EXPECT_NE(tl_GetModuleHandleA("msvcrt.dll"), nullptr);
    EXPECT_NE(tl_GetModuleHandleA("USER32.dll"), nullptr);
}

TEST(Win32ModuleTest, GetModuleHandleAUnknownReturnsNull) {
    EXPECT_EQ(tl_GetModuleHandleA("foo.dll"), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorFileNotFound);
}

TEST(Win32ModuleTest, GetModuleHandleANullReturnsDefault) {
    EXPECT_NE(tl_GetModuleHandleA(nullptr), nullptr);
}

TEST(Win32ModuleTest, GetModuleHandleWConvertsToA) {
    const std::uint16_t name[] = {'k', 'e', 'r', 'n', 'e', 'l', '3', '2', '.', 'd', 'l', 'l', 0};
    EXPECT_NE(tl_GetModuleHandleW(name), nullptr);
}

TEST(Win32ModuleTest, GetProcAddressReturnsNullForStub) {
    void* handle = tl_GetModuleHandleA("kernel32.dll");
    EXPECT_EQ(tl_GetProcAddress(handle, "SomeFunction"), nullptr);
}

TEST(Win32CommandLineTest, GetCommandLineAReturnsNonEmpty) {
    const char* cmdline = tl_GetCommandLineA();
    ASSERT_NE(cmdline, nullptr);
    EXPECT_GT(std::strlen(cmdline), 0U);
}

TEST(Win32CommandLineTest, GetCommandLineWReturnsNonEmpty) {
    const std::uint16_t* cmdline = tl_GetCommandLineW();
    ASSERT_NE(cmdline, nullptr);
    EXPECT_NE(cmdline[0], 0);
}

TEST(Win32EnvTest, GetEnvironmentVariableAFindsPath) {
    const std::uint32_t needed = tl_GetEnvironmentVariableA("PATH", nullptr, 0);
    EXPECT_GT(needed, 0U);

    std::vector<char> buffer(needed + 1, '\0');
    const std::uint32_t written = tl_GetEnvironmentVariableA("PATH", buffer.data(),
                                                               needed + 1);
    EXPECT_EQ(written, needed);
    EXPECT_GT(std::strlen(buffer.data()), 0U);
}

TEST(Win32EnvTest, GetEnvironmentVariableAMissingReturnsZero) {
    EXPECT_EQ(tl_GetEnvironmentVariableA("TL_NONEXISTENT_VAR_12345", nullptr, 0), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorFileNotFound);
}

TEST(Win32EnvTest, GetEnvironmentVariableAInsufficientBuffer) {
    const std::uint32_t needed = tl_GetEnvironmentVariableA("PATH", nullptr, 0);
    if (needed > 0) {
        std::vector<char> tiny(2, '\0');
        const std::uint32_t result = tl_GetEnvironmentVariableA("PATH", tiny.data(), 2);
        EXPECT_EQ(result, needed);
        EXPECT_EQ(tl_GetLastError(), abi::kErrorInsufficientBuffer);
    }
}

TEST(Win32GuiAbiTest, TargetControlLayoutsMatchMicrosoftX64) {
    EXPECT_EQ(sizeof(abi::GuestWndClassA), 72U);
    EXPECT_EQ(sizeof(abi::GuestWndClassExA), 80U);
    EXPECT_EQ(sizeof(abi::GuestLvColumnA), 32U);
    EXPECT_EQ(sizeof(abi::GuestLvItemA), 72U);
    EXPECT_EQ(sizeof(abi::GuestNmListView), 64U);
}

TEST(Win32GuiTest, MulDivRoundsAndRejectsZeroDenominator) {
    EXPECT_EQ(tl_MulDiv(5, 3, 2), 8);
    EXPECT_EQ(tl_MulDiv(-5, 3, 2), -8);
    EXPECT_EQ(tl_MulDiv(1, 1, 0), -1);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32GuiTest, PopupMenuHandleHasLifecycle) {
    void* menu = tl_CreatePopupMenu();
    ASSERT_NE(menu, nullptr);
    EXPECT_EQ(tl_AppendMenuA(menu, 0, 101, "Exit"), 1);
    EXPECT_EQ(tl_DestroyMenu(menu), 1);
}

TEST(Win32RegistryTest, TodoAutorunValueRoundTrips) {
    const void* current_user = reinterpret_cast<const void*>(0x80000001U);
    void* key = nullptr;
    ASSERT_EQ(tl_RegOpenKeyExA(current_user,
                               "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                               0, &key), abi::kErrorSuccess);
    const char executable[] = "simple_todo.exe\0";
    ASSERT_EQ(tl_RegSetValueExA(key, "TodoApp", 0, 1,
                                reinterpret_cast<const unsigned char*>(executable),
                                sizeof(executable)), abi::kErrorSuccess);
    char value[64]{};
    std::uint32_t size = sizeof(value);
    EXPECT_EQ(tl_RegQueryValueExA(key, "TodoApp", nullptr, nullptr,
                                  reinterpret_cast<unsigned char*>(value), &size),
              abi::kErrorSuccess);
    EXPECT_STREQ(value, "simple_todo.exe");
    EXPECT_EQ(tl_RegDeleteValueA(key, "TodoApp"), abi::kErrorSuccess);
    EXPECT_EQ(tl_RegCloseKey(key), abi::kErrorSuccess);
    void* sign_extended_key = nullptr;
    ASSERT_EQ(tl_RegOpenKeyExA(reinterpret_cast<const void*>(0xFFFFFFFF80000001ULL),
                               "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                               0, &sign_extended_key), abi::kErrorSuccess);
    EXPECT_EQ(tl_RegCloseKey(sign_extended_key), abi::kErrorSuccess);
    std::remove(".tl_registry_todo");
}
TEST(Win32EnvTest, GetEnvironmentVariableWConvertsResult) {
    const std::uint16_t name[] = {'P', 'A', 'T', 'H', 0};
    const std::uint32_t needed = tl_GetEnvironmentVariableW(name, nullptr, 0);
    EXPECT_GT(needed, 0U);
}

TEST(Win32HeapTest, GetProcessHeapReturnsNonNull) {
    EXPECT_NE(tl_GetProcessHeap(), nullptr);
}

TEST(Win32HeapTest, HeapAllocAndHeapFreeRoundTrip) {
    void* heap = tl_GetProcessHeap();
    ASSERT_NE(heap, nullptr);

    void* block = tl_HeapAlloc(heap, 0, 128);
    ASSERT_NE(block, nullptr);
    std::memset(block, 0xAB, 128);
    EXPECT_EQ(static_cast<unsigned char*>(block)[0], 0xAB);
    EXPECT_EQ(tl_HeapFree(heap, 0, block), 1);
}

TEST(Win32HeapTest, HeapAllocZeroFlagClearsMemory) {
    void* heap = tl_GetProcessHeap();
    void* block = tl_HeapAlloc(heap, 0x0008, 64);
    ASSERT_NE(block, nullptr);
    for (std::size_t i = 0; i < 64; ++i) {
        EXPECT_EQ(static_cast<unsigned char*>(block)[i], 0);
    }
    tl_HeapFree(heap, 0, block);
}

TEST(Win32HeapTest, HeapReAllocGrowsBlock) {
    void* heap = tl_GetProcessHeap();
    void* block = tl_HeapAlloc(heap, 0, 32);
    ASSERT_NE(block, nullptr);
    std::memset(block, 0xCC, 32);

    void* grown = tl_HeapReAlloc(heap, 0, block, 64);
    ASSERT_NE(grown, nullptr);
    EXPECT_EQ(static_cast<unsigned char*>(grown)[0], 0xCC);
    tl_HeapFree(heap, 0, grown);
}

TEST(Win32TimeTest, GetTickCount64ReturnsIncreasingValue) {
    const std::uint64_t t1 = tl_GetTickCount64();
    tl_Sleep(1);
    const std::uint64_t t2 = tl_GetTickCount64();
    EXPECT_GE(t2, t1);
    EXPECT_GT(t2 - t1, 0U);
}

TEST(Win32TimeTest, GetSystemTimeAsFileTimeReturnsReasonableValue) {
    std::uint64_t ft = 0;
    tl_GetSystemTimeAsFileTime(&ft);
    // Year 2020 in 100-ns intervals since 1601
    const std::uint64_t year_2020 = 132537600000000000ULL;
    EXPECT_GT(ft, year_2020);
}

// --- Fase 10: Sistema de arquivos e utilitários ---

// Helper: cria um diretório temporário relativo para testes de filesystem.
class TempDirFixture {
public:
    TempDirFixture() {
        mkdir("_tl_test", 0777);
    }
    ~TempDirFixture() {
        // Limpa arquivos criados.
        for (const auto& f : created_files_) {
            std::remove(f.c_str());
        }
        rmdir("_tl_test");
    }
    std::string path(const char* name) const {
        const std::string p = std::string("_tl_test/") + name;
        created_files_.push_back(p);
        return p;
    }
private:
    mutable std::vector<std::string> created_files_;
};

TEST(Win32FileTest, GetFileSizeReturnsCorrectSize) {
    TempDirFixture ctx;
    const std::string fpath = ctx.path("size_test.bin");
    FILE* f = std::fopen(fpath.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    const char data[] = "hello world";
    std::fwrite(data, 1, sizeof(data) - 1, f);
    std::fclose(f);

    void* handle = tl_CreateFileA(fpath.c_str(), abi::kGenericRead, 0, nullptr,
                                   abi::kOpenExisting, 0, nullptr);
    ASSERT_NE(handle, nullptr);
    std::uint32_t high = 0;
    const std::uint32_t size = tl_GetFileSize(handle, &high);
    EXPECT_EQ(size, sizeof(data) - 1);
    EXPECT_EQ(high, 0U);
    tl_CloseHandle(handle);
}

TEST(Win32FileTest, SetFilePointerSeeksToBeginning) {
    TempDirFixture ctx;
    const std::string fpath = ctx.path("seek_test.bin");
    FILE* f = std::fopen(fpath.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    const char data[] = "0123456789";
    std::fwrite(data, 1, sizeof(data) - 1, f);
    std::fclose(f);

    void* handle = tl_CreateFileA(fpath.c_str(), abi::kGenericRead, 0, nullptr,
                                   abi::kOpenExisting, 0, nullptr);
    ASSERT_NE(handle, nullptr);
    char buf[4]{};
    std::uint32_t bytes_read = 0;
    tl_ReadFile(handle, buf, 4, &bytes_read, nullptr);
    EXPECT_EQ(bytes_read, 4U);
    EXPECT_EQ(std::string(buf, 4), "0123");

    const std::int32_t pos = tl_SetFilePointer(handle, 0, nullptr, 0);
    EXPECT_EQ(pos, 0);

    bytes_read = 0;
    std::memset(buf, 0, sizeof(buf));
    tl_ReadFile(handle, buf, 4, &bytes_read, nullptr);
    EXPECT_EQ(std::string(buf, 4), "0123");
    tl_CloseHandle(handle);
}

TEST(Win32FileTest, SetFilePointerSeekFromEnd) {
    TempDirFixture ctx;
    const std::string fpath = ctx.path("seek_end_test.bin");
    FILE* f = std::fopen(fpath.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    const char data[] = "ABCDEFGHIJ";
    std::fwrite(data, 1, sizeof(data) - 1, f);
    std::fclose(f);

    void* handle = tl_CreateFileA(fpath.c_str(), abi::kGenericRead, 0, nullptr,
                                   abi::kOpenExisting, 0, nullptr);
    ASSERT_NE(handle, nullptr);
    const std::int32_t pos = tl_SetFilePointer(handle, -3, nullptr, 2);
    EXPECT_EQ(pos, 7);

    char buf[4]{};
    std::uint32_t bytes_read = 0;
    tl_ReadFile(handle, buf, 3, &bytes_read, nullptr);
    EXPECT_EQ(std::string(buf, 3), "HIJ");
    tl_CloseHandle(handle);
}

TEST(Win32FileTest, GetFileAttributesAReturnsArchiveForFile) {
    TempDirFixture ctx;
    const std::string fpath = ctx.path("attrs_test.bin");
    FILE* f = std::fopen(fpath.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fwrite("x", 1, 1, f);
    std::fclose(f);

    const std::uint32_t attrs = tl_GetFileAttributesA(fpath.c_str());
    EXPECT_NE(attrs, 0xFFFFFFFFU);
    EXPECT_NE(attrs & 0x20U, 0U);   // FILE_ATTRIBUTE_ARCHIVE
}

TEST(Win32FileTest, GetFileAttributesAReturnsDirectory) {
    const char* tmpdir = "_tl_test_dir";
    mkdir(tmpdir, 0777);
    const std::uint32_t attrs = tl_GetFileAttributesA(tmpdir);
    EXPECT_NE(attrs, 0xFFFFFFFFU);
    EXPECT_NE(attrs & 0x10U, 0U);   // FILE_ATTRIBUTE_DIRECTORY
    rmdir(tmpdir);
}

TEST(Win32FileTest, DeleteFileARemovesFile) {
    TempDirFixture ctx;
    const std::string fpath = ctx.path("delete_test.bin");
    FILE* f = std::fopen(fpath.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fwrite("x", 1, 1, f);
    std::fclose(f);

    EXPECT_EQ(tl_DeleteFileA(fpath.c_str()), 1);
    EXPECT_EQ(tl_GetFileAttributesA(fpath.c_str()), 0xFFFFFFFFU);
}

TEST(Win32FileTest, MoveFileARenamesFile) {
    TempDirFixture ctx;
    const std::string from = ctx.path("move_src.bin");
    const std::string to = "_tl_test/move_dst.bin";
    FILE* f = std::fopen(from.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fwrite("data", 1, 4, f);
    std::fclose(f);

    EXPECT_EQ(tl_MoveFileA(from.c_str(), to.c_str()), 1);
    EXPECT_EQ(tl_GetFileAttributesA(from.c_str()), 0xFFFFFFFFU);
    EXPECT_NE(tl_GetFileAttributesA(to.c_str()), 0xFFFFFFFFU);
    std::remove(to.c_str());
}

TEST(Win32FileTest, CreateDirectoryACreatesDirectory) {
    const char* tmpdir = "_tl_test_mkdir";
    EXPECT_EQ(tl_CreateDirectoryA(tmpdir, nullptr), 1);
    struct stat st{};
    EXPECT_EQ(stat(tmpdir, &st), 0);
    EXPECT_TRUE(S_ISDIR(st.st_mode));
    rmdir(tmpdir);
}

TEST(Win32FileTest, FindFirstFileAFindsFileInDirectory) {
    const char* tmpdir = "_tl_test_find";
    mkdir(tmpdir, 0777);
    const std::string file_path = std::string(tmpdir) + "/testfile.txt";
    FILE* f = std::fopen(file_path.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fwrite("content", 1, 7, f);
    std::fclose(f);

    const std::string pattern = std::string(tmpdir) + "/*";
    char find_data_buf[400]{};
    void* find_handle = tl_FindFirstFileA(pattern.c_str(), find_data_buf);
    ASSERT_NE(find_handle, reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max()));
    tl_FindClose(find_handle);
    std::remove(file_path.c_str());
    rmdir(tmpdir);
}

TEST(Win32FileTest, GetFileSizeFailsForInvalidHandle) {
    std::uint32_t high = 0;
    const std::uint32_t size = tl_GetFileSize(nullptr, &high);
    EXPECT_EQ(size, 0xFFFFFFFFU);
}

TEST(Win32FileTest, GetFileAttributesAFailsForNonExistent) {
    const std::uint32_t attrs = tl_GetFileAttributesA("_tl_nonexistent_file_999");
    EXPECT_EQ(attrs, 0xFFFFFFFFU);
}

TEST(Win32FileTest, DeleteFileAFailsForNonExistent) {
    EXPECT_EQ(tl_DeleteFileA("_tl_nonexistent_file_999"), 0);
}

TEST(Win32FileTest, MoveFileAFailsForNonExistentSource) {
    EXPECT_EQ(tl_MoveFileA("_tl_nonexistent_src", "_tl_nonexistent_dst"), 0);
}

TEST(Win32FileTest, CreateDirectoryAFailsForExistingDir) {
    const char* tmpdir = "_tl_test_mkdir_dup";
    mkdir(tmpdir, 0777);
    EXPECT_EQ(tl_CreateDirectoryA(tmpdir, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorAlreadyExists);
    rmdir(tmpdir);
}

TEST(Win32FileTest, SetFilePointerFailsForNegativePosition) {
    TempDirFixture ctx;
    const std::string fpath = ctx.path("seek_neg.bin");
    FILE* f = std::fopen(fpath.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fwrite("01234", 1, 5, f);
    std::fclose(f);

    void* handle = tl_CreateFileA(fpath.c_str(), abi::kGenericRead, 0, nullptr,
                                   abi::kOpenExisting, 0, nullptr);
    ASSERT_NE(handle, nullptr);
    // Seek before beginning.
    const std::int32_t pos = tl_SetFilePointer(handle, -10, nullptr, 0);
    EXPECT_EQ(pos, -1);
    tl_CloseHandle(handle);
}

TEST(Win32WideFileTest, UnicodeFileMetadataPositionAndCopyAreConsistent) {
    TempDirFixture ctx;
    const auto to_wide = [](const std::u16string& value) {
        std::vector<std::uint16_t> result(value.begin(), value.end());
        result.push_back(0);
        return result;
    };
    const std::vector<std::uint16_t> source = to_wide(u"_tl_test/arquivo_\u00e9.txt");
    const std::vector<std::uint16_t> copy = to_wide(u"_tl_test/arquivo_\u00e9.copy");
    const std::vector<std::uint16_t> moved = to_wide(u"_tl_test/arquivo_\u00e9.moved");
    ctx.path("arquivo_\xC3\xA9.txt");
    ctx.path("arquivo_\xC3\xA9.copy");
    ctx.path("arquivo_\xC3\xA9.moved");

    void* handle = tl_CreateFileW(source.data(), abi::kGenericRead | abi::kGenericWrite, 0,
                                  nullptr, abi::kCreateAlways, 0, nullptr);
    ASSERT_NE(handle, nullptr);
    const char payload[] = "unicode";
    std::uint32_t written = 0;
    ASSERT_EQ(tl_WriteFile(handle, payload, sizeof(payload) - 1, &written, nullptr), 1);
    EXPECT_EQ(written, sizeof(payload) - 1);

    std::int64_t size = 0;
    ASSERT_EQ(tl_GetFileSizeEx(handle, &size), 1);
    EXPECT_EQ(size, static_cast<std::int64_t>(sizeof(payload) - 1));

    std::int64_t position = -1;
    ASSERT_EQ(tl_SetFilePointerEx(handle, 2, &position, 0), 1);
    EXPECT_EQ(position, 2);
    ASSERT_EQ(tl_SetEndOfFile(handle), 1);
    ASSERT_EQ(tl_GetFileSizeEx(handle, &size), 1);
    EXPECT_EQ(size, 2);
    ASSERT_EQ(tl_FlushFileBuffers(handle), 1);

    struct FileTime {
        std::uint32_t low{};
        std::uint32_t high{};
    } creation{}, access{}, write{};
    ASSERT_EQ(tl_GetFileTime(handle, &creation, &access, &write), 1);
    ASSERT_EQ(tl_SetFileTime(handle, &creation, &access, &write), 1);

    struct ByHandleInfo {
        std::uint32_t attributes{};
        FileTime creation{};
        FileTime access{};
        FileTime write{};
        std::uint32_t volume{};
        std::uint32_t size_high{};
        std::uint32_t size_low{};
        std::uint32_t links{};
        std::uint32_t index_high{};
        std::uint32_t index_low{};
    } info{};
    ASSERT_EQ(tl_GetFileInformationByHandle(handle, &info), 1);
    EXPECT_EQ(info.size_low, 2U);
    EXPECT_NE(info.links, 0U);

    struct BasicInfo {
        std::int64_t creation{};
        std::int64_t access{};
        std::int64_t write{};
        std::int64_t change{};
        std::uint32_t attributes{};
        std::uint32_t reserved{};
    } basic{};
    ASSERT_EQ(tl_GetFileInformationByHandleEx(handle, 0, &basic, sizeof(basic)), 1);
    EXPECT_NE(basic.attributes & 0x20U, 0U);

    std::uint16_t final_path[260]{};
    EXPECT_GT(tl_GetFinalPathNameByHandleW(handle, final_path, 260, 0), 0U);
    EXPECT_NE(std::u16string(reinterpret_cast<const char16_t*>(final_path)).find(u"arquivo_\u00e9"),
              std::u16string::npos);
    ASSERT_EQ(tl_CloseHandle(handle), 1);

    struct AttributeData {
        std::uint32_t attributes{};
        FileTime creation{};
        FileTime access{};
        FileTime write{};
        std::uint32_t size_high{};
        std::uint32_t size_low{};
    } attributes{};
    ASSERT_EQ(tl_GetFileAttributesExW(source.data(), 0, &attributes), 1);
    EXPECT_EQ(attributes.size_low, 2U);
    ASSERT_EQ(tl_CopyFileW(source.data(), copy.data(), 1), 1);
    ASSERT_EQ(tl_MoveFileExW(copy.data(), moved.data(), 1), 1);
    EXPECT_EQ(tl_DeleteFileW(source.data()), 1);
    EXPECT_EQ(tl_DeleteFileW(moved.data()), 1);
}

TEST(Win32DirTest, GetCurrentDirectoryAReturnsNonEmpty) {
    char buf[4096]{};
    const std::uint32_t needed = tl_GetCurrentDirectoryA(sizeof(buf), buf);
    EXPECT_GT(needed, 1U);
    EXPECT_STRNE(buf, "");
    // Deve conter \ (convertido de /).
    EXPECT_NE(std::strchr(buf, '\\'), nullptr);
}

TEST(Win32DirTest, GetCurrentDirectoryAReturnsNeededWhenBufferTooSmall) {
    const std::uint32_t needed = tl_GetCurrentDirectoryA(1, nullptr);
    EXPECT_GT(needed, 1U);
}

TEST(Win32DirTest, GetModuleFileNameAReturnsSetPath) {
    set_guest_module_path("test/path/app.exe");
    char buf[4096]{};
    const std::uint32_t len = tl_GetModuleFileNameA(nullptr, buf, sizeof(buf));
    EXPECT_GT(len, 0U);
    EXPECT_STREQ(buf, "test/path/app.exe");
    set_guest_module_path(nullptr);
}

TEST(Win32DirTest, GetModuleFileNameAReturnsNeededWhenBufferTooSmall) {
    set_guest_module_path("long/path.exe");
    const std::uint32_t needed = tl_GetModuleFileNameA(nullptr, nullptr, 0);
    EXPECT_GT(needed, 0U);
    set_guest_module_path(nullptr);
}

TEST(Win32Utf16Test, MultiByteToWideCharUtf8ConvertsAccentedChar) {
    // "café" em UTF-8: c a f é(0xC3 0xA9)
    const char utf8[] = {'c', 'a', 'f', static_cast<char>(0xC3), static_cast<char>(0xA9), '\0'};
    std::uint16_t wide[8]{};
    const int written = tl_MultiByteToWideChar(abi::kCpUtf8, 0, utf8, -1, wide, 8);
    ASSERT_EQ(written, 5);
    EXPECT_EQ(wide[0], 'c');
    EXPECT_EQ(wide[1], 'a');
    EXPECT_EQ(wide[2], 'f');
    EXPECT_EQ(wide[3], 0x00E9);  // é
    EXPECT_EQ(wide[4], 0);
}

TEST(Win32Utf16Test, WideCharToMultiByteUtf8ConvertsAccentedChar) {
    // é em UTF-16 → 0xC3 0xA9 em UTF-8.
    const std::uint16_t wide[] = {0x00E9, 0};
    char utf8[8]{};
    const int written = tl_WideCharToMultiByte(abi::kCpUtf8, 0, wide, -1, utf8, 8,
                                               nullptr, nullptr);
    ASSERT_EQ(written, 3);
    EXPECT_EQ(static_cast<unsigned char>(utf8[0]), 0xC3);
    EXPECT_EQ(static_cast<unsigned char>(utf8[1]), 0xA9);
    EXPECT_EQ(utf8[2], '\0');
}

TEST(Win32Utf16Test, GetCurrentDirectoryWReturnsWideString) {
    std::uint16_t buf[4096]{};
    const std::uint32_t needed = tl_GetCurrentDirectoryW(sizeof(buf) / sizeof(buf[0]), buf);
    EXPECT_GT(needed, 1U);
    // O primeiro caractere deve ser uma letra ou '\' (CWD relativo).
    EXPECT_TRUE(buf[0] == L'\\' || (buf[0] >= L'A' && buf[0] <= L'Z') ||
                (buf[0] >= L'a' && buf[0] <= L'z'));
}

// ==================== Fase 11: Concorrência ====================

TEST(Win32ConcurrencyTest, TlsAllocReturnsValidIndex) {
    const std::uint32_t idx1 = tl_TlsAlloc();
    EXPECT_NE(idx1, 0xFFFFFFFFU);
    EXPECT_LT(idx1, 64U);

    const std::uint32_t idx2 = tl_TlsAlloc();
    EXPECT_NE(idx2, 0xFFFFFFFFU);
    EXPECT_NE(idx2, idx1);

    // Liberar ambos para não consumir slots entre testes.
    EXPECT_NE(tl_TlsFree(idx1), 0);
    EXPECT_NE(tl_TlsFree(idx2), 0);
}

TEST(Win32ConcurrencyTest, TlsSetValueAndGetValueRoundTrip) {
    const std::uint32_t idx = tl_TlsAlloc();
    ASSERT_NE(idx, abi::kErrorTooManyTlsIndexes);

    void* sentinel = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xDEADBEEF));
    EXPECT_NE(tl_TlsSetValue(idx, sentinel), 0);
    EXPECT_EQ(tl_TlsGetValue(idx), sentinel);

    EXPECT_NE(tl_TlsFree(idx), 0);
}

TEST(Win32ConcurrencyTest, TlsSetValueRejectsInvalidIndex) {
    EXPECT_EQ(tl_TlsSetValue(63, nullptr), 0);
    EXPECT_EQ(tl_TlsSetValue(0xFFFFFFFF, nullptr), 0);
}

TEST(Win32ConcurrencyTest, TlsFreeInvalidIndexFails) {
    EXPECT_EQ(tl_TlsFree(63), 0);
    EXPECT_EQ(tl_TlsFree(0xFFFFFFFF), 0);
}

TEST(Win32ConcurrencyTest, GetCurrentThreadIdReturnsNonZero) {
    const std::uint32_t tid = tl_GetCurrentThreadId();
    EXPECT_NE(tid, 0U);
}

TEST(Win32ConcurrencyTest, GetCurrentProcessIdMatchesHost) {
    const std::uint32_t pid = tl_GetCurrentProcessId();
    EXPECT_EQ(pid, static_cast<std::uint32_t>(getpid()));
}

TEST(Win32ConcurrencyTest, CriticalSectionInitEnterLeaveDelete) {
    // CRITICAL_SECTION is 40 bytes on Windows x64.
    alignas(8) char cs[40]{};
    tl_InitializeCriticalSection(cs);
    tl_EnterCriticalSection(cs);
    tl_LeaveCriticalSection(cs);
    tl_DeleteCriticalSection(cs);
}

TEST(Win32ConcurrencyTest, CriticalSectionRejectsNull) {
    // All four should fail gracefully.
    tl_InitializeCriticalSection(nullptr);
    tl_EnterCriticalSection(nullptr);
    tl_LeaveCriticalSection(nullptr);
    tl_DeleteCriticalSection(nullptr);
}

TEST(Win32ConcurrencyTest, CloseHandleRejectsNull) {
    EXPECT_EQ(tl_CloseHandle(nullptr), 0);
}

TEST(Win32ConcurrencyTest, CloseHandleRejectsGarbage) {
    EXPECT_EQ(tl_CloseHandle(reinterpret_cast<const void*>(0x12345678ULL)), 0);
}

TEST(Win32ConcurrencyTest, WaitForSingleObjectRejectsInvalidHandle) {
    EXPECT_EQ(tl_WaitForSingleObject(reinterpret_cast<const void*>(0xBADULL), 0),
              abi::kWaitFailed);
}

TEST(Win32ConcurrencyTest, TlsAllocExhaustion) {
    // Allocate all 64 slots.
    std::uint32_t indices[64];
    std::uint32_t allocated = 0;
    for (std::uint32_t i = 0; i < 64; ++i) {
        indices[i] = tl_TlsAlloc();
        if (indices[i] == abi::kErrorTooManyTlsIndexes) break;
        ++allocated;
    }
    EXPECT_EQ(allocated, 64U);

    // The next allocation should fail (TLS_OUT_OF_INDEXES = 0xFFFFFFFF).
    EXPECT_EQ(tl_TlsAlloc(), 0xFFFFFFFFU);

    // Free all.
    for (std::uint32_t i = 0; i < allocated; ++i) {
        EXPECT_NE(tl_TlsFree(indices[i]), 0);
    }
}

TEST(Win32ConcurrencyTest, TlsFreeIdempotentOnInvalidIndex) {
    // Freeing an already-free or never-allocated index should fail but not crash.
    EXPECT_EQ(tl_TlsFree(0), 0);
    EXPECT_EQ(tl_TlsFree(0), 0);
}

TEST(Win32ConcurrencyTest, WaitForSingleObjectTimeoutReturnsWaitTimeout) {
    // WaitForSingleObject on an invalid handle with timeout should return WAIT_FAILED.
    EXPECT_EQ(tl_WaitForSingleObject(reinterpret_cast<const void*>(0xDEADULL), 100),
              abi::kWaitFailed);
}

TEST(Win32ConcurrencyTest, EventsSemaphoresMutexAndMultipleWaitsHaveWin32Semantics) {
    void* auto_event = tl_CreateEventA(nullptr, 0, 0, nullptr);
    ASSERT_NE(auto_event, nullptr);
    EXPECT_EQ(tl_WaitForSingleObject(auto_event, 0), abi::kWaitTimeout);
    ASSERT_EQ(tl_SetEvent(auto_event), 1);
    EXPECT_EQ(tl_WaitForSingleObject(auto_event, 0), abi::kWaitObject0);
    EXPECT_EQ(tl_WaitForSingleObject(auto_event, 0), abi::kWaitTimeout);

    void* manual_event = tl_CreateEventW(nullptr, 1, 1, nullptr);
    ASSERT_NE(manual_event, nullptr);
    EXPECT_EQ(tl_WaitForSingleObject(manual_event, 0), abi::kWaitObject0);
    EXPECT_EQ(tl_WaitForSingleObject(manual_event, 0), abi::kWaitObject0);
    ASSERT_EQ(tl_ResetEvent(manual_event), 1);
    EXPECT_EQ(tl_WaitForSingleObject(manual_event, 0), abi::kWaitTimeout);

    void* semaphore = tl_CreateSemaphoreW(nullptr, 0, 2, nullptr);
    ASSERT_NE(semaphore, nullptr);
    std::int32_t previous = -1;
    ASSERT_EQ(tl_ReleaseSemaphore(semaphore, 2, &previous), 1);
    EXPECT_EQ(previous, 0);
    EXPECT_EQ(tl_WaitForSingleObject(semaphore, 0), abi::kWaitObject0);
    EXPECT_EQ(tl_WaitForSingleObject(semaphore, 0), abi::kWaitObject0);
    EXPECT_EQ(tl_WaitForSingleObject(semaphore, 0), abi::kWaitTimeout);

    void* mutex = tl_CreateMutexW(nullptr, 0, nullptr);
    ASSERT_NE(mutex, nullptr);
    EXPECT_EQ(tl_WaitForSingleObject(mutex, 0), abi::kWaitObject0);
    EXPECT_EQ(tl_WaitForSingleObject(mutex, 0), abi::kWaitObject0);
    ASSERT_EQ(tl_ReleaseMutex(mutex), 1);
    ASSERT_EQ(tl_ReleaseMutex(mutex), 1);
    EXPECT_EQ(tl_ReleaseMutex(mutex), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorAccessDenied);

    void* first = tl_CreateEventA(nullptr, 1, 1, nullptr);
    void* second = tl_CreateEventA(nullptr, 1, 1, nullptr);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    const void* handles[2] = {first, second};
    EXPECT_EQ(tl_WaitForMultipleObjects(2, handles, 1, 0), abi::kWaitObject0);

    EXPECT_EQ(tl_CloseHandle(auto_event), 1);
    EXPECT_EQ(tl_CloseHandle(manual_event), 1);
    EXPECT_EQ(tl_CloseHandle(semaphore), 1);
    EXPECT_EQ(tl_CloseHandle(mutex), 1);
    EXPECT_EQ(tl_CloseHandle(first), 1);
    EXPECT_EQ(tl_CloseHandle(second), 1);
}

TEST(Win32ConcurrencyTest, TlsGetValueInvalidIndexReturnsNull) {
    // TlsGetValue on an index that was allocated then freed should return nullptr
    // (slot 0 is TEB self pointer and may be set).
    const std::uint32_t idx = tl_TlsAlloc();
    ASSERT_NE(idx, 0xFFFFFFFFU);
    EXPECT_NE(tl_TlsSetValue(idx, nullptr), 0);
    EXPECT_EQ(tl_TlsGetValue(idx), nullptr);
    EXPECT_NE(tl_TlsFree(idx), 0);
    // After free, the slot is released. Using an out-of-range index returns nullptr.
    EXPECT_EQ(tl_TlsGetValue(0xFFFFFFFF), nullptr);
}

TEST(Win32ConcurrencyTest, GetCurrentThreadIdConsistentAcrossCalls) {
    // Multiple calls to GetCurrentThreadId should return the same value.
    const std::uint32_t tid1 = tl_GetCurrentThreadId();
    const std::uint32_t tid2 = tl_GetCurrentThreadId();
    EXPECT_EQ(tid1, tid2);
    EXPECT_NE(tid1, 0U);
}

TEST(Win32ConcurrencyTest, CriticalSectionSideTableExhaustion) {
    // Allocate all 32 CS slots and verify exhaustion is handled.
    alignas(8) char cs_slots[32][40]{};
    for (int i = 0; i < 32; ++i) {
        tl_InitializeCriticalSection(cs_slots[i]);
        tl_EnterCriticalSection(cs_slots[i]);
    }
    // All 32 slots occupied — this should not crash (just emit trace).
    alignas(8) char extra_cs[40]{};
    tl_InitializeCriticalSection(extra_cs);

    // Free all in reverse order.
    for (int i = 31; i >= 0; --i) {
        tl_LeaveCriticalSection(cs_slots[i]);
        tl_DeleteCriticalSection(cs_slots[i]);
    }
    tl_DeleteCriticalSection(extra_cs);
}

TEST(Win32ConcurrencyTest, TlsSetGetValueMultipleSlots) {
    // Allocate multiple TLS slots, set values, verify independence.
    const std::uint32_t idx1 = tl_TlsAlloc();
    const std::uint32_t idx2 = tl_TlsAlloc();
    const std::uint32_t idx3 = tl_TlsAlloc();
    ASSERT_NE(idx1, 0xFFFFFFFFU);
    ASSERT_NE(idx2, 0xFFFFFFFFU);
    ASSERT_NE(idx3, 0xFFFFFFFFU);

    void* val1 = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1111));
    void* val2 = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x2222));
    void* val3 = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x3333));

    EXPECT_NE(tl_TlsSetValue(idx1, val1), 0);
    EXPECT_NE(tl_TlsSetValue(idx2, val2), 0);
    EXPECT_NE(tl_TlsSetValue(idx3, val3), 0);

    EXPECT_EQ(tl_TlsGetValue(idx1), val1);
    EXPECT_EQ(tl_TlsGetValue(idx2), val2);
    EXPECT_EQ(tl_TlsGetValue(idx3), val3);

    // Overwrite idx2.
    EXPECT_NE(tl_TlsSetValue(idx2, val3), 0);
    EXPECT_EQ(tl_TlsGetValue(idx2), val3);
    EXPECT_EQ(tl_TlsGetValue(idx1), val1);

    EXPECT_NE(tl_TlsFree(idx1), 0);
    EXPECT_NE(tl_TlsFree(idx2), 0);
    EXPECT_NE(tl_TlsFree(idx3), 0);
}

// ---------------------------------------------------------------------------
// Variantes wide (Fase 10+).
// ---------------------------------------------------------------------------

TEST(Win32WideTest, GetFileAttributesWMatchesAByConvertedPath) {
    const char* tmpdir = "_tl_test_attrw";
    mkdir(tmpdir, 0777);
    const std::string file_path = std::string(tmpdir) + "/wide_attr.txt";
    FILE* f = std::fopen(file_path.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fclose(f);
    std::uint16_t wide_path[260]{};
    for (std::size_t i = 0; i < file_path.size(); ++i) {
        wide_path[i] = static_cast<std::uint16_t>(static_cast<unsigned char>(file_path[i]));
    }
    const std::uint32_t attrs_w = tl_GetFileAttributesW(wide_path);
    const std::uint32_t attrs_a = tl_GetFileAttributesA(file_path.c_str());
    EXPECT_EQ(attrs_w, attrs_a);
    std::remove(file_path.c_str());
    rmdir(tmpdir);
}

TEST(Win32WideTest, GetFileAttributesWFailsForNonExistent) {
    const std::uint16_t wide_path[] = {'_', 't', 'l', '_', 'n', 'o', 'n', 'e', 'x', 'i', 's', 't',
                                       '_', 'w', 0};
    EXPECT_EQ(tl_GetFileAttributesW(wide_path), 0xFFFFFFFFU);
}

TEST(Win32WideTest, FindFirstFileWReturnsWideName) {
    const char* tmpdir = "_tl_test_findw";
    mkdir(tmpdir, 0777);
    const std::string file_path = std::string(tmpdir) + "/widefile.txt";
    FILE* f = std::fopen(file_path.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fwrite("content", 1, 7, f);
    std::fclose(f);

    std::uint16_t wide_pattern[260]{};
    const std::string pattern = std::string(tmpdir) + "/*";
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        wide_pattern[i] = static_cast<std::uint16_t>(static_cast<unsigned char>(pattern[i]));
    }
    alignas(8) unsigned char find_data_buf[592]{};
    void* find_handle = tl_FindFirstFileW(wide_pattern, find_data_buf);
    ASSERT_NE(find_handle, reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max()));
    const auto* data = reinterpret_cast<const std::uint16_t*>(find_data_buf + 44);
    EXPECT_EQ(std::u16string(reinterpret_cast<const char16_t*>(data)), u"widefile.txt");
    EXPECT_EQ(tl_FindNextFileW(find_handle, find_data_buf), 0);
    tl_FindClose(find_handle);
    std::remove(file_path.c_str());
    rmdir(tmpdir);
}

TEST(Win32WideTest, FindFirstFileWFailsForMissingDirectory) {
    std::uint16_t wide_pattern[] = {'_', 't', 'l', '_', 'm', 'i', 's', 's', 'i', 'n', 'g', '_', 'w',
                                    '/', '*', 0};
    alignas(8) unsigned char find_data_buf[592]{};
    EXPECT_EQ(tl_FindFirstFileW(wide_pattern, find_data_buf),
              reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max()));
}

TEST(Win32WideTest, FormatMessageWWithAllocateBufferReturnsKnownMessage) {
    constexpr std::uint32_t kFormatMessageAllocateBuffer = 0x100U;
    constexpr std::uint32_t kFormatMessageFromSystem = 0x1000U;
    constexpr std::uint32_t kFormatMessageIgnoreInserts = 0x200U;
    std::uint16_t* buffer = nullptr;
    const std::uint32_t written =
        tl_FormatMessageW(kFormatMessageAllocateBuffer | kFormatMessageFromSystem |
                              kFormatMessageIgnoreInserts,
                          nullptr, abi::kErrorFileNotFound, 0,
                          reinterpret_cast<std::uint16_t*>(&buffer), 0, nullptr);
    ASSERT_GT(written, 0U);
    ASSERT_NE(buffer, nullptr);
    const std::u16string expected = u"The system cannot find the file specified.";
    EXPECT_EQ(std::u16string(reinterpret_cast<const char16_t*>(buffer)), expected);
    tl_LocalFree(buffer);
}

TEST(Win32WideTest, FormatMessageWWithGuestBufferHonorsSize) {
    constexpr std::uint32_t kFormatMessageFromSystem = 0x1000U;
    std::uint16_t small[4]{};
    EXPECT_EQ(tl_FormatMessageW(kFormatMessageFromSystem, nullptr, abi::kErrorFileNotFound, 0,
                                small, 4, nullptr),
              0U);
    std::uint16_t big[64]{};
    const std::uint32_t written =
        tl_FormatMessageW(kFormatMessageFromSystem, nullptr, abi::kErrorAccessDenied, 0, big,
                          sizeof(big) / sizeof(big[0]), nullptr);
    ASSERT_GT(written, 0U);
    EXPECT_EQ(std::u16string(reinterpret_cast<const char16_t*>(big)), u"Access is denied.");
}

TEST(Win32WideTest, GetConsoleOutputCPReturnsUtf8) {
    EXPECT_EQ(tl_GetConsoleOutputCP(), 65001U);
    EXPECT_NE(tl_SetConsoleOutputCP(65001U), 0);
}

TEST(Win32WideTest, LocalFreeReturnsNull) {
    void* memory = std::malloc(16);
    ASSERT_NE(memory, nullptr);
    EXPECT_EQ(tl_LocalFree(memory), nullptr);
}

TEST(Win32WideTest, GetTempFileNameWCreatesFileAndReturnsName) {
    const char* tmpdir = "_tl_test_tmpw";
    mkdir(tmpdir, 0777);
    std::uint16_t dir[260]{};
    std::uint16_t prefix[260]{};
    std::uint16_t out[260]{};
    const std::string dir_str = tmpdir;
    for (std::size_t i = 0; i < dir_str.size(); ++i) {
        dir[i] = static_cast<std::uint16_t>(static_cast<unsigned char>(dir_str[i]));
    }
    const std::string prefix_str = "d2utmp";
    for (std::size_t i = 0; i < prefix_str.size(); ++i) {
        prefix[i] = static_cast<std::uint16_t>(static_cast<unsigned char>(prefix_str[i]));
    }
    const std::uint32_t ret = tl_GetTempFileNameW(dir, prefix, 0, out);
    ASSERT_NE(ret, 0U);
    std::string name;
    for (int i = 0; i < 260 && out[i] != 0; ++i) {
        name.push_back(static_cast<char>(out[i]));
    }
    EXPECT_EQ(name.compare(0, dir_str.size() + 1, dir_str + "/"), 0);
    struct stat st{};
    EXPECT_EQ(stat(name.c_str(), &st), 0);
    EXPECT_NE(std::remove(name.c_str()), -1);
    rmdir(tmpdir);
}

TEST(Win32WideTest, CommandLineToArgvWParsesQuotedArguments) {
    const std::uint16_t cmd[] = {L'"', L'/', L'p', L'a', L't', L'h', L'/', L'a', L'p', L'p',
                                 L'.', L'e', L'x', L'e', L'"', L' ', L'f', L'i', L'l', L'e',
                                 L'1', L'.', L't', L'x', L't', L' ', L'-', L'k', L' ', L'"',
                                 L'a', L'r', L'g', L' ', L'w', L'i', L't', L'h', L' ', L's',
                                 L'p', L'a', L'c', L'e', L'"', 0};
    int argc = 0;
    std::uint16_t** argv = tl_CommandLineToArgvW(cmd, &argc);
    ASSERT_NE(argv, nullptr);
    EXPECT_EQ(argc, 4);
    EXPECT_EQ(std::u16string(reinterpret_cast<const char16_t*>(argv[0])), u"/path/app.exe");
    EXPECT_EQ(std::u16string(reinterpret_cast<const char16_t*>(argv[1])), u"file1.txt");
    EXPECT_EQ(std::u16string(reinterpret_cast<const char16_t*>(argv[2])), u"-k");
    EXPECT_EQ(std::u16string(reinterpret_cast<const char16_t*>(argv[3])), u"arg with space");
    EXPECT_EQ(argv[4], nullptr);
    tl_LocalFree(argv);
}

TEST(Win32WideTest, CommandLineToArgvWFailsOnNullArguments) {
    EXPECT_EQ(tl_CommandLineToArgvW(nullptr, nullptr), nullptr);
}

}  // namespace
}  // namespace tradutorlinux
