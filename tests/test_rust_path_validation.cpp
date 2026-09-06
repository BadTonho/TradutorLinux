#include "path_rules.hpp"
#include "rust_path_validator.hpp"

#include "tradutorlinux/ffi/rust_validator.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include <gtest/gtest.h>

namespace {

using ValidateFunction = tl_rust_status_t (*)(
    const tl_rust_validator_t*, const std::uint8_t*, std::uint64_t, char*, std::uint64_t,
    std::uint64_t*);

std::uint8_t next_byte(std::uint64_t& state) {
    state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return static_cast<std::uint8_t>(state >> 32U);
}

class RustPathValidationTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(tl_rust_validator_create(1024U * 1024U, &validator_), TL_RUST_STATUS_OK);
        ASSERT_NE(validator_, nullptr);
    }

    void TearDown() override {
        tl_rust_validator_destroy(validator_);
    }

    [[nodiscard]] bool rust_accepts(const ValidateFunction function,
                                    const std::string_view path,
                                    tl_rust_status_t& status) const {
        std::array<char, 128> error{};
        std::uint64_t required = 0;
        status = function(validator_, reinterpret_cast<const std::uint8_t*>(path.data()),
                          static_cast<std::uint64_t>(path.size()), error.data(), error.size(),
                          &required);
        return status == TL_RUST_STATUS_OK;
    }

    tl_rust_validator_t* validator_{nullptr};
};

TEST_F(RustPathValidationTest, RelativeCorpusMatchesCppExceptIntentionalNulHardening) {
    const std::array<std::string_view, 9> corpus{
        "fixture.dat", "nested/file.dat", "nested/arquivo-\xc3\xa9.dat",
        "C:/compat-as-relative.dat", "", "/absolute.dat", "nested/../outside.dat",
        "nested\\outside.dat", "nested/./outside.dat"};

    for (const std::string_view path : corpus) {
        tl_rust_status_t status = TL_RUST_STATUS_INTERNAL;
        const bool rust_accepts_path = rust_accepts(
            tl_rust_validator_validate_relative_path, path, status);
        const bool cpp_accepts_path =
            tradutorlinux::compat::path_rules::is_relative_source(
                std::filesystem::path{std::string{path}});
        EXPECT_EQ(rust_accepts_path, cpp_accepts_path) << "path: " << path;
        EXPECT_EQ(status, cpp_accepts_path ? TL_RUST_STATUS_OK : TL_RUST_STATUS_INVALID_PATH)
            << "path: " << path;
    }

    const std::string nul_path{"bad\0name", 8U};
    tl_rust_status_t status = TL_RUST_STATUS_INTERNAL;
    EXPECT_FALSE(rust_accepts(tl_rust_validator_validate_relative_path, nul_path, status));
    EXPECT_EQ(status, TL_RUST_STATUS_INVALID_PATH);

    std::uint64_t state = UINT64_C(0x6c65786963616c31);
    for (std::size_t iteration = 0; iteration < 2048U; ++iteration) {
        const std::size_t length = static_cast<std::size_t>(next_byte(state)) % 96U;
        std::string generated;
        generated.reserve(length);
        for (std::size_t index = 0; index < length; ++index) {
            std::uint8_t byte = next_byte(state);
            if (byte == 0U) byte = static_cast<std::uint8_t>('x');
            generated.push_back(static_cast<char>(byte));
        }

        const bool rust_accepts_path = rust_accepts(
            tl_rust_validator_validate_relative_path, generated, status);
        const bool cpp_accepts_path = tradutorlinux::compat::path_rules::is_relative_source(
            std::filesystem::path{generated});
        EXPECT_EQ(rust_accepts_path, cpp_accepts_path) << "generated relative case: "
                                                       << iteration;
        EXPECT_EQ(status, cpp_accepts_path ? TL_RUST_STATUS_OK : TL_RUST_STATUS_INVALID_PATH)
            << "generated relative case: " << iteration;
    }
}

TEST_F(RustPathValidationTest, CDriveCorpusMatchesCppLexicalRules) {
    const std::array<std::string_view, 9> corpus{
        "C:\\Fixture\\compat.dat", "c:/Fixture/compat.dat", "C:\\a\\..\\b.dat",
        "C:\\a//b.dat", "", "D:\\Fixture\\file.dat", "C:", "C:\\",
        "C:\\..\\outside.dat"};

    for (const std::string_view path : corpus) {
        tl_rust_status_t status = TL_RUST_STATUS_INTERNAL;
        const bool rust_accepts_path =
            rust_accepts(tl_rust_validator_validate_c_drive_path, path, status);
        const bool cpp_accepts_path =
            tradutorlinux::compat::path_rules::is_c_drive_path_lexically_confined(path);
        EXPECT_EQ(rust_accepts_path, cpp_accepts_path) << "path: " << path;
        EXPECT_EQ(status, cpp_accepts_path ? TL_RUST_STATUS_OK : TL_RUST_STATUS_INVALID_PATH)
            << "path: " << path;
    }

    const std::string nul_path{"C:\\bad\0name", 11U};
    tl_rust_status_t status = TL_RUST_STATUS_INTERNAL;
    EXPECT_FALSE(rust_accepts(tl_rust_validator_validate_c_drive_path, nul_path, status));
    EXPECT_EQ(status, TL_RUST_STATUS_INVALID_PATH);

    std::uint64_t state = UINT64_C(0x6c65786963616c32);
    for (std::size_t iteration = 0; iteration < 2048U; ++iteration) {
        const std::size_t length = static_cast<std::size_t>(next_byte(state)) % 96U;
        std::string generated{"C:\\"};
        generated.reserve(3U + length);
        for (std::size_t index = 0; index < length; ++index) {
            std::uint8_t byte = next_byte(state);
            if (byte == 0U) byte = static_cast<std::uint8_t>('x');
            generated.push_back(static_cast<char>(byte));
        }

        const bool rust_accepts_path = rust_accepts(
            tl_rust_validator_validate_c_drive_path, generated, status);
        const bool cpp_accepts_path =
            tradutorlinux::compat::path_rules::is_c_drive_path_lexically_confined(generated);
        EXPECT_EQ(rust_accepts_path, cpp_accepts_path) << "generated C case: " << iteration;
        EXPECT_EQ(status, cpp_accepts_path ? TL_RUST_STATUS_OK : TL_RUST_STATUS_INVALID_PATH)
            << "generated C case: " << iteration;
    }
}

TEST_F(RustPathValidationTest, PathInputUsesTheConfiguredLimit) {
    tl_rust_validator_t* small_validator = nullptr;
    ASSERT_EQ(tl_rust_validator_create(4U, &small_validator), TL_RUST_STATUS_OK);
    ASSERT_NE(small_validator, nullptr);

    const std::string_view path = "five!";
    std::array<char, 64> error{};
    std::uint64_t required = 0;
    EXPECT_EQ(tl_rust_validator_validate_relative_path(
                  small_validator, reinterpret_cast<const std::uint8_t*>(path.data()),
                  path.size(), error.data(), error.size(), &required),
              TL_RUST_STATUS_INPUT_TOO_LARGE);
    EXPECT_NE(std::string_view{error.data()}.find("limit"), std::string_view::npos);
    tl_rust_validator_destroy(small_validator);

    const std::string exact_limit(1024U * 1024U, 'x');
    const std::string over_limit = exact_limit + 'x';
    tl_rust_status_t status = TL_RUST_STATUS_INTERNAL;
    EXPECT_TRUE(rust_accepts(tl_rust_validator_validate_relative_path, exact_limit, status));
    EXPECT_EQ(status, TL_RUST_STATUS_OK);
    EXPECT_FALSE(rust_accepts(tl_rust_validator_validate_relative_path, over_limit, status));
    EXPECT_EQ(status, TL_RUST_STATUS_INPUT_TOO_LARGE);

    const std::string max_c_drive = "C:\\" + std::string(1024U * 1024U - 3U, 'x');
    EXPECT_TRUE(rust_accepts(tl_rust_validator_validate_c_drive_path, max_c_drive, status));
    EXPECT_EQ(status, TL_RUST_STATUS_OK);
}

TEST_F(RustPathValidationTest, PathFunctionsKeepTheCommonFfiPointerContract) {
    const std::string_view path = "fixture.dat";
    std::array<char, 64> error{};
    std::uint64_t required = 0;

    EXPECT_EQ(tl_rust_validator_validate_relative_path(
                  validator_, nullptr, path.size(), error.data(), error.size(), &required),
              TL_RUST_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(tl_rust_validator_validate_relative_path(
                  validator_, reinterpret_cast<const std::uint8_t*>(path.data()), path.size(),
                  nullptr, 0U, &required),
              TL_RUST_STATUS_BUFFER_TOO_SMALL);
    EXPECT_EQ(tl_rust_validator_validate_relative_path(
                  validator_, reinterpret_cast<const std::uint8_t*>(path.data()), path.size(),
                  nullptr, 1U, &required),
              TL_RUST_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(tl_rust_validator_validate_c_drive_path(
                  validator_, reinterpret_cast<const std::uint8_t*>("C:\\Fixture\\file.dat"),
                  19U, error.data(), error.size(), nullptr),
              TL_RUST_STATUS_INVALID_ARGUMENT);
}

TEST(RustPathValidationSessionTest, ReusesOneHandleAndCollectsPhaseMetrics) {
    tradutorlinux::compat::detail::RustPathValidationSession session;
    ASSERT_TRUE(session.available());

    std::string error;
    EXPECT_EQ(session.validate_relative_path("fixture.dat", error),
              tradutorlinux::compat::detail::RustPathValidationResult::Accepted);
    EXPECT_EQ(session.validate_c_drive_path("C:\\Fixture\\compat.dat", error),
              tradutorlinux::compat::detail::RustPathValidationResult::Accepted);
    EXPECT_EQ(session.validate_relative_path("nested/../outside.dat", error),
              tradutorlinux::compat::detail::RustPathValidationResult::InvalidInput);

    const auto& metrics = session.metrics();
    EXPECT_EQ(metrics.backend, tradutorlinux::compat::PathValidationBackend::Rust);
    EXPECT_EQ(metrics.handle_count, 1U);
    EXPECT_EQ(metrics.checks, 3U);
    EXPECT_EQ(metrics.rejected, 1U);
    EXPECT_FALSE(metrics.infrastructure_error);
}

}  // namespace
