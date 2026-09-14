#include "test_win32_common.hpp"
#include "tradutorlinux/diagnostics/trace.hpp"
#include "../src/runtime/core/environment_internal.hpp"
#include "../src/runtime/core/runtime_handle_state.hpp"
#include "../src/runtime/core/runtime_thread_state.hpp"

namespace tradutorlinux {
namespace {

std::atomic<std::uint32_t> g_gs_failure_entry_calls{0};

TL_MSABI std::uint32_t gs_failure_entry(const void*) noexcept {
    g_gs_failure_entry_calls.fetch_add(1, std::memory_order_relaxed);
    return 91U;
}

bool fail_guest_gs_base(const void*) noexcept {
    return false;
}

struct GuestGsFailureScope final {
    GuestGsFailureScope() noexcept {
        set_guest_image_view(
            reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(&gs_failure_entry)),
            1, 0, 0);
        set_guest_gs_base_test_hook(&fail_guest_gs_base);
    }

    ~GuestGsFailureScope() {
        set_guest_gs_base_test_hook(nullptr);
        set_guest_image_view(nullptr, 0, 0, 0);
    }
};

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

TEST(Win32CodePageTest, Cp437MapsGlyphCharactersWithUseGlyphChars) {
    const char input[] = {1, 2, 0x1E, 0x1F};
    std::uint16_t output[4]{};
    const int written = tl_MultiByteToWideChar(
        abi::kCp437, abi::kMbUseGlyphChars | abi::kMbErrInvalidChars, input, 4, output, 4);
    ASSERT_EQ(written, 4);
    EXPECT_EQ(output[0], 0x263A);
    EXPECT_EQ(output[1], 0x263B);
    EXPECT_EQ(output[2], 0x25B2);
    EXPECT_EQ(output[3], 0x25BC);
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

TEST(Win32VirtualTest, QueryTracksReserveCommitAndProtection) {
    void* memory = tl_VirtualAlloc(nullptr, 0x2000, abi::kMemReserve,
                                   abi::kPageReadWrite);
    ASSERT_NE(memory, nullptr);

    GuestMemoryBasicInformation info{};
    ASSERT_EQ(tl_VirtualQuery(memory, &info, sizeof(info)), sizeof(info));
    EXPECT_EQ(info.base_address, memory);
    EXPECT_EQ(info.allocation_base, memory);
    EXPECT_EQ(info.allocation_protect, abi::kPageReadWrite);
    EXPECT_EQ(info.region_size, 0x2000U);
    EXPECT_EQ(info.state, abi::kMemReserve);
    EXPECT_EQ(info.protect, 0U);
    EXPECT_EQ(info.type, 0x20000U);

    ASSERT_EQ(tl_VirtualAlloc(memory, 0x2000, abi::kMemCommit,
                              abi::kPageReadWrite), memory);
    ASSERT_EQ(tl_VirtualQuery(memory, &info, sizeof(info)), sizeof(info));
    EXPECT_EQ(info.state, abi::kMemCommit);
    EXPECT_EQ(info.protect, abi::kPageReadWrite);

    std::uint32_t old_protection = 0;
    ASSERT_EQ(tl_VirtualProtect(memory, 0x1000, abi::kPageReadOnly,
                                &old_protection), 1);
    EXPECT_EQ(old_protection, abi::kPageReadWrite);
    ASSERT_EQ(tl_VirtualQuery(memory, &info, sizeof(info)), sizeof(info));
    EXPECT_EQ(info.allocation_protect, abi::kPageReadWrite);
    EXPECT_EQ(info.protect, abi::kPageReadOnly);

    ASSERT_EQ(tl_VirtualFree(memory, 0, abi::kMemRelease), 1);
    ASSERT_EQ(tl_VirtualQuery(memory, &info, sizeof(info)), sizeof(info));
    EXPECT_EQ(info.base_address, memory);
    EXPECT_EQ(info.allocation_base, nullptr);
    EXPECT_EQ(info.allocation_protect, 0U);
    EXPECT_EQ(info.region_size, 0x2000U);
    EXPECT_EQ(info.state, abi::kMemFree);
    EXPECT_EQ(info.protect, 0U);
    EXPECT_EQ(info.type, 0U);
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

TEST(Win32VirtualTest, ProtectedMemoryOutputsRejectUnmappedPointers) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
    EXPECT_EQ(tl_GlobalMemoryStatusEx(invalid), 0);
    EXPECT_EQ(tl_GetPhysicallyInstalledSystemMemory(
                  reinterpret_cast<std::uint64_t*>(invalid)),
              0);
    tl_GlobalMemoryStatus(invalid);

    void* const memory = tl_VirtualAlloc(nullptr, 0x1000,
                                         abi::kMemCommit | abi::kMemReserve,
                                         abi::kPageReadWrite);
    ASSERT_NE(memory, nullptr);
    EXPECT_EQ(tl_VirtualQuery(memory, invalid, sizeof(GuestMemoryBasicInformation)), 0U);
    EXPECT_EQ(tl_VirtualQueryEx(nullptr, memory, invalid,
                                sizeof(GuestMemoryBasicInformation)), 0U);
    EXPECT_EQ(tl_VirtualProtect(memory, 0x1000, abi::kPageReadOnly,
                                reinterpret_cast<std::uint32_t*>(invalid)),
              0);
    std::uint32_t old_protection = 0;
    EXPECT_EQ(tl_VirtualProtect(memory, 0x1000, abi::kPageReadWrite,
                                &old_protection), 1);
    EXPECT_EQ(tl_VirtualFree(memory, 0, abi::kMemRelease), 1);
}

TEST(Win32FileTest, ProtectedDirectoryChangeAndFlushInputsRejectUnmappedPointers) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    std::uint32_t bytes_returned = 123;
    EXPECT_EQ(tl_ReadDirectoryChangesW(nullptr, nullptr, 0, 0, 0,
                                       &bytes_returned, nullptr, nullptr), 1);
    EXPECT_EQ(bytes_returned, 0U);
    EXPECT_EQ(tl_ReadDirectoryChangesW(nullptr, nullptr, 0, 0, 0,
                                       static_cast<std::uint32_t*>(invalid), nullptr, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_FlushViewOfFile(invalid, 4096), 0);

    void* memory = tl_VirtualAlloc(nullptr, 4096, abi::kMemCommit | abi::kMemReserve,
                                   abi::kPageReadWrite);
    ASSERT_NE(memory, nullptr);
    EXPECT_EQ(tl_FlushViewOfFile(memory, 4096), 1);
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

TEST(Win32ConsoleTest, ProtectedConsoleOutputsRejectUnmappedPointers) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));

    EXPECT_EQ(tl_GetConsoleScreenBufferInfo(nullptr, invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_PeekNamedPipe(nullptr, nullptr, 0, static_cast<std::uint32_t*>(invalid),
                               static_cast<std::uint32_t*>(invalid),
                               static_cast<std::uint32_t*>(invalid)), 1);
    EXPECT_EQ(tl_ReadConsoleA(nullptr, nullptr, 0, static_cast<std::uint32_t*>(invalid), nullptr), 1);
    EXPECT_EQ(tl_CreatePipe(static_cast<void**>(invalid), static_cast<void**>(invalid), nullptr, 0), 1);
    EXPECT_EQ(tl_GetCommState(nullptr, invalid), 1);
    EXPECT_EQ(tl_GetOverlappedResult(nullptr, nullptr, static_cast<std::uint32_t*>(invalid), 0), 1);
    tl_OutputDebugStringA(reinterpret_cast<const char*>(invalid));
    tl_OutputDebugStringW(reinterpret_cast<const std::uint16_t*>(invalid));
}

TEST(Win32ConsoleTest, IsDBCSLeadByteExAlwaysReturnsFalse) {
    EXPECT_EQ(tl_IsDBCSLeadByteEx(abi::kCp1252, 0x81), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
}

TEST(Win32UserMiscTest, ProtectedCursorScrollAndIconOutputsRejectUnmappedPointers) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));

    EXPECT_EQ(tl_GetCursorPos(invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetCaretPos(invalid), 1);
    EXPECT_EQ(tl_GetScrollRange(nullptr, 0, static_cast<int*>(invalid),
                                static_cast<int*>(invalid)), 1);
    EXPECT_EQ(tl_GetIconInfo(nullptr, invalid), 1);
    EXPECT_EQ(tl_GetIconInfoExW(nullptr, invalid), 1);
    EXPECT_EQ(tl_GetUpdateRect(nullptr, invalid, 0), 1);
    EXPECT_EQ(tl_GetUserObjectInformationW(nullptr, 0, invalid, sizeof(std::uint32_t),
                                            static_cast<std::uint32_t*>(invalid)), 1);
    EXPECT_EQ(tl_GetMonitorInfoW(nullptr, invalid), 1);
    EXPECT_EQ(tl_GetComboBoxInfo(nullptr, invalid), 1);
}

TEST(Win32ProcessConsoleTest, StandardHandlesStartupAndSystemDirectoryShareContext) {
    void* const input = tl_GetStdHandle(abi::kStdInputHandle);
    void* const output = tl_GetStdHandle(abi::kStdOutputHandle);
    void* const error = tl_GetStdHandle(abi::kStdErrorHandle);
    ASSERT_NE(input, nullptr);
    ASSERT_NE(output, nullptr);
    ASSERT_NE(error, nullptr);

    EXPECT_EQ(tl_SetStdHandle(abi::kStdOutputHandle, error), 1);
    EXPECT_EQ(tl_GetStdHandle(abi::kStdOutputHandle), error);
    abi::GuestStartupInfoW startup{};
    tl_GetStartupInfoW(&startup);
    EXPECT_EQ(startup.cb, sizeof(startup));
    EXPECT_EQ(startup.flags, abi::kStartfUseStdHandles);
    EXPECT_EQ(startup.std_input, input);
    EXPECT_EQ(startup.std_output, error);
    EXPECT_EQ(startup.std_error, error);
    EXPECT_EQ(tl_SetStdHandle(abi::kStdOutputHandle, output), 1);

    std::uint16_t system_directory[32]{};
    EXPECT_EQ(tl_GetSystemDirectoryW(nullptr, 0), 20U);
    EXPECT_EQ(tl_GetSystemDirectoryW(system_directory, std::size(system_directory)), 19U);
    EXPECT_EQ(std::u16string_view(reinterpret_cast<char16_t*>(system_directory)),
              u"C:\\Windows\\System32");
    EXPECT_EQ(tl_GetSystemDirectoryW(system_directory, 4), 20U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInsufficientBuffer);
}

TEST(Win32ProcessConsoleTest, FileTypeAndWideConsoleRejectInvalidHandles) {
    void* const output = tl_GetStdHandle(abi::kStdOutputHandle);
    const std::uint32_t type = tl_GetFileType(output);
    EXPECT_TRUE(type == abi::kFileTypeDisk || type == abi::kFileTypeChar ||
                type == abi::kFileTypePipe);
    EXPECT_EQ(tl_GetFileType(nullptr), abi::kFileTypeUnknown);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidHandle);

    std::uint16_t buffer[4]{};
    std::uint32_t transferred = 99;
    EXPECT_EQ(tl_ReadConsoleW(output, buffer, std::size(buffer), &transferred, nullptr), 0);
    EXPECT_EQ(transferred, 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidHandle);
    EXPECT_EQ(tl_WriteConsoleW(tl_GetStdHandle(abi::kStdInputHandle), buffer,
                               std::size(buffer), &transferred, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidHandle);
}

TEST(Win32ProcessConsoleTest, PointerEncodingProcessorFeaturesAndSListAreDeterministic) {
    void* const original = reinterpret_cast<void*>(0x12345678ULL);
    void* const encoded = tl_EncodePointer(original);
    EXPECT_NE(encoded, original);
    EXPECT_EQ(tl_DecodePointer(encoded), original);
    EXPECT_EQ(tl_DecodePointer(tl_EncodePointer(nullptr)), nullptr);

    EXPECT_EQ(tl_IsDebuggerPresent(), 0);
    EXPECT_EQ(tl_IsProcessorFeaturePresent(abi::kPfXmmi64InstructionsAvailable), 1);
    EXPECT_EQ(tl_IsProcessorFeaturePresent(0xFFFFFFFFU), 0);

    abi::GuestSListHeader header{~0ULL, ~0ULL};
    tl_InitializeSListHead(&header);
    EXPECT_EQ(header.alignment, 0U);
    EXPECT_EQ(header.region, 0U);
    alignas(16) std::array<std::byte, 32> unaligned_storage{};
    tl_InitializeSListHead(reinterpret_cast<abi::GuestSListHeader*>(
        unaligned_storage.data() + 1));
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
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

TEST(Win32CriticalSectionTest, ExtendedInitializersHonorFlagsAndLifecycle) {
    alignas(8) std::array<std::byte, 64> first{};
    alignas(8) std::array<std::byte, 64> second{};
    EXPECT_EQ(tl_InitializeCriticalSectionAndSpinCount(first.data(), 4000), 1);
    tl_EnterCriticalSection(first.data());
    tl_LeaveCriticalSection(first.data());
    tl_DeleteCriticalSection(first.data());

    EXPECT_EQ(tl_InitializeCriticalSectionEx(second.data(), 4000,
                                              abi::kCriticalSectionNoDebugInfo), 1);
    tl_EnterCriticalSection(second.data());
    tl_LeaveCriticalSection(second.data());
    tl_DeleteCriticalSection(second.data());
}

TEST(Win32CriticalSectionTest, ExtendedInitializersRejectInvalidArguments) {
    EXPECT_EQ(tl_InitializeCriticalSectionAndSpinCount(nullptr, 0), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_InitializeCriticalSectionEx(nullptr, 0, 0), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    alignas(8) std::array<std::byte, 64> storage{};
    EXPECT_EQ(tl_InitializeCriticalSectionEx(storage.data(), 0, 1), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32CodePageTest, AreFileApisANSIUsesTheFixedAnsiCodePage) {
    EXPECT_EQ(tl_AreFileApisANSI(), 1);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
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

TEST(Win32ModuleTest, ProtectedModuleNamesAndOutputsRejectUnmappedPointers) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    auto* const invalid_string = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x100000U));
    const std::uint16_t wide_name[] = {'k', 'e', 'r', 'n', 'e', 'l', '3', '2', '.', 'd', 'l', 'l', 0};
    void* module = nullptr;

    EXPECT_EQ(tl_GetModuleHandleA(reinterpret_cast<const char*>(invalid)), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetModuleHandleW(reinterpret_cast<const std::uint16_t*>(invalid)), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_GetModuleHandleExA(0, "kernel32.dll", static_cast<void**>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetModuleHandleExW(0, wide_name, static_cast<void**>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    ASSERT_EQ(tl_GetModuleHandleExA(0, "kernel32.dll", &module), 1);
    ASSERT_NE(module, nullptr);

    EXPECT_EQ(tl_LoadLibraryA(reinterpret_cast<const char*>(invalid)), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_LoadLibraryW(reinterpret_cast<const std::uint16_t*>(invalid)), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetProcAddress(module, reinterpret_cast<const char*>(invalid_string)), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32ModuleTest, GetProcAddressReturnsNullForStub) {
    void* handle = tl_GetModuleHandleA("kernel32.dll");
    EXPECT_EQ(tl_GetProcAddress(handle, "SomeFunction"), nullptr);
}

TEST(Win32ToolhelpTest, ProtectedProcessEntriesRejectUnmappedPointers) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    void* const snapshot = tl_CreateToolhelp32Snapshot(abi::kTh32csSnapProcess, 0);
    ASSERT_NE(snapshot, reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1)));

    EXPECT_EQ(tl_Process32FirstW(snapshot, invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_Process32NextW(snapshot, invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    abi::GuestProcessEntry32W entry{};
    entry.dwSize = sizeof(entry);
    EXPECT_EQ(tl_Process32FirstW(snapshot, &entry), 1);
    EXPECT_EQ(entry.dwSize, sizeof(entry));
    EXPECT_NE(entry.th32ProcessID, 0U);
    EXPECT_EQ(tl_CloseHandle(snapshot), 1);
}

TEST(Win32ProcessTest, ProtectedProcessOutputsRejectUnmappedPointers) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    std::uint32_t size = 4096;
    std::uint32_t returned_length = 64;

    tl_GetStartupInfoA(invalid);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    tl_GetStartupInfoW(reinterpret_cast<abi::GuestStartupInfoW*>(invalid));
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_QueryFullProcessImageNameW(
                  nullptr, 0, reinterpret_cast<std::uint16_t*>(invalid), &size), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_QueryFullProcessImageNameW(
                  nullptr, 0, nullptr, reinterpret_cast<std::uint32_t*>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_GetProcessAffinityMask(nullptr,
                                        reinterpret_cast<std::uintptr_t*>(invalid), nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetProcessTimes(nullptr, invalid, nullptr, nullptr, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_K32GetProcessMemoryInfo(nullptr, invalid, 64), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_GetLogicalProcessorInformation(invalid, &returned_length), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetLogicalProcessorInformation(nullptr,
                                                reinterpret_cast<std::uint32_t*>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_IsNetworkAlive(reinterpret_cast<std::uint32_t*>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
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
    std::array<char, 4096> buffer{};
    const std::uint32_t needed = tl_GetEnvironmentVariableA("PATH", nullptr, 0);
    EXPECT_GT(needed, 0U);
    ASSERT_LT(needed, buffer.size());

    const std::uint32_t written = tl_GetEnvironmentVariableA("PATH", buffer.data(),
                                                               static_cast<std::uint32_t>(buffer.size()));
    // Sucesso retorna o número de caracteres copiados, sem o terminador.
    EXPECT_EQ(written, needed - 1);
    EXPECT_GT(std::strlen(buffer.data()), 0U);
}

TEST(Win32EnvTest, GetEnvironmentVariableAMissingReturnsZero) {
    EXPECT_EQ(tl_GetEnvironmentVariableA("TL_NONEXISTENT_VAR_12345", nullptr, 0), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorEnvvarNotFound);
}

TEST(Win32EnvTest, ProtectedEnvironmentInputsAndOutputsRejectUnmappedPointers) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    const std::uint16_t wide_name[] = {'P', 'A', 'T', 'H', 0};
    const std::uint16_t wide_value[] = {'x', 0};
    const std::uint16_t expansion[] = {'%', 'P', 'A', 'T', 'H', '%', 0};

    EXPECT_EQ(tl_GetEnvironmentVariableA(reinterpret_cast<const char*>(invalid), nullptr, 0), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetEnvironmentVariableA("PATH", static_cast<char*>(invalid), 4096), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_GetEnvironmentVariableW(reinterpret_cast<const std::uint16_t*>(invalid),
                                         nullptr, 0), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetEnvironmentVariableW(wide_name, static_cast<std::uint16_t*>(invalid),
                                         4096), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_SetEnvironmentVariableW(reinterpret_cast<const std::uint16_t*>(invalid),
                                         wide_value), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_SetEnvironmentVariableW(wide_name,
                                         reinterpret_cast<const std::uint16_t*>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_ExpandEnvironmentStringsW(reinterpret_cast<const std::uint16_t*>(invalid),
                                           nullptr, 0), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_ExpandEnvironmentStringsW(expansion, static_cast<std::uint16_t*>(invalid),
                                           4096), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32EnvTest, GetEnvironmentVariableAInsufficientBuffer) {
    std::array<char, 2> tiny{};
    const std::uint32_t needed = tl_GetEnvironmentVariableA("PATH", nullptr, 0);
    if (needed > 0) {
        const std::uint32_t result = tl_GetEnvironmentVariableA("PATH", tiny.data(), 2);
        EXPECT_EQ(result, needed);
        EXPECT_EQ(tl_GetLastError(), abi::kErrorInsufficientBuffer);
    }
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

TEST(Win32RegistryTest, WideQueryConvertsDefaultProgramFilesValueToUtf16) {
    const void* local_machine = reinterpret_cast<const void*>(0x80000002U);
    const std::u16string subkey_text = u"Software\\Microsoft\\Windows\\CurrentVersion";
    const std::u16string value_name_text = u"ProgramFilesDir";
    void* key = nullptr;
    ASSERT_EQ(tl_RegOpenKeyExW(
                  local_machine,
                  reinterpret_cast<const std::uint16_t*>(subkey_text.c_str()), 0, 0, &key),
              abi::kErrorSuccess);

    std::uint32_t type = 0;
    std::uint32_t byte_count = 0;
    ASSERT_EQ(tl_RegQueryValueExW(
                  key, reinterpret_cast<const std::uint16_t*>(value_name_text.c_str()), nullptr,
                  &type, nullptr, &byte_count),
              abi::kErrorSuccess);
    EXPECT_EQ(type, 1U);
    EXPECT_EQ(byte_count, (std::u16string{u"C:\\Program Files"}.size() + 1U) * 2U);

    std::vector<unsigned char> wide_data(byte_count);
    ASSERT_EQ(tl_RegQueryValueExW(
                  key, reinterpret_cast<const std::uint16_t*>(value_name_text.c_str()), nullptr,
                  nullptr, wide_data.data(), &byte_count),
              abi::kErrorSuccess);
    std::u16string decoded;
    for (std::size_t index = 0; index + 1U < wide_data.size(); index += 2U) {
        const auto unit = static_cast<char16_t>(
            static_cast<unsigned int>(wide_data[index]) |
            (static_cast<unsigned int>(wide_data[index + 1U]) << 8U));
        if (unit == 0) {
            break;
        }
        decoded.push_back(unit);
    }
    EXPECT_EQ(decoded, u"C:\\Program Files");
    EXPECT_EQ(tl_RegCloseKey(key), abi::kErrorSuccess);
}

TEST(Win32RegistryTest, WideSetAndAnsiQueryUseTheSameStringValue) {
    const void* current_user = reinterpret_cast<const void*>(0x80000001U);
    const std::u16string subkey_text = u"Software\\TradutorLinux\\RegistryTest";
    const std::u16string value_name_text = u"WideValue";
    const std::u16string value_text = u"C:\\Program Files\\WinRAR";
    void* key = nullptr;
    ASSERT_EQ(tl_RegCreateKeyExW(
                  current_user,
                  reinterpret_cast<const std::uint16_t*>(subkey_text.c_str()), 0, nullptr, 0, 0,
                  nullptr, &key, nullptr),
              abi::kErrorSuccess);
    ASSERT_EQ(tl_RegSetValueExW(
                  key, reinterpret_cast<const std::uint16_t*>(value_name_text.c_str()), 0, 1,
                  reinterpret_cast<const unsigned char*>(value_text.c_str()),
                  static_cast<std::uint32_t>((value_text.size() + 1U) * sizeof(char16_t))),
              abi::kErrorSuccess);

    const char value_name[] = "WideValue";
    std::array<unsigned char, 64> ansi{};
    std::uint32_t byte_count = static_cast<std::uint32_t>(ansi.size());
    ASSERT_EQ(tl_RegQueryValueExA(key, value_name, nullptr, nullptr, ansi.data(), &byte_count),
              abi::kErrorSuccess);
    EXPECT_STREQ(reinterpret_cast<const char*>(ansi.data()), "C:\\Program Files\\WinRAR");
    EXPECT_EQ(tl_RegDeleteValueW(
                  key, reinterpret_cast<const std::uint16_t*>(value_name_text.c_str())),
              abi::kErrorSuccess);
    EXPECT_EQ(tl_RegCloseKey(key), abi::kErrorSuccess);
}

TEST(Win32RegistryTest, ProtectedRegistryInputsAndOutputsRejectUnmappedPointers) {
    const void* current_user = reinterpret_cast<const void*>(0x80000001U);
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    const char subkey[] = "Software\\TradutorLinux\\ProtectedRegistry";
    const char value_name[] = "Payload";
    void* key = nullptr;

    EXPECT_EQ(tl_RegOpenKeyExA(current_user, subkey, 0, 0, static_cast<void**>(invalid)),
              abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_RegOpenKeyExA(current_user, static_cast<const char*>(invalid), 0, 0, &key),
              abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_RegCreateKeyExW(current_user, reinterpret_cast<const std::uint16_t*>(u"ProtectedRegistry"),
                                 0, nullptr, 0, 0, invalid, &key, nullptr),
              abi::kErrorInvalidParameter);
    ASSERT_EQ(tl_RegCreateKeyExA(current_user, subkey, 0, nullptr, 0, 0, nullptr, &key, nullptr),
              abi::kErrorSuccess);

    const unsigned char payload[] = {'o', 'k', 0};
    EXPECT_EQ(tl_RegSetValueExA(key, value_name, 0, 1,
                                static_cast<const unsigned char*>(invalid), sizeof(payload)),
              abi::kErrorInvalidParameter);
    ASSERT_EQ(tl_RegSetValueExA(key, value_name, 0, 1, payload, sizeof(payload)),
              abi::kErrorSuccess);

    std::array<unsigned char, 16> output{};
    std::uint32_t output_size = static_cast<std::uint32_t>(output.size());
    EXPECT_EQ(tl_RegQueryValueExA(key, value_name, nullptr, nullptr, output.data(),
                                  static_cast<std::uint32_t*>(invalid)),
              abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_RegQueryValueExA(key, value_name, nullptr, nullptr,
                                  static_cast<unsigned char*>(invalid), &output_size),
              abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_RegQueryValueExA(key, static_cast<const char*>(invalid), nullptr, nullptr,
                                  output.data(), &output_size),
              abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_RegDeleteValueA(key, static_cast<const char*>(invalid)),
              abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_RegDeleteValueA(key, value_name), abi::kErrorSuccess);
    EXPECT_EQ(tl_RegCloseKey(key), abi::kErrorSuccess);
}

TEST(Win32CryptoTest, ProtectedCryptoBuffersRejectUnmappedPointers) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    void* provider = nullptr;
    EXPECT_EQ(tl_CryptAcquireContextA(static_cast<void**>(invalid), nullptr, nullptr, 0, 0), 0);
    ASSERT_EQ(tl_CryptAcquireContextA(&provider, nullptr, nullptr, 0, 0), 1);

    std::array<std::uint8_t, 32> random_bytes{};
    EXPECT_EQ(tl_CryptGenRandom(provider, static_cast<std::uint32_t>(random_bytes.size()),
                                static_cast<std::uint8_t*>(invalid)),
              0);
    ASSERT_EQ(tl_CryptGenRandom(provider, static_cast<std::uint32_t>(random_bytes.size()),
                                random_bytes.data()),
              1);

    std::uintptr_t hash = 0;
    EXPECT_EQ(tl_CryptCreateHash(0, 0x8004, 0, 0,
                                 static_cast<std::uintptr_t*>(invalid)), 0);
    ASSERT_EQ(tl_CryptCreateHash(0, 0x8004, 0, 0, &hash), 1);

    std::uint32_t length = 32;
    EXPECT_EQ(tl_CryptGetHashParam(hash, 2, static_cast<std::uint8_t*>(invalid), &length, 0), 0);
    EXPECT_EQ(tl_CryptGetHashParam(hash, 2, random_bytes.data(),
                                   static_cast<std::uint32_t*>(invalid), 0), 0);
    ASSERT_EQ(tl_CryptGetHashParam(hash, 2, random_bytes.data(), &length, 0), 1);
    EXPECT_EQ(length, 32U);

    std::uint32_t signature_length = 256;
    EXPECT_EQ(tl_CryptSignHashW(hash, 0, nullptr, 0,
                                static_cast<std::uint8_t*>(invalid), &signature_length), 0);
    EXPECT_EQ(tl_CryptSignHashW(hash, 0, nullptr, 0, random_bytes.data(),
                                static_cast<std::uint32_t*>(invalid)), 0);

    std::array<std::uint8_t, 64> key_blob{};
    std::uint32_t blob_length = static_cast<std::uint32_t>(key_blob.size());
    EXPECT_EQ(tl_CryptExportKey(0, 0, 0, 0, static_cast<std::uint8_t*>(invalid), &blob_length), 0);
    EXPECT_EQ(tl_CryptExportKey(0, 0, 0, 0, key_blob.data(),
                                static_cast<std::uint32_t*>(invalid)), 0);
    ASSERT_EQ(tl_CryptExportKey(0, 0, 0, 0, key_blob.data(), &blob_length), 1);

    std::uintptr_t user_key = 0;
    EXPECT_EQ(tl_CryptGetUserKey(0, 0, static_cast<std::uintptr_t*>(invalid)), 0);
    ASSERT_EQ(tl_CryptGetUserKey(0, 0, &user_key), 1);

    std::uint32_t provider_length = 16;
    EXPECT_EQ(tl_CryptGetProvParam(0, 0, static_cast<std::uint8_t*>(invalid), &provider_length, 0), 0);
    EXPECT_EQ(tl_CryptGetProvParam(0, 0, random_bytes.data(),
                                   static_cast<std::uint32_t*>(invalid), 0), 0);
    ASSERT_EQ(tl_CryptGetProvParam(0, 0, random_bytes.data(), &provider_length, 0), 1);

    EXPECT_EQ(tl_SystemFunction036(invalid, 16), 0);
    ASSERT_EQ(tl_SystemFunction036(random_bytes.data(),
                                   static_cast<std::uint32_t>(random_bytes.size())), 1);
}

TEST(Win32AdvapiTest, ProtectedIdentityAndRegistryQueryOutputsRejectUnmappedPointers) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));

    EXPECT_EQ(tl_LookupPrivilegeValueW(nullptr, nullptr, invalid), 0);
    std::array<std::uint32_t, 2> luid{};
    ASSERT_EQ(tl_LookupPrivilegeValueW(nullptr, nullptr, luid.data()), 1);

    EXPECT_EQ(tl_AdjustTokenPrivileges(nullptr, 0, nullptr, 0, nullptr,
                                       static_cast<std::uint32_t*>(invalid)), 0);
    std::uint32_t return_length = 1;
    ASSERT_EQ(tl_AdjustTokenPrivileges(nullptr, 0, nullptr, 0, nullptr, &return_length), 1);
    EXPECT_EQ(return_length, 0U);

    EXPECT_EQ(tl_GetFileSecurityW(nullptr, 0, nullptr, 0,
                                  static_cast<std::uint32_t*>(invalid)), 0);
    std::uint32_t length_needed = 1;
    ASSERT_EQ(tl_GetFileSecurityW(nullptr, 0, nullptr, 0, &length_needed), 1);
    EXPECT_EQ(length_needed, 0U);

    std::uint32_t user_size = 6;
    EXPECT_EQ(tl_GetUserNameA(static_cast<char*>(invalid), &user_size), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetUserNameA(nullptr, static_cast<std::uint32_t*>(invalid)), 0);
    std::array<char, 6> user_name{};
    ASSERT_EQ(tl_GetUserNameA(user_name.data(), &user_size), 1);
    EXPECT_STREQ(user_name.data(), "Tonho");

    std::array<std::uint8_t, 28> sid{};
    std::array<std::uint16_t, 10> domain{};
    std::uint32_t sid_size = static_cast<std::uint32_t>(sid.size());
    std::uint32_t domain_size = static_cast<std::uint32_t>(domain.size());
    std::uint32_t sid_name_use = 0;
    EXPECT_EQ(tl_LookupAccountNameW(nullptr, nullptr, sid.data(),
                                     static_cast<std::uint32_t*>(invalid), domain.data(),
                                     &domain_size, &sid_name_use), 0);
    ASSERT_EQ(tl_LookupAccountNameW(nullptr, nullptr, sid.data(), &sid_size, domain.data(),
                                    &domain_size, &sid_name_use), 1);
    EXPECT_EQ(sid_size, 28U);
    EXPECT_EQ(domain_size, 10U);
    EXPECT_EQ(sid_name_use, 1U);
    EXPECT_EQ(tl_LookupAccountNameW(nullptr, nullptr, invalid, &sid_size, domain.data(),
                                    &domain_size, &sid_name_use), 0);

    EXPECT_NE(tl_LsaOpenPolicy(nullptr, nullptr, 0, static_cast<void**>(invalid)), 0);
    void* policy = nullptr;
    ASSERT_EQ(tl_LsaOpenPolicy(nullptr, nullptr, 0, &policy), 0);
    EXPECT_EQ(tl_LsaClose(policy), 0);

    EXPECT_EQ(tl_RegQueryInfoKeyA(nullptr, nullptr, nullptr, nullptr,
                                  static_cast<std::uint32_t*>(invalid), nullptr, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, nullptr),
              abi::kErrorInvalidParameter);
    std::uint32_t sub_keys = 1;
    ASSERT_EQ(tl_RegQueryInfoKeyW(nullptr, nullptr, nullptr, nullptr, &sub_keys, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
              abi::kErrorSuccess);
    EXPECT_EQ(sub_keys, 0U);
}

TEST(Win32AdvapiTest, ProtectedIsTextUnicodeBuffersRejectUnmappedPointers) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    const std::array<std::uint8_t, 2> unicode_prefix{0xFF, 0xFE};
    int result = -1;

    EXPECT_EQ(tl_IsTextUnicode(invalid, static_cast<int>(unicode_prefix.size()), &result), 0);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(tl_IsTextUnicode(unicode_prefix.data(), static_cast<int>(unicode_prefix.size()),
                               static_cast<int*>(invalid)),
              0);
    ASSERT_EQ(tl_IsTextUnicode(unicode_prefix.data(),
                               static_cast<int>(unicode_prefix.size()), &result), 1);
    EXPECT_EQ(result, 1);
}

TEST(Win32EnvTest, GetEnvironmentVariableWConvertsResult) {
    const std::uint16_t name[] = {'P', 'A', 'T', 'H', 0};
    const std::uint32_t needed = tl_GetEnvironmentVariableW(name, nullptr, 0);
    EXPECT_GT(needed, 0U);
}

TEST(Win32EnvTest, MutableWideEnvironmentDoesNotMutateHostAndExpandsCaseInsensitively) {
    const std::uint16_t name[] = {'t', 'l', '_', 'e', 'n', 'v', '_', 'u', 'n', 'i', 't', 0};
    const std::uint16_t upper_name[] = {'T', 'L', '_', 'E', 'N', 'V', '_', 'U', 'N', 'I', 'T', 0};
    const std::uint16_t value[] = {'v', 'a', 'l', 'u', 'e', 0};
    const std::uint16_t expansion[] = {'%', 'T', 'L', '_', 'E', 'N', 'V', '_', 'U', 'N', 'I', 'T', '%', '!', 0};
    const char* const host_before = std::getenv("TL_ENV_UNIT");
    const std::string saved_host = host_before != nullptr ? host_before : "";
    const bool host_was_defined = host_before != nullptr;

    ASSERT_EQ(tl_SetEnvironmentVariableW(name, value), 1);
    std::uint16_t result[32]{};
    EXPECT_EQ(tl_GetEnvironmentVariableW(upper_name, result, std::size(result)), 5U);
    EXPECT_EQ(result[0], 'v');
    EXPECT_EQ(result[4], 'e');
    EXPECT_EQ(result[5], 0);
    EXPECT_EQ(tl_ExpandEnvironmentStringsW(expansion, result, std::size(result)), 7U);
    EXPECT_EQ(result[0], 'v');
    EXPECT_EQ(result[5], '!');
    EXPECT_EQ(result[6], 0);
    const char* const host_after = std::getenv("TL_ENV_UNIT");
    EXPECT_EQ(host_after != nullptr, host_was_defined);
    if (host_was_defined) {
        EXPECT_STREQ(host_after, saved_host.c_str());
    }

    EXPECT_EQ(tl_SetEnvironmentVariableW(name, nullptr), 1);
    EXPECT_EQ(tl_GetEnvironmentVariableW(name, nullptr, 0), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorEnvvarNotFound);
}

TEST(Win32EnvTest, EnvironmentBlockIsSortedAndCanOnlyBeFreedOnce) {
    const std::uint16_t name[] = {'T', 'L', '_', 'B', 'L', 'O', 'C', 'K', 0};
    const std::uint16_t value[] = {'x', 0};
    ASSERT_EQ(tl_SetEnvironmentVariableW(name, value), 1);
    std::uint16_t* const block = tl_GetEnvironmentStringsW();
    ASSERT_NE(block, nullptr);
    bool found = false;
    for (const std::uint16_t* entry = block; *entry != 0;) {
        if (entry[0] == 'T' && entry[1] == 'L' && entry[2] == '_' && entry[3] == 'B') {
            found = true;
        }
        while (*entry++ != 0) {}
    }
    EXPECT_TRUE(found);
    EXPECT_EQ(tl_FreeEnvironmentStringsW(block), 1);
    EXPECT_EQ(tl_FreeEnvironmentStringsW(block), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_SetEnvironmentVariableW(name, nullptr), 1);
}

TEST(Win32EnvTest, EnvironmentBlockSizeRejectsCheckedOverflow) {
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    std::size_t units = 0;
    const std::array<std::size_t, 1> impossible_length{maximum};
    EXPECT_FALSE(runtime::checked_environment_block_units(impossible_length, units));

    const std::array<std::size_t, 2> overflowing_sum{maximum / 2U, maximum / 2U};
    EXPECT_FALSE(runtime::checked_environment_block_units(overflowing_sum, units));

    const std::array<std::size_t, 1> overflowing_allocation{maximum / sizeof(std::uint16_t)};
    EXPECT_FALSE(runtime::checked_environment_block_units(overflowing_allocation, units));

    const std::array<std::size_t, 2> valid_lengths{1U, 2U};
    ASSERT_TRUE(runtime::checked_environment_block_units(valid_lengths, units));
    EXPECT_EQ(units, 6U);
}

TEST(Win32LocaleTest, FixedCodePagesAndCp437RoundTrip) {
    EXPECT_EQ(tl_GetACP(), abi::kCp1252);
    EXPECT_EQ(tl_GetOEMCP(), abi::kCp437);
    abi::GuestCpInfo cpinfo{};
    ASSERT_EQ(tl_GetCPInfo(abi::kCp437, &cpinfo), 1);
    EXPECT_EQ(cpinfo.max_char_size, 1U);
    ASSERT_EQ(tl_GetCPInfo(abi::kCpUtf8, &cpinfo), 1);
    EXPECT_EQ(cpinfo.max_char_size, 4U);
    const char cp437[] = {static_cast<char>(0x82), 0};
    std::uint16_t wide[2]{};
    ASSERT_EQ(tl_MultiByteToWideChar(abi::kCp437, 0, cp437, -1, wide, 2), 2);
    EXPECT_EQ(wide[0], 0x00E9U);
    char back[2]{};
    ASSERT_EQ(tl_WideCharToMultiByte(abi::kCp437, 0, wide, -1, back, 2, nullptr, nullptr), 2);
    EXPECT_EQ(static_cast<unsigned char>(back[0]), 0x82U);
}

TEST(Win32LocaleTest, LocaleInfoAndCaseMappingValidateBuffersAndFlags) {
    std::uint16_t value[32]{};
    EXPECT_EQ(tl_GetLocaleInfoW(abi::kLocaleEnglishUnitedStates, abi::kLocaleIDefaultCodePage,
                                value, std::size(value)), 5);
    EXPECT_EQ(value[0], '1');
    EXPECT_EQ(value[3], '2');
    const std::uint16_t source[] = {'A', 0x00C9, 0};
    std::uint16_t mapped[3]{};
    EXPECT_EQ(tl_LCMapStringW(abi::kLocaleEnglishUnitedStates, abi::kLcmapsLowercase,
                               source, -1, mapped, 3), 3);
    EXPECT_EQ(mapped[0], 'a');
    EXPECT_EQ(mapped[1], 0x00E9U);
    EXPECT_EQ(tl_LCMapStringW(abi::kLocaleEnglishUnitedStates, 0x10U,
                               source, -1, mapped, 3), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidFlags);
    EXPECT_EQ(tl_LCMapStringW(abi::kLocaleEnglishUnitedStates, abi::kLcmapsUppercase,
                               source, -1, mapped, 2), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInsufficientBuffer);
}

TEST(Win32LocaleTest, ExtendedLocaleValidatesNamesCodePagesAndCharacterTypes) {
    const std::uint16_t en_us[] = {'e', 'n', '-', 'U', 'S', 0};
    std::uint16_t country[32]{};
    EXPECT_EQ(tl_IsValidCodePage(abi::kCp1252), 1);
    EXPECT_EQ(tl_IsValidCodePage(932), 0);
    EXPECT_EQ(tl_IsValidLocale(abi::kLocaleEnglishUnitedStates, abi::kLcidSupported), 1);
    EXPECT_EQ(tl_IsValidLocale(0x0416U, abi::kLcidSupported), 0);
    EXPECT_EQ(tl_IsValidLocale(abi::kLocaleEnglishUnitedStates, 0), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidFlags);
    ASSERT_EQ(tl_GetLocaleInfoEx(en_us, abi::kLocaleSCountry, country, std::size(country)), 14);
    EXPECT_EQ(std::u16string_view(reinterpret_cast<char16_t*>(country)), u"United States");

    const std::uint16_t source[] = {'A', '7', ' ', 0x00E9U, 0};
    std::uint16_t types[5]{};
    ASSERT_EQ(tl_GetStringTypeW(abi::kCType1, source, -1, types), 1);
    EXPECT_EQ(types[0], abi::kC1Upper | abi::kC1Alpha | abi::kC1Xdigit);
    EXPECT_EQ(types[1], abi::kC1Digit | abi::kC1Xdigit);
    EXPECT_EQ(types[2], abi::kC1Space | abi::kC1Blank);
    EXPECT_EQ(types[3], abi::kC1Lower | abi::kC1Alpha);
    EXPECT_EQ(tl_GetStringTypeW(2, source, -1, types), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidFlags);
    EXPECT_EQ(tl_EnumSystemLocalesW(0, abi::kLcidSupported), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_EnumSystemLocalesW(0, 0), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidFlags);
}

TEST(Win32LocaleTest, ExtendedLocaleFormatsStaticEnUsDateAndTime) {
    const abi::GuestSystemTime date{2024, 1, 2, 2, 15, 4, 5, 0};
    std::uint16_t formatted[64]{};
    ASSERT_EQ(tl_GetDateFormatW(abi::kLocaleEnglishUnitedStates, 0, &date, nullptr,
                                formatted, std::size(formatted)), 9);
    EXPECT_EQ(std::u16string_view(reinterpret_cast<char16_t*>(formatted)), u"1/2/2024");
    ASSERT_EQ(tl_GetDateFormatW(abi::kLocaleEnglishUnitedStates, abi::kDateLongDate, &date,
                                nullptr, formatted, std::size(formatted)), 25);
    EXPECT_EQ(std::u16string_view(reinterpret_cast<char16_t*>(formatted)), u"Tuesday, January 2, 2024");
    ASSERT_EQ(tl_GetTimeFormatW(abi::kLocaleEnglishUnitedStates, 0, &date, nullptr,
                                formatted, std::size(formatted)), 11);
    EXPECT_EQ(std::u16string_view(reinterpret_cast<char16_t*>(formatted)), u"3:04:05 PM");
    ASSERT_EQ(tl_GetTimeFormatW(abi::kLocaleEnglishUnitedStates,
                                abi::kTimeNoSeconds | abi::kTimeNoTimeMarker,
                                &date, nullptr, formatted, std::size(formatted)), 6);
    EXPECT_EQ(std::u16string_view(reinterpret_cast<char16_t*>(formatted)), u"15:04");
    EXPECT_EQ(tl_GetDateFormatW(abi::kLocaleEnglishUnitedStates, 0x100U, &date, nullptr,
                                formatted, std::size(formatted)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidFlags);
    EXPECT_EQ(tl_GetTimeFormatW(abi::kLocaleEnglishUnitedStates, 0, &date, nullptr,
                                formatted, 2), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInsufficientBuffer);
}

TEST(Win32LocaleTest, ProtectedSystemAndMessageBuffersRejectUnmappedPointers) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    EXPECT_EQ(tl_GetVersionExA(invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetVersionExW(invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_VerifyVersionInfoW(invalid, 1, 0), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_GetUserDefaultLocaleName(static_cast<std::uint16_t*>(invalid), 6), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_FormatMessageA(abi::kFormatMessageFromSystem, nullptr,
                                abi::kErrorFileNotFound, 0, static_cast<char*>(invalid), 64,
                                nullptr), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_FormatMessageW(abi::kFormatMessageFromSystem, nullptr,
                                abi::kErrorFileNotFound, 0,
                                static_cast<std::uint16_t*>(invalid), 64, nullptr), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    tl_GetSystemInfo(invalid);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    std::uint32_t size = 64;
    EXPECT_EQ(tl_GetComputerNameA(static_cast<char*>(invalid), &size), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    size = 64;
    EXPECT_EQ(tl_GetComputerNameW(static_cast<std::uint16_t*>(invalid), &size), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32LocaleTest, ProtectedConversionAndFormattingBuffersRejectUnmappedPointers) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    const char narrow[] = "123";
    const std::uint16_t wide[] = {'1', '2', '3', 0};
    std::uint16_t wide_output[32]{};
    char narrow_output[32]{};

    EXPECT_EQ(tl_MultiByteToWideChar(abi::kCp1252, 0, static_cast<const char*>(invalid), -1,
                                     wide_output, std::size(wide_output)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_MultiByteToWideChar(abi::kCp1252, 0, narrow, -1,
                                     static_cast<std::uint16_t*>(invalid), std::size(wide_output)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_WideCharToMultiByte(abi::kCp1252, 0, static_cast<const std::uint16_t*>(invalid), -1,
                                     narrow_output, std::size(narrow_output), nullptr, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_WideCharToMultiByte(abi::kCp1252, 0, wide, -1,
                                     static_cast<char*>(invalid), std::size(narrow_output), nullptr, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_GetCPInfo(abi::kCp1252, static_cast<abi::GuestCpInfo*>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetLocaleInfoW(abi::kLocaleEnglishUnitedStates, abi::kLocaleSCountry,
                                static_cast<std::uint16_t*>(invalid), 32), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetStringTypeW(abi::kCType1, static_cast<const std::uint16_t*>(invalid), -1,
                                wide_output), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetStringTypeW(abi::kCType1, wide, -1,
                                static_cast<std::uint16_t*>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_FoldStringW(abi::kLcmapsLowercase, static_cast<const std::uint16_t*>(invalid), -1,
                             wide_output, std::size(wide_output)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_FoldStringW(abi::kLcmapsLowercase, wide, -1,
                             static_cast<std::uint16_t*>(invalid), std::size(wide_output)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetNumberFormatW(abi::kLocaleEnglishUnitedStates, 0,
                                  static_cast<const std::uint16_t*>(invalid), nullptr,
                                  wide_output, std::size(wide_output)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetNumberFormatW(abi::kLocaleEnglishUnitedStates, 0, wide, nullptr,
                                  static_cast<std::uint16_t*>(invalid), std::size(wide_output)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_GetSystemDirectoryW(static_cast<std::uint16_t*>(invalid), 32), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetSystemDirectoryA(static_cast<char*>(invalid), 32), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    const abi::GuestSystemTime date{2024, 1, 2, 2, 15, 4, 5, 0};
    EXPECT_EQ(tl_GetDateFormatW(abi::kLocaleEnglishUnitedStates, 0,
                                static_cast<const abi::GuestSystemTime*>(invalid), nullptr,
                                wide_output, std::size(wide_output)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetDateFormatW(abi::kLocaleEnglishUnitedStates, 0, &date, nullptr,
                                static_cast<std::uint16_t*>(invalid), std::size(wide_output)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetTimeFormatW(abi::kLocaleEnglishUnitedStates, 0,
                                static_cast<const abi::GuestSystemTime*>(invalid), nullptr,
                                wide_output, std::size(wide_output)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetTimeFormatW(abi::kLocaleEnglishUnitedStates, 0, &date, nullptr,
                                static_cast<std::uint16_t*>(invalid), std::size(wide_output)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
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

TEST(Win32GlobalMemoryTest, MoveableZeroInitializedBlockLocksAndFrees) {
    void* const handle = tl_GlobalAlloc(abi::kGmemMoveable | abi::kGmemZeroinit, 32);
    ASSERT_NE(handle, nullptr);
    auto* const block = static_cast<unsigned char*>(tl_GlobalLock(handle));
    ASSERT_NE(block, nullptr);
    for (std::size_t index = 0; index < 32; ++index) {
        EXPECT_EQ(block[index], 0U);
    }
    block[0] = 0x5A;
    EXPECT_EQ(tl_GlobalLock(handle), block);
    EXPECT_EQ(tl_GlobalUnlock(handle), 1);
    EXPECT_EQ(tl_GlobalUnlock(handle), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
    EXPECT_EQ(tl_GlobalFree(handle), nullptr);
}

TEST(Win32GlobalMemoryTest, RejectsInvalidHandleAndFlags) {
    EXPECT_EQ(tl_GlobalAlloc(0x8000U, 8), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GlobalLock(reinterpret_cast<void*>(0x1234)), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidHandle);
    EXPECT_EQ(tl_GlobalFree(reinterpret_cast<void*>(0x1234)), reinterpret_cast<void*>(0x1234));
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidHandle);
}

TEST(Win32GlobalMemoryTest, LocalAllocUsesLocalFree) {
    void* const memory = tl_LocalAlloc(abi::kGmemZeroinit, 16);
    ASSERT_NE(memory, nullptr);
    const auto* const bytes = static_cast<const unsigned char*>(memory);
    for (std::size_t index = 0; index < 16; ++index) {
        EXPECT_EQ(bytes[index], 0U);
    }
    EXPECT_EQ(tl_LocalFree(memory), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
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

TEST(Win32TimeTest, GetSystemTimeAsFileTimeRejectsInvalidDestination) {
    tl_GetSystemTimeAsFileTime(reinterpret_cast<void*>(0x1U));
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32TimeTest, ProtectedTimeBuffersRejectUnmappedPointers) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    std::uint64_t file_time = 0;
    abi::GuestSystemTime system_time{};
    std::uint16_t fat_date = 0;
    std::uint16_t fat_time = 0;

    EXPECT_EQ(tl_QueryPerformanceCounter(static_cast<std::int64_t*>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_QueryPerformanceFrequency(static_cast<std::int64_t*>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    tl_GetSystemTime(invalid);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    tl_GetLocalTime(invalid);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetTimeZoneInformation(invalid), 0xFFFFFFFFU);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_FileTimeToSystemTime(invalid, &system_time), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_FileTimeToSystemTime(&file_time, invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_SystemTimeToFileTime(invalid, &file_time), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_SystemTimeToFileTime(&system_time, invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_FileTimeToLocalFileTime(invalid, &file_time), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_FileTimeToLocalFileTime(&file_time, invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_SystemTimeToTzSpecificLocalTime(nullptr, invalid, &system_time), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_SystemTimeToTzSpecificLocalTime(nullptr, &system_time, invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_TzSpecificLocalTimeToSystemTime(nullptr, invalid, &system_time), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_TzSpecificLocalTimeToSystemTime(nullptr, &system_time, invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_FileTimeToDosDateTime(invalid, &fat_date, &fat_time), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_FileTimeToDosDateTime(&file_time, static_cast<std::uint16_t*>(invalid),
                                       &fat_time), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_DosDateTimeToFileTime(0x5821U, 0, invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_LocalFileTimeToFileTime(invalid, &file_time), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_LocalFileTimeToFileTime(&file_time, invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_CompareFileTime(invalid, &file_time), 0);
}

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

TEST(Win32FileTest, PrivateProfileStringsUseProtectedGuestBuffers) {
    char narrow[8]{};
    EXPECT_EQ(tl_GetPrivateProfileStringA(nullptr, nullptr, "fallback", narrow,
                                          sizeof(narrow), nullptr), 7U);
    EXPECT_STREQ(narrow, "fallbac");

    constexpr std::uint16_t kFallback[] = {'w', 'i', 'd', 'e', 0};
    std::uint16_t wide[8]{};
    EXPECT_EQ(tl_GetPrivateProfileStringW(nullptr, nullptr, kFallback, wide,
                                          std::size(wide), nullptr), 4U);
    EXPECT_EQ(wide[0], static_cast<std::uint16_t>('w'));
    EXPECT_EQ(wide[4], 0U);

    auto* const invalid_narrow =
        reinterpret_cast<char*>(static_cast<std::uintptr_t>(0x1000U));
    auto* const invalid_wide =
        reinterpret_cast<std::uint16_t*>(static_cast<std::uintptr_t>(0x1000U));
    EXPECT_EQ(tl_GetPrivateProfileStringA(nullptr, nullptr, "x", invalid_narrow, 2U, nullptr),
              0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetPrivateProfileStringW(nullptr, nullptr, kFallback, invalid_wide, 2U, nullptr),
              0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_GetPrivateProfileSectionA(nullptr, invalid_narrow, 2U, nullptr), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
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

TEST(Win32FileTest, ReadFileAdvancesCurrentPosition) {
    TempDirFixture ctx;
    const std::string fpath = ctx.path("read_current_test.bin");
    FILE* f = std::fopen(fpath.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    const char data[] = "0123456789";
    std::fwrite(data, 1, sizeof(data) - 1, f);
    std::fclose(f);

    void* const handle = tl_CreateFileA(fpath.c_str(), abi::kGenericRead, 0, nullptr,
                                        abi::kOpenExisting, 0, nullptr);
    ASSERT_NE(handle, nullptr);
    char first[4]{};
    std::uint32_t bytes_read = 0;
    ASSERT_EQ(tl_ReadFile(handle, first, sizeof(first), &bytes_read, nullptr), 1);
    ASSERT_EQ(bytes_read, sizeof(first));

    const std::int32_t position = tl_SetFilePointer(handle, 0, nullptr, 1);
    EXPECT_EQ(position, 4);
    char second[4]{};
    ASSERT_EQ(tl_ReadFile(handle, second, sizeof(second), &bytes_read, nullptr), 1);
    EXPECT_EQ(std::string(second, sizeof(second)), "4567");
    tl_CloseHandle(handle);
}

TEST(Win32FileTest, WriteFileAdvancesCurrentPosition) {
    TempDirFixture ctx;
    const std::string fpath = ctx.path("write_current_test.bin");
    void* const handle = tl_CreateFileA(fpath.c_str(), abi::kGenericRead | abi::kGenericWrite,
                                        0, nullptr, abi::kCreateAlways, 0, nullptr);
    ASSERT_NE(handle, nullptr);
    const char data[] = "payload";
    std::uint32_t bytes_written = 0;
    ASSERT_EQ(tl_WriteFile(handle, data, sizeof(data) - 1, &bytes_written, nullptr), 1);
    ASSERT_EQ(bytes_written, sizeof(data) - 1);

    std::int64_t position = -1;
    ASSERT_EQ(tl_SetFilePointerEx(handle, 0, &position, 1), 1);
    EXPECT_EQ(position, static_cast<std::int64_t>(sizeof(data) - 1));
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

TEST(Win32FileTest, ProtectedIoOutputsRejectUnmappedPointers) {
    TempDirFixture ctx;
    const std::string path = ctx.path("protected-io.tmp");
    FILE* file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    std::fwrite("data", 1, 4, file);
    std::fclose(file);
    void* handle = tl_CreateFileA(path.c_str(), abi::kGenericRead | abi::kGenericWrite, 0,
                                  nullptr, abi::kOpenExisting, 0, nullptr);
    ASSERT_NE(handle, nullptr);
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    auto* const invalid_handle = reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1));
    char buffer[4]{};

    EXPECT_EQ(tl_CreateFileA(reinterpret_cast<const char*>(invalid), abi::kGenericRead, 0,
                             nullptr, abi::kOpenExisting, 0, nullptr),
              invalid_handle);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_CreateFileW(reinterpret_cast<const std::uint16_t*>(invalid), abi::kGenericRead,
                             0, nullptr, abi::kOpenExisting, 0, nullptr),
              invalid_handle);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_OpenFile(reinterpret_cast<const char*>(invalid), nullptr, 0), -1);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_WriteFile(handle, buffer, sizeof(buffer),
                           static_cast<std::uint32_t*>(invalid), nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_ReadFile(handle, buffer, sizeof(buffer),
                          static_cast<std::uint32_t*>(invalid), nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_SetFilePointer(handle, 0, static_cast<std::int32_t*>(invalid), 0), -1);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_SetFilePointerEx(handle, 0, static_cast<std::int64_t*>(invalid), 0), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_DeviceIoControl(nullptr, 0, nullptr, 0, nullptr, 0,
                                 static_cast<std::uint32_t*>(invalid), nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_DuplicateHandle(nullptr, handle, nullptr,
                                 reinterpret_cast<void**>(invalid), 0, 0, 0), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_CloseHandle(handle), 1);
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

TEST(Win32FileMetadataTest, Amd64LayoutsMatchWindows) {
    EXPECT_EQ(sizeof(abi::GuestFileBasicInfo), 40U);
    EXPECT_EQ(sizeof(abi::GuestFileDispositionInfo), 1U);
    EXPECT_EQ(sizeof(abi::GuestFileDispositionInfoEx), 4U);
}

TEST(Win32FileMetadataTest, ProtectedMetadataBuffersRejectUnmappedPointers) {
    TempDirFixture ctx;
    const std::string path = ctx.path("protected-metadata.tmp");
    FILE* file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    std::fwrite("data", 1, 4, file);
    std::fclose(file);
    const std::uint16_t wide_path[] = {
        '_', 't', 'l', '_', 't', 'e', 's', 't', '/',
        'p', 'r', 'o', 't', 'e', 'c', 't', 'e', 'd', '-',
        'm', 'e', 't', 'a', 'd', 'a', 't', 'a', '.', 't', 'm', 'p', 0};
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));

    EXPECT_EQ(tl_GetVolumeInformationA(nullptr, static_cast<char*>(invalid), 32,
                                       nullptr, nullptr, nullptr, nullptr, 0), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetVolumeInformationW(nullptr, static_cast<std::uint16_t*>(invalid), 32,
                                       nullptr, nullptr, nullptr, nullptr, 0), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetFileAttributesExW(wide_path, 0, invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    void* handle = tl_CreateFileA(path.c_str(), abi::kGenericRead | abi::kGenericWrite, 0,
                                  nullptr, abi::kOpenExisting, 0, nullptr);
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(tl_GetFileTime(handle, invalid, nullptr, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_SetFileTime(handle, invalid, nullptr, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetFileInformationByHandle(handle, invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetFileInformationByHandleEx(handle, 0, invalid, 40), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    abi::GuestFileBasicInfo basic{};
    EXPECT_EQ(tl_SetFileInformationByHandle(handle, abi::kFileBasicInfo, invalid,
                                            sizeof(basic)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_CloseHandle(handle), 1);
}

TEST(Win32FileMetadataTest, FindFirstFileExWMatchesCaseWildcardsAndFillsMetadata) {
    TempDirFixture ctx;
    const std::string first = ctx.path("Case_A.TXT");
    const std::string second = ctx.path("case_b.txt");
    for (const std::string* path : {&first, &second}) {
        FILE* file = std::fopen(path->c_str(), "wb");
        ASSERT_NE(file, nullptr);
        std::fwrite("data", 1, 4, file);
        std::fclose(file);
    }
    const std::u16string pattern_text = u"_tl_test/case_?.txt";
    std::vector<std::uint16_t> pattern(pattern_text.begin(), pattern_text.end());
    pattern.push_back(0);
    alignas(8) std::array<std::byte, 592> data{};
    void* find = tl_FindFirstFileExW(pattern.data(), abi::kFindExInfoStandard, data.data(),
                                     abi::kFindExSearchNameMatch, nullptr, 0);
    ASSERT_NE(find, reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max()));
    int count = 0;
    do {
        const auto* words = reinterpret_cast<const std::uint32_t*>(data.data());
        EXPECT_EQ(words[7], 0U);
        EXPECT_EQ(words[8], 4U);
        EXPECT_NE(words[6], 0U);
        ++count;
    } while (tl_FindNextFileW(find, data.data()) != 0);
    EXPECT_EQ(count, 2);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNoMoreFiles);
    EXPECT_EQ(tl_FindClose(find), 1);

    find = tl_FindFirstFileExW(pattern.data(), abi::kFindExInfoBasic, data.data(),
                               abi::kFindExSearchNameMatch, nullptr,
                               abi::kFindFirstExLargeFetch);
    ASSERT_NE(find, reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max()));
    EXPECT_EQ(tl_FindClose(find), 1);
}

TEST(Win32FileMetadataTest, FindFirstFileExWRejectsUnsupportedParameters) {
    const std::uint16_t pattern[] = {'*', 0};
    alignas(8) std::array<std::byte, 592> data{};
    const void* invalid = reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    EXPECT_EQ(tl_FindFirstFileExW(pattern, 2, data.data(), 0, nullptr, 0), invalid);
    EXPECT_EQ(tl_FindFirstFileExW(pattern, 0, data.data(), 1, nullptr, 0), invalid);
    EXPECT_EQ(tl_FindFirstFileExW(pattern, 0, data.data(), 0, data.data(), 0), invalid);
    EXPECT_EQ(tl_FindFirstFileExW(pattern, 0, data.data(), 0, nullptr, 1), invalid);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32FileMetadataTest, SetFileAttributesWControlsReadonlyAndValidatesBits) {
    TempDirFixture ctx;
    const std::string path = ctx.path("attributes.txt");
    FILE* file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    std::fclose(file);
    const std::uint16_t wide[] = {'_', 't', 'l', '_', 't', 'e', 's', 't', '/',
                                  'a', 't', 't', 'r', 'i', 'b', 'u', 't', 'e', 's', '.',
                                  't', 'x', 't', 0};
    ASSERT_EQ(tl_SetFileAttributesW(
                  wide, abi::kFileAttributeReadOnly | abi::kFileAttributeArchive),
              1);
    EXPECT_NE(tl_GetFileAttributesW(wide) & abi::kFileAttributeReadOnly, 0U);
    ASSERT_EQ(tl_SetFileAttributesW(wide, abi::kFileAttributeNormal), 1);
    EXPECT_EQ(tl_GetFileAttributesW(wide) & abi::kFileAttributeReadOnly, 0U);
    ASSERT_EQ(tl_SetFileAttributesW(wide, 0), 1);
    EXPECT_EQ(tl_GetFileAttributesW(wide) & abi::kFileAttributeReadOnly, 0U);
    ASSERT_EQ(tl_SetFileAttributesW(
                  wide, abi::kFileAttributeArchive | abi::kFileAttributeNotContentIndexed),
              1);
    EXPECT_NE(tl_GetFileAttributesW(wide) & abi::kFileAttributeArchive, 0U);
    EXPECT_EQ(tl_SetFileAttributesW(wide, 0x2U), 0);
    EXPECT_EQ(tl_SetFileAttributesW(
                  wide, abi::kFileAttributeNormal | abi::kFileAttributeArchive),
              0);
}

TEST(Win32FileMetadataTest, SetFileInformationSupportsBasicAndDispositionClasses) {
    TempDirFixture ctx;
    const std::string basic_path = ctx.path("basic.tmp");
    const std::string close_path = ctx.path("close.tmp");
    const std::string posix_path = ctx.path("posix.tmp");
    auto open_file = [](const std::string& path) {
        return tl_CreateFileA(path.c_str(), abi::kGenericRead | abi::kGenericWrite, 0,
                              nullptr, abi::kCreateAlways, 0, nullptr);
    };
    void* basic_handle = open_file(basic_path);
    ASSERT_NE(basic_handle, nullptr);
    abi::GuestFileBasicInfo basic{};
    basic.file_attributes = abi::kFileAttributeReadOnly | abi::kFileAttributeArchive;
    EXPECT_EQ(tl_SetFileInformationByHandle(basic_handle, abi::kFileBasicInfo, &basic,
                                            sizeof(basic)),
              1);
    struct stat st{};
    ASSERT_EQ(::stat(basic_path.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & S_IWUSR, 0U);
    basic.file_attributes = abi::kFileAttributeNormal;
    EXPECT_EQ(tl_SetFileInformationByHandle(basic_handle, abi::kFileBasicInfo, &basic,
                                            sizeof(basic)),
              1);
    EXPECT_EQ(tl_SetFileInformationByHandle(basic_handle, 99, &basic, sizeof(basic)), 0);
    EXPECT_EQ(tl_SetFileInformationByHandle(basic_handle, abi::kFileBasicInfo, &basic, 1), 0);
    EXPECT_EQ(tl_CloseHandle(basic_handle), 1);

    void* close_handle = open_file(close_path);
    ASSERT_NE(close_handle, nullptr);
    abi::GuestFileDispositionInfo disposition{1};
    ASSERT_EQ(tl_SetFileInformationByHandle(close_handle, abi::kFileDispositionInfo,
                                            &disposition, sizeof(disposition)),
              1);
    disposition.delete_file = 0;
    ASSERT_EQ(tl_SetFileInformationByHandle(close_handle, abi::kFileDispositionInfo,
                                            &disposition, sizeof(disposition)),
              1);
    disposition.delete_file = 1;
    ASSERT_EQ(tl_SetFileInformationByHandle(close_handle, abi::kFileDispositionInfo,
                                            &disposition, sizeof(disposition)),
              1);
    EXPECT_EQ(tl_CloseHandle(close_handle), 1);
    EXPECT_NE(::access(close_path.c_str(), F_OK), 0);

    void* posix_handle = open_file(posix_path);
    ASSERT_NE(posix_handle, nullptr);
    abi::GuestFileDispositionInfoEx extended{0x4U};
    EXPECT_EQ(tl_SetFileInformationByHandle(posix_handle, abi::kFileDispositionInfoEx,
                                            &extended, sizeof(extended)),
              0);
    extended.flags = abi::kFileDispositionFlagDelete |
                     abi::kFileDispositionFlagPosixSemantics;
    ASSERT_EQ(tl_SetFileInformationByHandle(posix_handle, abi::kFileDispositionInfoEx,
                                            &extended, sizeof(extended)),
              1);
    EXPECT_NE(::access(posix_path.c_str(), F_OK), 0);
    EXPECT_EQ(tl_CloseHandle(posix_handle), 1);
    EXPECT_EQ(tl_SetFileInformationByHandle(nullptr, abi::kFileDispositionInfoEx,
                                            &extended, sizeof(extended)),
              0);
}

TEST(Win32FileMetadataTest, PrefixesKeepIdenticalLogicalPathsIsolated) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("tl-file-prefix-" + std::to_string(static_cast<unsigned long long>(::getpid())));
    const std::filesystem::path first = root / "first";
    const std::filesystem::path second = root / "second";
    std::filesystem::create_directories(first / "drive_c");
    std::filesystem::create_directories(second / "drive_c");
    const std::uint16_t logical[] = {'C', ':', '\\', 's', 'a', 'm', 'e', '.', 't', 'x', 't', 0};

    set_guest_prefix_path(first);
    void* handle = tl_CreateFileW(logical, abi::kGenericWrite, 0, nullptr,
                                  abi::kCreateAlways, 0, nullptr);
    ASSERT_NE(handle, nullptr);
    ASSERT_EQ(tl_CloseHandle(handle), 1);
    EXPECT_TRUE(std::filesystem::exists(first / "drive_c" / "same.txt"));
    EXPECT_FALSE(std::filesystem::exists(second / "drive_c" / "same.txt"));

    set_guest_prefix_path(second);
    EXPECT_EQ(tl_GetFileAttributesW(logical), 0xFFFFFFFFU);
    handle = tl_CreateFileW(logical, abi::kGenericWrite, 0, nullptr,
                            abi::kCreateAlways, 0, nullptr);
    ASSERT_NE(handle, nullptr);
    ASSERT_EQ(tl_CloseHandle(handle), 1);
    EXPECT_TRUE(std::filesystem::exists(second / "drive_c" / "same.txt"));

    set_guest_prefix_path({});
    std::filesystem::remove_all(root);
}

TEST(PrefixTest, RejectsPathsThatEscapeDriveC) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("tl-prefix-traversal-" + std::to_string(static_cast<unsigned long long>(::getpid())));
    ASSERT_TRUE(prefix::initialize_prefix(root));

    EXPECT_TRUE(prefix::resolve_windows_path("C:\\inside\\file.txt", root).empty() == false);
    EXPECT_TRUE(prefix::resolve_windows_path("C:\\..\\outside.txt", root).empty());
    EXPECT_TRUE(prefix::resolve_windows_path("C:\\Program Files\\..\\..\\outside.txt", root).empty());

    std::filesystem::remove_all(root);
}

TEST(PrefixTest, ResolvesExtendedLengthDrivePathsInsideDriveC) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("tl-prefix-extended-" + std::to_string(static_cast<unsigned long long>(::getpid())));
    ASSERT_TRUE(prefix::initialize_prefix(root));

    const auto resolved = prefix::resolve_windows_path(
        R"(\\?\C:\Program Files\fixture.exe)", root);
    ASSERT_FALSE(resolved.empty());
    EXPECT_TRUE(prefix::is_path_within(resolved, prefix::get_environment_paths(root).drive_c));
    EXPECT_EQ(std::filesystem::weakly_canonical(resolved),
              std::filesystem::weakly_canonical(root / "drive_c" / "Program Files" / "fixture.exe"));
    EXPECT_TRUE(prefix::resolve_windows_path(R"(\\?\C:\..\outside.txt)", root).empty());
    EXPECT_TRUE(prefix::resolve_windows_path(R"(\\?\UNC\server\share\file.txt)", root).empty());

    std::filesystem::remove_all(root);
}

TEST(PrefixTest, DriveLinksAreExplicitAndKeepCDriveConfined) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("tl-prefix-drives-" + std::to_string(static_cast<unsigned long long>(::getpid())));
    ASSERT_TRUE(prefix::initialize_prefix(root));
    const prefix::EnvironmentPaths paths = prefix::get_environment_paths(root);
    const std::filesystem::path c_link = paths.dosdevices_dir / "c:";
    const std::filesystem::path z_link = paths.dosdevices_dir / "z:";

    ASSERT_TRUE(std::filesystem::is_symlink(c_link));
    ASSERT_TRUE(std::filesystem::is_symlink(z_link));
    EXPECT_EQ(std::filesystem::read_symlink(c_link), std::filesystem::path("../drive_c"));
    EXPECT_EQ(std::filesystem::read_symlink(z_link), std::filesystem::path("/"));

    const std::filesystem::path c_inside =
        prefix::resolve_windows_path("C:\\Program Files\\fixture.exe", root);
    ASSERT_FALSE(c_inside.empty());
    EXPECT_TRUE(prefix::is_path_within(c_inside, paths.drive_c));
    EXPECT_TRUE(prefix::resolve_windows_path("C:\\..\\outside.txt", root).empty());

    const std::filesystem::path z_external =
        prefix::resolve_windows_path("Z:\\tmp\\fixture.txt", root);
    ASSERT_FALSE(z_external.empty());
    EXPECT_EQ(std::filesystem::weakly_canonical(z_external),
              std::filesystem::weakly_canonical(std::filesystem::path("/tmp/fixture.txt")));
    EXPECT_FALSE(prefix::is_path_within(z_external, paths.drive_c));

    std::filesystem::remove_all(root);
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

TEST(Win32DirTest, ProtectedPathOutputsRejectUnmappedPointers) {
    auto* const invalid_narrow = reinterpret_cast<char*>(static_cast<std::uintptr_t>(1));
    auto* const invalid_wide = reinterpret_cast<std::uint16_t*>(static_cast<std::uintptr_t>(1));
    auto* const invalid_u32 = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(1));

    EXPECT_EQ(tl_GetCurrentDirectoryA(4096, invalid_narrow), 0U);
    EXPECT_EQ(tl_GetCurrentDirectoryW(4096, invalid_wide), 0U);
    set_guest_module_path("protected/path.exe");
    EXPECT_EQ(tl_GetModuleFileNameA(nullptr, invalid_narrow, 4096), 0U);
    EXPECT_EQ(tl_GetModuleFileNameW(nullptr, invalid_wide, 4096), 0U);
    set_guest_module_path(nullptr);
    EXPECT_EQ(tl_GetFullPathNameA("relative.txt", 4096, invalid_narrow, nullptr), 0U);
    EXPECT_EQ(tl_GetTempPathA(64, invalid_narrow), 0U);
    EXPECT_EQ(tl_GetTempPathW(64, invalid_wide), 0U);
    EXPECT_EQ(tl_GetDiskFreeSpaceA(nullptr, invalid_u32, nullptr, nullptr, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32DirTest, GetModuleFileNameAReturnsSetPath) {
    set_guest_module_path("test/path/app.exe");
    char buf[4096]{};
    const std::uint32_t len = tl_GetModuleFileNameA(nullptr, buf, sizeof(buf));
    EXPECT_GT(len, 0U);
    const std::string logical_path{buf};
    EXPECT_TRUE(logical_path.starts_with("Z:\\"));
    EXPECT_TRUE(logical_path.ends_with("\\test\\path\\app.exe"));
    set_guest_module_path(nullptr);
}

TEST(Win32DirTest, GetModuleFileNameAReturnsNeededWhenBufferTooSmall) {
    set_guest_module_path("long/path.exe");
    const std::uint32_t needed = tl_GetModuleFileNameA(nullptr, nullptr, 0);
    EXPECT_GT(needed, 0U);
    set_guest_module_path(nullptr);
}

TEST(Win32DirTest, GetModuleFileNameReturnsErrorWithoutGuestModule) {
    set_guest_module_path(nullptr);
    char narrow[8]{};
    std::uint16_t wide[8]{};

    EXPECT_EQ(tl_GetModuleFileNameA(nullptr, narrow, sizeof(narrow)), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetModuleFileNameW(nullptr, wide, 8), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
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

TEST(Win32ConcurrencyTest, PointerBackedTlsSlotIsZeroInitializedAndReleased) {
    std::array<std::byte, 8> raw_template{};
    raw_template[0] = std::byte{0x5A};
    runtime::GuestTeb teb{};
    runtime::initialize_guest_teb(&teb, &g_guest_peb, 0x2000U, 0x1000U, 1);
    EXPECT_EQ(teb.stack_base, 0x2000U);
    EXPECT_EQ(teb.stack_limit, 0x1000U);

    set_guest_tls_directory(
        reinterpret_cast<std::uintptr_t>(raw_template.data()),
        reinterpret_cast<std::uintptr_t>(raw_template.data() + raw_template.size()),
        0, static_cast<std::uint32_t>(kPointerBackedTlsSlotOffset + sizeof(std::uint64_t)), {});
    initialize_thread_tls(&teb);
    initialize_pointer_backed_tls_slot(&teb);

    std::uint64_t block_value = 0;
    std::memcpy(&block_value, teb.tls_module0_data.data() + kPointerBackedTlsSlotOffset,
                sizeof(block_value));
    ASSERT_NE(block_value, 0U);
    const auto* const block = reinterpret_cast<const std::byte*>(
        static_cast<std::uintptr_t>(block_value));
    for (std::size_t index = 0; index < kPointerBackedTlsAllocationSize; ++index) {
        ASSERT_EQ(block[index], std::byte{0});
    }

    free_tls_dynamic_blocks(&teb);
    set_guest_tls_directory(0, 0, 0, 0, {});
}

TEST(Win32FlsTest, ValuesAreIndependentPerHostThreadAndFreeClearsTheIndex) {
    const std::uint32_t index = tl_FlsAlloc(0);
    ASSERT_NE(index, abi::kFlsOutOfIndexes);
    void* const main_value = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1010));
    void* const thread_value = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x2020));
    ASSERT_EQ(tl_FlsSetValue(index, main_value), 1);
    EXPECT_EQ(tl_FlsGetValue(index), main_value);
    void* observed_before = main_value;
    void* observed_after = nullptr;
    std::thread worker([&] {
        observed_before = tl_FlsGetValue(index);
        EXPECT_EQ(tl_FlsSetValue(index, thread_value), 1);
        observed_after = tl_FlsGetValue(index);
    });
    worker.join();
    EXPECT_EQ(observed_before, nullptr);
    EXPECT_EQ(observed_after, thread_value);
    EXPECT_EQ(tl_FlsGetValue(index), main_value);
    EXPECT_EQ(tl_FlsFree(index), 1);
    EXPECT_EQ(tl_FlsGetValue(index), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32FlsTest, RejectsInvalidCallbackAndReportsExhaustion) {
    const auto invalid_callback = reinterpret_cast<std::uintptr_t>(&tl_GetACP);
    EXPECT_EQ(tl_FlsAlloc(invalid_callback), abi::kFlsOutOfIndexes);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    std::array<std::uint32_t, 128> indices{};
    for (std::uint32_t& index : indices) {
        index = tl_FlsAlloc(0);
        ASSERT_NE(index, abi::kFlsOutOfIndexes);
    }
    EXPECT_EQ(tl_FlsAlloc(0), abi::kFlsOutOfIndexes);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotEnoughMemory);
    for (const std::uint32_t index : indices) {
        EXPECT_EQ(tl_FlsFree(index), 1);
    }
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

TEST(Win32ConcurrencyTest, ProtectedThreadBuffersRejectUnmappedMemory) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    EXPECT_EQ(tl_GetExitCodeThread(nullptr, static_cast<std::uint32_t*>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetThreadTimes(nullptr, invalid, nullptr, nullptr, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    std::size_t attribute_size = 64U;
    EXPECT_EQ(tl_InitializeProcThreadAttributeList(invalid, 1U, 0U, &attribute_size), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_InitializeProcThreadAttributeList(nullptr, 1U, 0U,
                                                    static_cast<std::size_t*>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    tl_InitializeSListHead(reinterpret_cast<abi::GuestSListHeader*>(invalid));
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    std::array<std::byte, 16> entry{};
    EXPECT_EQ(tl_InterlockedPushEntrySList(invalid, entry.data()), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32ConcurrencyTest, CreateThreadRejectsInvalidStartWithoutPartialHandle) {
    std::uint32_t thread_id = 0xA5A5U;
    EXPECT_EQ(tl_CreateThread(nullptr, 0, 0, nullptr, 0, &thread_id), nullptr);
    EXPECT_EQ(thread_id, 0xA5A5U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32ConcurrencyTest, CreateThreadRejectsReadableNonExecutableStart) {
    std::uint32_t readable_data = 0;
    EXPECT_EQ(tl_CreateThread(nullptr, 0,
                              reinterpret_cast<std::uintptr_t>(&readable_data), nullptr, 0,
                              nullptr),
              nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32ConcurrencyTest, CreateThreadCleansUpAfterInjectedGsFailure) {
    g_gs_failure_entry_calls.store(0, std::memory_order_relaxed);
    const std::filesystem::path trace_directory =
        std::filesystem::temp_directory_path() /
        ("tl-create-thread-gs-failure-" + std::to_string(static_cast<unsigned long long>(getpid())));
    std::filesystem::remove_all(trace_directory);
    ASSERT_TRUE(diagnostics::configure_trace_json_directory(trace_directory));
    GuestGsFailureScope failure_scope;

    std::uint32_t thread_id = 0;
    void* const handle = tl_CreateThread(nullptr, 0,
                                         reinterpret_cast<std::uintptr_t>(&gs_failure_entry),
                                         nullptr, 0, &thread_id);
    ASSERT_NE(handle, nullptr);
    ASSERT_NE(thread_id, 0U);
    const std::size_t slot_index = static_cast<std::size_t>(
        reinterpret_cast<std::uintptr_t>(handle) - kThreadHandleBase);
    ASSERT_LT(slot_index, g_threads.size());

    EXPECT_EQ(tl_WaitForSingleObject(handle, 1000), abi::kWaitObject0);
    std::uint32_t exit_code = 0xFFFFFFFFU;
    EXPECT_EQ(tl_GetExitCodeThread(handle, &exit_code), 1);
    EXPECT_EQ(exit_code, 0U);
    EXPECT_EQ(g_gs_failure_entry_calls.load(std::memory_order_relaxed), 0U);
    EXPECT_EQ(tl_CloseHandle(handle), 1);
    EXPECT_EQ(find_thread_slot(handle), nullptr);
    {
        std::lock_guard<std::mutex> lock(g_threads_mutex);
        EXPECT_FALSE(g_threads[slot_index].used);
        EXPECT_EQ(g_threads[slot_index].teb, nullptr);
        EXPECT_EQ(g_threads[slot_index].stack, nullptr);
        EXPECT_EQ(g_threads[slot_index].stack_size, 0U);
    }

    diagnostics::disable_trace_json_directory();
    std::string trace;
    for (const auto& entry : std::filesystem::directory_iterator(trace_directory)) {
        if (entry.path().extension() != ".jsonl") continue;
        std::ifstream input(entry.path());
        trace.append(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
    }
    EXPECT_NE(trace.find("\"event\": \"api-failure\""), std::string::npos);
    EXPECT_NE(trace.find("\"symbol\": \"CreateThread\""), std::string::npos);
    EXPECT_NE(trace.find("\"operation\": \"guest-teb\""), std::string::npos);
    std::filesystem::remove_all(trace_directory);
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

TEST(Win32ConcurrencyTest, ProtectedSynchronizationPointersRejectUnmappedMemory) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000));
    EXPECT_EQ(tl_CreateMutexA(invalid, 0, nullptr), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_CreateEventA(invalid, 0, 0, nullptr), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_CreateSemaphoreA(invalid, 0, 1, nullptr), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_WaitForMultipleObjects(
                  1, reinterpret_cast<const void* const*>(invalid), 0, 0),
              abi::kWaitFailed);

    std::uint32_t expected = 0;
    EXPECT_EQ(tl_WaitOnAddress(invalid, &expected, sizeof(expected), 0), 0);

    void* const semaphore = tl_CreateSemaphoreA(nullptr, 0, 1, nullptr);
    ASSERT_NE(semaphore, nullptr);
    EXPECT_EQ(tl_ReleaseSemaphore(
                  semaphore, 1, reinterpret_cast<std::int32_t*>(invalid)),
              0);
    EXPECT_EQ(tl_WaitForSingleObject(semaphore, 0), abi::kWaitTimeout);
    EXPECT_EQ(tl_CloseHandle(semaphore), 1);

    int pending = 0;
    void* context = nullptr;
    EXPECT_EQ(tl_InitOnceBeginInitialize(invalid, 0, &pending, &context), 0);
    tl_InitializeSRWLock(invalid);
    tl_InitializeConditionVariable(invalid);
    EXPECT_EQ(tl_RegisterWaitForSingleObject(
                  reinterpret_cast<void**>(invalid), nullptr, nullptr, nullptr, 0, 0),
              0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
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

TEST(Win32AnsiTest, FormatMessageAWithGuestBufferHonorsSize) {
    constexpr std::uint32_t kFormatMessageFromSystem = 0x1000U;
    char small[4]{};
    EXPECT_EQ(tl_FormatMessageA(kFormatMessageFromSystem, nullptr, abi::kErrorFileNotFound, 0,
                                small, sizeof(small), nullptr), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInsufficientBuffer);

    char big[128]{};
    const std::uint32_t written =
        tl_FormatMessageA(kFormatMessageFromSystem, nullptr, abi::kErrorAccessDenied, 0,
                          big, sizeof(big), nullptr);
    ASSERT_GT(written, 0U);
    EXPECT_STREQ(big, "Access is denied.");
}

TEST(Win32AnsiTest, FormatMessageAAllocateBufferUsesLocalFree) {
    constexpr std::uint32_t kFormatMessageAllocateBuffer = 0x100U;
    constexpr std::uint32_t kFormatMessageFromSystem = 0x1000U;
    char* buffer = nullptr;
    const std::uint32_t written =
        tl_FormatMessageA(kFormatMessageAllocateBuffer | kFormatMessageFromSystem, nullptr,
                          abi::kErrorFileNotFound, 0, reinterpret_cast<char*>(&buffer), 0,
                          nullptr);
    ASSERT_GT(written, 0U);
    ASSERT_NE(buffer, nullptr);
    EXPECT_STREQ(buffer, "The system cannot find the file specified.");
    EXPECT_EQ(tl_LocalFree(buffer), nullptr);
}

TEST(Win32WideTest, GetConsoleOutputCPReturnsUtf8) {
    EXPECT_EQ(tl_GetConsoleOutputCP(), 65001U);
    EXPECT_NE(tl_SetConsoleOutputCP(65001U), 0);
}

TEST(Win32WideTest, LocalFreeReturnsNull) {
    void* memory = tl_LocalAlloc(0, 16);
    ASSERT_NE(memory, nullptr);
    EXPECT_EQ(tl_LocalFree(memory), nullptr);
}

TEST(Win32WideTest, LocalFreeRejectsUnknownHandle) {
    void* const invalid = reinterpret_cast<void*>(0x1234);
    EXPECT_EQ(tl_LocalFree(invalid), invalid);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidHandle);
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

TEST(OleAut32Test, BstrAndVariantOperations) {
    constexpr std::uint16_t sample[] = {'T', 'e', 's', 't', 'B', 'S', 'T', 'R', 0};
    GuestBstr bstr = tl_SysAllocString(sample);
    ASSERT_NE(bstr, nullptr);
    EXPECT_EQ(tl_SysStringLen(bstr), 8U);
    EXPECT_EQ(tl_SysStringByteLen(bstr), 16U);

    GuestVariant var{};
    tl_VariantInit(&var);
    EXPECT_EQ(var.vt, kGuestVtEmpty);

    var.vt = kGuestVtBstr;
    var.data.bstrVal = bstr;

    GuestVariant var_copy{};
    EXPECT_EQ(tl_VariantCopy(&var_copy, &var), 0);
    EXPECT_EQ(var_copy.vt, kGuestVtBstr);
    EXPECT_NE(var_copy.data.bstrVal, nullptr);
    EXPECT_EQ(tl_SysStringLen(var_copy.data.bstrVal), 8U);

    EXPECT_EQ(tl_VariantClear(&var_copy), 0);
    EXPECT_EQ(var_copy.vt, kGuestVtEmpty);

    EXPECT_EQ(tl_VariantClear(&var), 0);
}

TEST(Win32HandleObjectTest, FindHandleCannotBeClosedWithCloseHandle) {
    const char* tmpdir = "_tl_test_find_obj";
    mkdir(tmpdir, 0777);
    const std::string file_path = std::string(tmpdir) + "/dummy.txt";
    FILE* f = std::fopen(file_path.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fwrite("abc", 1, 3, f);
    std::fclose(f);

    char pattern[64]{};
    std::snprintf(pattern, sizeof(pattern), "%s/*", tmpdir);
    char find_data_buf[400]{};
    void* find_handle = tl_FindFirstFileA(pattern, find_data_buf);
    ASSERT_NE(find_handle, reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max()));

    // Attempting to close a Find handle with CloseHandle must fail with ERROR_INVALID_HANDLE
    EXPECT_EQ(tl_CloseHandle(find_handle), 0);
    EXPECT_EQ(tl_GetLastError(), 6U /* ERROR_INVALID_HANDLE */);

    // Must be closed with FindClose
    EXPECT_EQ(tl_FindClose(find_handle), 1);

    std::remove(file_path.c_str());
    rmdir(tmpdir);
}

TEST(Win32HandleObjectTest, DuplicateHandleIncrementsRefCountAndAllowsMultipleClose) {
    const char* path = "_tl_test_dup_handle.bin";
    void* handle = tl_CreateFileA(path, 0xC0000000U /* GENERIC_READ | GENERIC_WRITE */,
                                  0, nullptr, 2U /* CREATE_ALWAYS */, 0x80U /* NORMAL */, nullptr);
    ASSERT_NE(handle, reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1)));

    void* dup_handle = nullptr;
    EXPECT_EQ(tl_DuplicateHandle(nullptr, handle, nullptr, &dup_handle, 0, 0, 2), 1);
    EXPECT_EQ(dup_handle, handle);

    // Writing through original handle works
    const char msg[] = "test";
    std::uint32_t written = 0;
    EXPECT_EQ(tl_WriteFile(handle, msg, 4, &written, nullptr), 1);
    EXPECT_EQ(written, 4U);

    // Close duplicated handle - ref count decrements to 1, file remains open
    EXPECT_EQ(tl_CloseHandle(dup_handle), 1);

    // Can still write through handle
    EXPECT_EQ(tl_WriteFile(handle, msg, 4, &written, nullptr), 1);
    EXPECT_EQ(written, 4U);

    // Close original handle - ref count reaches 0, actually closed
    EXPECT_EQ(tl_CloseHandle(handle), 1);

    // Third close fails with INVALID_HANDLE
    EXPECT_EQ(tl_CloseHandle(handle), 0);
    EXPECT_EQ(tl_GetLastError(), 6U);

    std::remove(path);
}

TEST(Win32HandleObjectTest, DuplicateHandleRejectsInvalidHandles) {
    void* target = nullptr;
    EXPECT_EQ(tl_DuplicateHandle(nullptr, nullptr, nullptr, &target, 0, 0, 0), 0);
    EXPECT_EQ(tl_GetLastError(), 6U /* ERROR_INVALID_HANDLE */);

    EXPECT_EQ(tl_DuplicateHandle(nullptr, reinterpret_cast<void*>(0xBAADF00DULL), nullptr, &target, 0, 0, 0), 0);
    EXPECT_EQ(tl_GetLastError(), 6U /* ERROR_INVALID_HANDLE */);

    EXPECT_EQ(tl_DuplicateHandle(nullptr, reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1)), nullptr, &target, 0, 0, 0), 0);
    EXPECT_EQ(tl_GetLastError(), 6U /* ERROR_INVALID_HANDLE */);
}

TEST(Win32HandleObjectTest, DuplicateHandleSyncObject) {
    void* event = tl_CreateEventA(nullptr, 0, 0, nullptr);
    ASSERT_NE(event, nullptr);

    void* dup_event = nullptr;
    EXPECT_EQ(tl_DuplicateHandle(nullptr, event, nullptr, &dup_event, 0, 0, 0), 1);
    EXPECT_EQ(dup_event, event);

    // First close decrements ref count
    EXPECT_EQ(tl_CloseHandle(dup_event), 1);

    // Event is still usable
    EXPECT_EQ(tl_SetEvent(event), 1);

    // Second close frees event
    EXPECT_EQ(tl_CloseHandle(event), 1);

    // Third close fails
    EXPECT_EQ(tl_CloseHandle(event), 0);
}

TEST(Win32CodePageTest, CP1250RoundTrip) {
    // 0x8A = Š (U+0160), 0x9A = š (U+0161), 0x8E = Ž (U+017D), 0x9E = ž (U+017E), 0xA3 = Ł (U+0141), 0xB3 = ł (U+0142)
    const char cp1250_input[] = {static_cast<char>(0x8A), static_cast<char>(0x9A),
                                  static_cast<char>(0x8E), static_cast<char>(0x9E),
                                  static_cast<char>(0xA3), static_cast<char>(0xB3), 0};
    std::uint16_t wide[8]{};
    const int converted = tl_MultiByteToWideChar(abi::kCp1250, 0, cp1250_input, -1, wide, 8);
    ASSERT_EQ(converted, 7);
    EXPECT_EQ(wide[0], 0x0160U);
    EXPECT_EQ(wide[1], 0x0161U);
    EXPECT_EQ(wide[2], 0x017DU);
    EXPECT_EQ(wide[3], 0x017EU);
    EXPECT_EQ(wide[4], 0x0141U);
    EXPECT_EQ(wide[5], 0x0142U);
    EXPECT_EQ(wide[6], 0U);

    char back[8]{};
    int used_default = 0;
    const int back_len = tl_WideCharToMultiByte(abi::kCp1250, 0, wide, -1, back, 8, nullptr, &used_default);
    ASSERT_EQ(back_len, 7);
    EXPECT_EQ(used_default, 0);
    EXPECT_EQ(static_cast<unsigned char>(back[0]), 0x8AU);
    EXPECT_EQ(static_cast<unsigned char>(back[1]), 0x9AU);
    EXPECT_EQ(static_cast<unsigned char>(back[2]), 0x8EU);
    EXPECT_EQ(static_cast<unsigned char>(back[3]), 0x9EU);
    EXPECT_EQ(static_cast<unsigned char>(back[4]), 0xA3U);
    EXPECT_EQ(static_cast<unsigned char>(back[5]), 0xB3U);
}

TEST(Win32CodePageTest, CP1251RoundTrip) {
    // "Привет": П (0xCF = U+041F), р (0xF0 = U+0440), и (0xE8 = U+0438), в (0xE2 = U+0432), е (0xE5 = U+0435), т (0xF2 = U+0442)
    const char cp1251_input[] = {static_cast<char>(0xCF), static_cast<char>(0xF0),
                                  static_cast<char>(0xE8), static_cast<char>(0xE2),
                                  static_cast<char>(0xE5), static_cast<char>(0xF2), 0};
    std::uint16_t wide[8]{};
    const int converted = tl_MultiByteToWideChar(abi::kCp1251, 0, cp1251_input, -1, wide, 8);
    ASSERT_EQ(converted, 7);
    EXPECT_EQ(wide[0], 0x041FU);
    EXPECT_EQ(wide[1], 0x0440U);
    EXPECT_EQ(wide[2], 0x0438U);
    EXPECT_EQ(wide[3], 0x0432U);
    EXPECT_EQ(wide[4], 0x0435U);
    EXPECT_EQ(wide[5], 0x0442U);

    char back[8]{};
    int used_default = 0;
    const int back_len = tl_WideCharToMultiByte(abi::kCp1251, 0, wide, -1, back, 8, nullptr, &used_default);
    ASSERT_EQ(back_len, 7);
    EXPECT_EQ(used_default, 0);
    EXPECT_EQ(static_cast<unsigned char>(back[0]), 0xCFU);
    EXPECT_EQ(static_cast<unsigned char>(back[1]), 0xF0U);
    EXPECT_EQ(static_cast<unsigned char>(back[2]), 0xE8U);
    EXPECT_EQ(static_cast<unsigned char>(back[3]), 0xE2U);
    EXPECT_EQ(static_cast<unsigned char>(back[4]), 0xE5U);
    EXPECT_EQ(static_cast<unsigned char>(back[5]), 0xF2U);
}

TEST(Win32CodePageTest, CP28591RoundTrip) {
    const char latin1_input[] = {'H', 'e', 'l', 'l', 'o', static_cast<char>(0xE9), static_cast<char>(0xC0), 0};
    std::uint16_t wide[10]{};
    const int converted = tl_MultiByteToWideChar(abi::kCp28591, 0, latin1_input, -1, wide, 10);
    ASSERT_EQ(converted, 8);
    EXPECT_EQ(wide[5], 0x00E9U);
    EXPECT_EQ(wide[6], 0x00C0U);

    char back[10]{};
    int used_default = 0;
    const int back_len = tl_WideCharToMultiByte(abi::kCp28591, 0, wide, -1, back, 10, nullptr, &used_default);
    ASSERT_EQ(back_len, 8);
    EXPECT_EQ(used_default, 0);
    EXPECT_EQ(static_cast<unsigned char>(back[5]), 0xE9U);
    EXPECT_EQ(static_cast<unsigned char>(back[6]), 0xC0U);
}

TEST(Win32CodePageTest, IsValidCodePageRecognizesExpandedPages) {
    EXPECT_EQ(tl_IsValidCodePage(abi::kCp1252), 1);
    EXPECT_EQ(tl_IsValidCodePage(abi::kCp437), 1);
    EXPECT_EQ(tl_IsValidCodePage(abi::kCpUtf8), 1);
    EXPECT_EQ(tl_IsValidCodePage(abi::kCp1250), 1);
    EXPECT_EQ(tl_IsValidCodePage(abi::kCp1251), 1);
    EXPECT_EQ(tl_IsValidCodePage(abi::kCp28591), 1);
    EXPECT_EQ(tl_IsValidCodePage(932U), 0);
    EXPECT_EQ(tl_IsValidCodePage(99999U), 0);
}

TEST(Win32CodePageTest, GetCPInfoSucceedsForExpandedPages) {
    abi::GuestCpInfo info{};
    EXPECT_EQ(tl_GetCPInfo(abi::kCp1250, &info), 1);
    EXPECT_EQ(info.max_char_size, 1U);
    EXPECT_EQ(info.default_char[0], '?');

    EXPECT_EQ(tl_GetCPInfo(abi::kCp1251, &info), 1);
    EXPECT_EQ(info.max_char_size, 1U);

    EXPECT_EQ(tl_GetCPInfo(abi::kCp28591, &info), 1);
    EXPECT_EQ(info.max_char_size, 1U);

    EXPECT_EQ(tl_GetCPInfo(932U, &info), 0);
}

}  // namespace
}  // namespace tradutorlinux
