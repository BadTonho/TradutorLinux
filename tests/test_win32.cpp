#include "tradutorlinux/runtime/winapi.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

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

}  // namespace
}  // namespace tradutorlinux
