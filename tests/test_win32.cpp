#include "tradutorlinux/runtime/winapi.hpp"

#include <array>
#include <cstdint>
#include <string>
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

}  // namespace
}  // namespace tradutorlinux
