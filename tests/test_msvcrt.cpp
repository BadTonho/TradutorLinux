#include "tradutorlinux/runtime/msvcrt.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <unistd.h>

namespace tradutorlinux {
namespace {

TEST(MsvcrtStartupTest, GetmainargsMirrorsGuestCommandLine) {
    msvcrt_set_guest_command_line({"tl_hello.exe", "-p", "entrada.bin"});
    int argc = 0;
    char** argv = nullptr;
    char** envp = nullptr;
    int glob = 0;

    ASSERT_EQ(tl___getmainargs(&argc, &argv, &envp, &glob, nullptr), 0);
    EXPECT_EQ(argc, 3);
    ASSERT_NE(argv, nullptr);
    EXPECT_EQ(std::string(argv[0]), "tl_hello.exe");
    EXPECT_EQ(std::string(argv[1]), "-p");
    EXPECT_EQ(std::string(argv[2]), "entrada.bin");
    EXPECT_EQ(argv[3], nullptr);
    EXPECT_NE(envp, nullptr);
    std::free(argv);
    msvcrt_set_guest_command_line({});
}

TEST(MsvcrtStartupTest, GetmainargsDefaultsToEmptyArgv0) {
    int argc = -1;
    char** argv = nullptr;
    char** envp = nullptr;
    int glob = 0;
    ASSERT_EQ(tl___getmainargs(&argc, &argv, &envp, &glob, nullptr), 0);
    EXPECT_EQ(argc, 1);
    ASSERT_NE(argv, nullptr);
    EXPECT_STREQ(argv[0], "");
    std::free(argv);
}

TEST(MsvcrtFileTest, FopenFprintfFseekAndFcloseRoundTrip) {
    const std::string path = "tl_msvcrt_unit_" + std::to_string(static_cast<long>(getpid())) + ".txt";
    std::remove(path.c_str());

    GuestFile* file = tl_fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    const int wrote = tl_fprintf(file, "linha %d: %s", 7, "valor");
    EXPECT_GT(wrote, 0);
    ASSERT_EQ(tl_fclose(file), 0);

    file = tl_fopen(path.c_str(), "rb");
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(tl_ferror(file), 0);
    EXPECT_GE(tl__fileno(file), 0);
    char buffer[16]{};
    for (std::size_t index = 0; index < 14; ++index) {
        buffer[index] = static_cast<char>(tl_getc(file));
    }
    EXPECT_EQ(std::string(buffer), "linha 7: valor");
    EXPECT_EQ(tl_fseek(file, 0, SEEK_END), 0);
    EXPECT_EQ(tl_ftell(file), 14L);
    ASSERT_EQ(tl_fclose(file), 0);
    std::remove(path.c_str());
}

TEST(MsvcrtFileTest, FopenOfMissingFileReturnsNull) {
    EXPECT_EQ(tl_fopen("tl_msvcrt_nao_existe_12345.txt", "rb"), nullptr);
}

TEST(MsvcrtStringTest, StrcatFamilyBehavesLikeLibc) {
    char buffer[32] = "abc";
    EXPECT_EQ(tl_strlen(buffer), 3U);
    EXPECT_EQ(tl_strcmp("abc", "abc"), 0);
    EXPECT_LT(tl_strcmp("abc", "abd"), 0);
    EXPECT_GT(tl_strcmp("abd", "abc"), 0);
    EXPECT_EQ(tl_strncmp("abcd", "abce", 3), 0);
    EXPECT_EQ(tl_strcpy(buffer, "xyz"), buffer);
    EXPECT_STREQ(buffer, "xyz");
    EXPECT_EQ(tl_strlen(tl_strcpy(buffer, "prolongado")), 10U);
}

TEST(MsvcrtStringTest, StrtolAndStrtoulParseBases) {
    char* end = nullptr;
    EXPECT_EQ(tl_strtol("  -123", &end, 10), -123L);
    EXPECT_EQ(tl_strtoul("0x1F", &end, 16), 31UL);
    EXPECT_EQ(tl_strtol("1010", &end, 2), 10L);
}

TEST(MsvcrtStringTest, WcslenCountsUtf16Units) {
    const std::uint16_t text[] = {'a', 0x00E7, 0xD83D, 0xDE00, 0};
    EXPECT_EQ(tl_wcslen(text), 4U);
}

TEST(MsvcrtMemoryTest, MallocCallocFreeAndMemset) {
    void* block = tl_malloc(32);
    ASSERT_NE(block, nullptr);
    tl_memset(block, 0xAB, 32);
    EXPECT_EQ(static_cast<unsigned char*>(block)[0], 0xAB);
    tl_free(block);

    int* counts = static_cast<int*>(tl_calloc(4, sizeof(int)));
    ASSERT_NE(counts, nullptr);
    EXPECT_EQ(counts[3], 0);
    tl_free(counts);
}

TEST(MsvcrtLocaleTest, CodePageIsFixedAtCp1252) {
    EXPECT_EQ(tl___lc_codepage_func(), 1252U);
    EXPECT_EQ(tl___mb_cur_max_func(), 1);
}

TEST(MsvcrtLocaleTest, LocaleconvExposesCDecimalPoint) {
    void* locale = tl_localeconv();
    ASSERT_NE(locale, nullptr);
    EXPECT_STREQ(*static_cast<char**>(locale), ".");
}

TEST(MsvcrtIoTest, SetmodeReportsPreviousMode) {
    constexpr int kText = 0x4000;
    constexpr int kBinary = 0x8000;
    const int original = tl__setmode(STDOUT_FILENO, kBinary);
    EXPECT_EQ(tl__setmode(STDOUT_FILENO, kBinary), kBinary);
    EXPECT_EQ(tl__setmode(STDOUT_FILENO, kText), kBinary);
    EXPECT_EQ(tl__setmode(STDOUT_FILENO, kText), kText);
    tl__setmode(STDOUT_FILENO, original);
}

TEST(MsvcrtIoTest, IsattyOnPipeIsFalse) {
    EXPECT_EQ(tl__isatty(STDIN_FILENO), 0);
}

TEST(MsvcrtEnvTest, GetenvFindsPath) {
    char* path = tl_getenv("PATH");
    ASSERT_NE(path, nullptr);
    EXPECT_GT(std::strlen(path), 0U);
}

TEST(MsvcrtErrnoTest, ErrnoCellIsReadWrite) {
    int* cell = tl__errno();
    ASSERT_NE(cell, nullptr);
    *cell = 0;
    *cell = 42;
    EXPECT_EQ(*tl__errno(), 42);
    *cell = 0;
}

TEST(MsvcrtSignalTest, RegistersAndReplacesHandler) {
    const auto handler_fn = static_cast<void (*)(int)>([](int) {});
    const void* handler = reinterpret_cast<const void*>(handler_fn);
    EXPECT_EQ(tl_signal(15, handler_fn), nullptr);
    EXPECT_EQ(tl_signal(15, handler_fn), handler);
    EXPECT_EQ(tl_signal(15, nullptr), handler);
    EXPECT_EQ(tl_signal(-1, handler_fn), reinterpret_cast<void*>(SIG_ERR));
}

TEST(MsvcrtCharTest, IsalnumAndToupperFollowAscii) {
    EXPECT_EQ(tl_isalnum('A'), 1);
    EXPECT_EQ(tl_isalnum('#'), 0);
    EXPECT_EQ(tl_toupper('a'), 'A');
    EXPECT_EQ(tl_toupper('Z'), 'Z');
}

}  // namespace
}  // namespace tradutorlinux
