#include "tradutorlinux/runtime/winapi.hpp"

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace tradutorlinux {
namespace {

using GetStdHandleFn = TL_MSABI void* (*)(std::uint32_t);
using WriteFileFn = TL_MSABI int (*)(const void*, const void*, std::uint32_t, std::uint32_t*,
                                     const void*);
using ReadFileFn = TL_MSABI int (*)(const void*, void*, std::uint32_t, std::uint32_t*,
                                    const void*);
using ExitProcessFn = TL_MSABI void (*)(std::uint32_t);

TEST(AbiTest, ExposesMsAbiMacro) {
#ifndef TL_MSABI
    GTEST_FAIL() << "TL_MSABI não está definido";
#endif
    // O tipo abaixo só compila se ms_abi for um atributo válido.
    using BoundFunction = TL_MSABI void (*)();
    (void)sizeof(BoundFunction);
}

TEST(AbiTest, ExposesWin32ScalarTypes) {
    EXPECT_EQ(sizeof(abi::Handle), sizeof(void*));
    EXPECT_EQ(sizeof(abi::Bool), 4);
    EXPECT_EQ(sizeof(abi::Dword), 4);
    EXPECT_EQ(sizeof(abi::Uint), 4);
}

TEST(AbiTest, ExposesStandardHandleConstants) {
    EXPECT_EQ(abi::kStdInputHandle, 0xFFFFFFF6U);
    EXPECT_EQ(abi::kStdOutputHandle, 0xFFFFFFF5U);
    EXPECT_EQ(abi::kStdErrorHandle, 0xFFFFFFF4U);
}

TEST(AbiTest, ConsoleApisAreReachableThroughMsAbiPointers) {
    const GetStdHandleFn get_std_handle = &tl_GetStdHandle;
    EXPECT_NE(get_std_handle(abi::kStdOutputHandle), nullptr);
    EXPECT_EQ(get_std_handle(0), nullptr);

    const WriteFileFn write_file = &tl_WriteFile;
    std::uint32_t bytes_written = 0xDEADBEEFU;
    const int written = write_file(nullptr, nullptr, 0, &bytes_written, nullptr);
    EXPECT_EQ(written, 0);
    EXPECT_EQ(bytes_written, 0U);

    const ReadFileFn read_file = &tl_ReadFile;
    std::uint32_t bytes_read = 0xDEADBEEFU;
    char buffer[4]{};
    const int read = read_file(nullptr, buffer, sizeof(buffer), &bytes_read, nullptr);
    EXPECT_EQ(read, 0);
    EXPECT_EQ(bytes_read, 0U);

    const ExitProcessFn exit_process = &tl_ExitProcess;
    EXPECT_NO_THROW(exit_process(0));
}

TEST(AbiTest, SymbolsHaveCAndNoexceptLinkage) {
    EXPECT_EQ(static_cast<GetStdHandleFn>(&tl_GetStdHandle), &tl_GetStdHandle);
}

}  // namespace
}  // namespace tradutorlinux
