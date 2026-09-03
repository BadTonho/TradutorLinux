#include "tradutorlinux/runtime/winapi.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <string>

#include <gtest/gtest.h>
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

}  // namespace
}  // namespace tradutorlinux
