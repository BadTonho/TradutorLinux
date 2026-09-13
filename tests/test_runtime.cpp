#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

namespace tradutorlinux {
namespace {

TEST(RuntimeTest, LastErrorRoundTrip) {
    tl_SetLastError(1234);
    EXPECT_EQ(tl_GetLastError(), 1234U);
    tl_SetLastError(abi::kErrorSuccess);
}

TEST(RuntimeTest, VirtualAllocAndFreeUseDocumentedSubset) {
    void* memory = tl_VirtualAlloc(nullptr, 17, abi::kMemCommit | abi::kMemReserve,
                                   abi::kPageReadWrite);
    ASSERT_NE(memory, nullptr);
    static_cast<char*>(memory)[0] = 'x';
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
    EXPECT_EQ(tl_VirtualFree(memory, 0, abi::kMemRelease), 1);
    EXPECT_EQ(tl_VirtualFree(memory, 0, abi::kMemRelease), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(RuntimeTest, FileSubsetCreatesWritesReopensAndReads) {
    const std::string path = "tl_runtime_unit_" + std::to_string(static_cast<unsigned long>(getpid())) + ".tmp";
    std::remove(path.c_str());

    const void* file = tl_CreateFileA(path.c_str(), abi::kGenericRead | abi::kGenericWrite, 0,
                                      nullptr, abi::kCreateAlways, 0, nullptr);
    ASSERT_NE(file, nullptr);
    const std::array<char, 3> input{'a', 'b', 'c'};
    std::uint32_t written = 0;
    ASSERT_EQ(tl_WriteFile(file, input.data(), input.size(), &written, nullptr), 1);
    EXPECT_EQ(written, input.size());
    ASSERT_EQ(tl_CloseHandle(file), 1);

    file = tl_CreateFileA(path.c_str(), abi::kGenericRead, 0, nullptr, abi::kOpenExisting, 0,
                          nullptr);
    ASSERT_NE(file, nullptr);
    std::array<char, 3> output{};
    std::uint32_t read = 0;
    ASSERT_EQ(tl_ReadFile(file, output.data(), output.size(), &read, nullptr), 1);
    EXPECT_EQ(read, output.size());
    EXPECT_EQ(output, input);
    EXPECT_EQ(tl_CloseHandle(file), 1);
    std::remove(path.c_str());
}

TEST(RuntimeTest, RejectsAbsoluteFilePath) {
    const void* file = tl_CreateFileA("/tmp/not-supported", abi::kGenericRead, 0, nullptr,
                                      abi::kOpenExisting, 0, nullptr);
    EXPECT_EQ(file, reinterpret_cast<void*>(~static_cast<std::uintptr_t>(0)));
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(RuntimeMemoryValidationTest, RejectsMisalignedUtf16Pointer) {
    const std::uint16_t aligned_string[] = {u'A', 0};
    EXPECT_TRUE(runtime::validate_mapped_wstring(aligned_string));

    const auto* const bytes = reinterpret_cast<const std::byte*>(aligned_string);
    const auto* const misaligned_string = reinterpret_cast<const std::uint16_t*>(bytes + 1);
    EXPECT_FALSE(runtime::validate_mapped_wstring(misaligned_string));
}

TEST(RuntimeMemoryValidationTest, ProtectedCopiesReportInvalidAndPartialRanges) {
    std::array<std::byte, 8> local{};
    const runtime::GuestMemoryAccessResult null_read =
        runtime::read_guest_memory(nullptr, local.data(), local.size());
    EXPECT_EQ(null_read.status, runtime::GuestMemoryAccessStatus::InvalidArgument);
    const runtime::GuestMemoryAccessResult overflow_read = runtime::read_guest_memory(
        reinterpret_cast<const void*>(std::numeric_limits<std::uintptr_t>::max() - 3U),
        local.data(), 8);
    EXPECT_EQ(overflow_read.status, runtime::GuestMemoryAccessStatus::InvalidArgument);

    const long page_value = ::sysconf(_SC_PAGESIZE);
    ASSERT_GT(page_value, 0);
    const std::size_t page = static_cast<std::size_t>(page_value);
    auto* const mapping = static_cast<std::byte*>(
        ::mmap(nullptr, page * 2U, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    ASSERT_NE(mapping, MAP_FAILED);
    std::memset(mapping, 0xA5, page);
    ASSERT_EQ(::munmap(mapping + page, page), 0);

    std::vector<std::byte> destination(page * 2U);
    const runtime::GuestMemoryAccessResult partial =
        runtime::read_guest_memory(mapping, destination.data(), destination.size());
    EXPECT_EQ(partial.status, runtime::GuestMemoryAccessStatus::Partial);
    EXPECT_EQ(partial.transferred, page);
    EXPECT_EQ(destination.front(), static_cast<std::byte>(0xA5));

    ASSERT_EQ(::munmap(mapping, page), 0);
}

TEST(RuntimeMemoryValidationTest, ProtectedCopiesRejectReadOnlyWritesAndConcurrentProtectionChanges) {
    const long page_value = ::sysconf(_SC_PAGESIZE);
    ASSERT_GT(page_value, 0);
    const std::size_t page = static_cast<std::size_t>(page_value);
    auto* const mapping = static_cast<std::byte*>(
        ::mmap(nullptr, page * 2U, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    ASSERT_NE(mapping, MAP_FAILED);
    std::array<std::byte, 8> source{};
    const runtime::GuestMemoryAccessResult initial_write =
        runtime::write_guest_memory(mapping, source.data(), source.size());
    ASSERT_EQ(initial_write.status, runtime::GuestMemoryAccessStatus::Success);

    ASSERT_EQ(::mprotect(mapping, page, PROT_READ), 0);
    const runtime::GuestMemoryAccessResult read_only_write =
        runtime::write_guest_memory(mapping, source.data(), source.size());
    EXPECT_TRUE(read_only_write.status == runtime::GuestMemoryAccessStatus::Unmapped ||
                read_only_write.status == runtime::GuestMemoryAccessStatus::PermissionDenied);
    ASSERT_EQ(::mprotect(mapping, page, PROT_READ | PROT_WRITE), 0);

    std::atomic<bool> stop{false};
    std::thread protection_flipper([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            static_cast<void>(::mprotect(mapping + page, page, PROT_NONE));
            static_cast<void>(::mprotect(mapping + page, page, PROT_READ | PROT_WRITE));
        }
    });
    std::vector<std::byte> destination(page * 2U);
    for (int attempt = 0; attempt < 128; ++attempt) {
        const runtime::GuestMemoryAccessResult result =
            runtime::read_guest_memory(mapping, destination.data(), destination.size());
        EXPECT_TRUE(result.status == runtime::GuestMemoryAccessStatus::Success ||
                    result.status == runtime::GuestMemoryAccessStatus::Partial ||
                    result.status == runtime::GuestMemoryAccessStatus::Unmapped ||
                    result.status == runtime::GuestMemoryAccessStatus::PermissionDenied);
    }
    stop.store(true, std::memory_order_relaxed);
    protection_flipper.join();
    ASSERT_EQ(::munmap(mapping, page * 2U), 0);
}

}  // namespace
}  // namespace tradutorlinux
