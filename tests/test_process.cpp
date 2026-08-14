#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/process.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>
#include <gtest/gtest.h>

namespace tradutorlinux::loader {
namespace {

std::vector<std::byte> read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    EXPECT_TRUE(input.good()) << "fixture não encontrada: " << path;
    std::vector<std::byte> bytes;
    char byte = '\0';
    while (input.get(byte)) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
    }
    return bytes;
}

std::string fixture_path(const std::string& name) {
    return std::string(TL_FIXTURE_OUTPUT_DIRECTORY) + "/" + name + ".exe";
}

std::optional<std::string> maps_permissions_for(const std::uintptr_t address) {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        const std::string::size_type space = line.find(' ');
        if (space == std::string::npos) {
            continue;
        }
        const std::string range = line.substr(0, space);
        const std::string::size_type dash = range.find('-');
        if (dash == std::string::npos) {
            continue;
        }
        const std::uintptr_t start = static_cast<std::uintptr_t>(
            std::stoull(range.substr(0, dash), nullptr, 16));
        const std::uintptr_t end = static_cast<std::uintptr_t>(
            std::stoull(range.substr(dash + 1), nullptr, 16));
        if (address >= start && address < end) {
            return line.substr(space + 1, 4);
        }
    }
    return std::nullopt;
}

class ProcessTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_modules();
        register_builtin_modules();
        page = sysconf(_SC_PAGESIZE) > 0 ? static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE))
                                         : 0x1000ULL;
    }

    void TearDown() override {
        clear_modules();
    }

    std::uint64_t page{0x1000};
};

TEST_F(ProcessTest, PreparesNopProcessWithStackAndGuardPage) {
    const std::vector<std::byte> bytes = read_file(fixture_path("tl_nop"));
    const pe::ParseResult parse_result = pe::parse_pe(bytes);
    ASSERT_EQ(parse_result.status, pe::ParseStatus::Success);

    PrepareResult result = prepare_process(parse_result.info, bytes);
    ASSERT_EQ(result.status, PrepareStatus::Success);
    ASSERT_EQ(result.process.thread.entry_point,
              result.process.image.base +
                  static_cast<std::uint64_t>(result.process.info.address_of_entry_point));
    EXPECT_EQ(result.process.imports.status, ImportStatus::Resolved);
    EXPECT_TRUE(result.process.imports.imports.empty());

    const GuestThread& thread = result.process.thread;
    ASSERT_GT(thread.stack_top, 0U);
    EXPECT_EQ(thread.stack_size, kGuestStackSize);
    EXPECT_EQ(thread.stack_top % 16, 0U);

    const std::uintptr_t first_usable =
        static_cast<std::uintptr_t>(thread.stack_top - thread.stack_size);
    const std::uintptr_t guard_page =
        static_cast<std::uintptr_t>(thread.stack_top - thread.stack_size - page);
    EXPECT_EQ(maps_permissions_for(first_usable), "rw-p");
    EXPECT_EQ(maps_permissions_for(guard_page), "---p");

    destroy_process(result.process);
    EXPECT_EQ(result.process.image.memory, nullptr);
    EXPECT_EQ(result.process.stack, nullptr);
    EXPECT_TRUE(result.process.imports.imports.empty());
}

TEST_F(ProcessTest, PreparesHelloWithAllImportsResolved) {
    const std::vector<std::byte> bytes = read_file(fixture_path("tl_hello"));
    const pe::ParseResult parse_result = pe::parse_pe(bytes);
    ASSERT_EQ(parse_result.status, pe::ParseStatus::Success);

    PrepareResult result = prepare_process(parse_result.info, bytes);
    ASSERT_EQ(result.status, PrepareStatus::Success);
    ASSERT_EQ(result.process.imports.imports.size(), 3U);
    for (const ResolvedImport& entry : result.process.imports.imports) {
        EXPECT_EQ(entry.status, ImportStatus::Resolved);
        EXPECT_FALSE(entry.by_ordinal);
    }

    destroy_process(result.process);
}

TEST_F(ProcessTest, RejectsUnknownDllWithUnresolvedImports) {
    const std::vector<std::byte> bytes = read_file(fixture_path("tl_missing_dll"));
    const pe::ParseResult parse_result = pe::parse_pe(bytes);
    ASSERT_EQ(parse_result.status, pe::ParseStatus::Success);

    PrepareResult result = prepare_process(parse_result.info, bytes);
    ASSERT_EQ(result.status, PrepareStatus::UnresolvedImports);
    EXPECT_FALSE(result.error_message.empty());
    ASSERT_EQ(result.process.imports.imports.size(), 1U);
    EXPECT_EQ(result.process.imports.imports[0].status, ImportStatus::UnknownDll);

    destroy_process(result.process);
}

TEST_F(ProcessTest, DestroyProcessClearsState) {
    const std::vector<std::byte> bytes = read_file(fixture_path("tl_nop"));
    const pe::ParseResult parse_result = pe::parse_pe(bytes);
    ASSERT_EQ(parse_result.status, pe::ParseStatus::Success);

    PrepareResult result = prepare_process(parse_result.info, bytes);
    ASSERT_EQ(result.status, PrepareStatus::Success);
    const std::uint64_t image_base = result.process.image.base;
    ASSERT_GT(image_base, 0U);

    destroy_process(result.process);
    EXPECT_EQ(result.process.image.memory, nullptr);
    EXPECT_EQ(result.process.image.base, 0U);
    EXPECT_EQ(result.process.stack, nullptr);
    EXPECT_EQ(result.process.stack_size, 0U);
    EXPECT_TRUE(result.process.imports.imports.empty());
    EXPECT_EQ(result.process.thread.stack_top, 0U);
    EXPECT_EQ(result.process.thread.entry_point, 0U);
}

}  // namespace
}  // namespace tradutorlinux::loader
