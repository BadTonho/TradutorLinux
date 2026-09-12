#include "tradutorlinux/compat/profile.hpp"
#include "tradutorlinux/compat/rust_profile_parser.hpp"
#include "tradutorlinux/prefix/prefix.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <unistd.h>

namespace tradutorlinux::compat {
namespace {

std::vector<std::byte> bytes(const std::string_view text) {
    std::vector<std::byte> result(text.size());
    if (!text.empty()) std::memcpy(result.data(), text.data(), text.size());
    return result;
}

RustProfileParseResult parse_rust(const std::string_view text,
                                  const std::string_view app_id = "fixture",
                                  const std::string_view sha256 = {},
                                  const std::string_view version = {}) {
    const auto input = bytes(text);
    return parse_profile_rust(input, app_id, sha256, version);
}

class RustProfileParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() /
                ("tl-rust-profile-" + std::to_string(static_cast<unsigned long long>(::getpid())));
        std::filesystem::remove_all(root_);
        ASSERT_TRUE(prefix::initialize_prefix(root_));
    }

    void TearDown() override { std::filesystem::remove_all(root_); }

    std::filesystem::path root_;
};

TEST_F(RustProfileParserTest, ValidProfilesMatchCppModelForAllSchemas) {
    const auto schema1 = parse_rust(
        R"json({"schema":1,"app_id":"fixture","app_sha256":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA","app_version":"1.2.3","files":[]})json",
        "fixture", std::string(64, 'a'), "1.2.3");
    ASSERT_EQ(schema1.status, TL_PROFILE_STATUS_SUCCESS);
    EXPECT_EQ(schema1.profile.schema, 1U);
    EXPECT_EQ(schema1.profile.app_id, "fixture");
    EXPECT_EQ(schema1.profile.app_sha256,
              "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    EXPECT_EQ(schema1.profile.app_version, "1.2.3");
    EXPECT_TRUE(schema1.profile.files.empty());

    const auto schema2 = parse_rust(
        R"json({"schema":2,"app_id":"fixture","files":[],"dlls":[{"module":"COMPAT","source":"compat.dll"}]})json");
    ASSERT_EQ(schema2.status, TL_PROFILE_STATUS_SUCCESS);
    ASSERT_EQ(schema2.profile.dlls.size(), 1U);
    EXPECT_EQ(schema2.profile.dlls[0].module, "compat.dll");
    EXPECT_EQ(schema2.profile.dlls[0].source, std::filesystem::path{"compat.dll"});

    const auto schema3 = parse_rust(
        R"json({"schema":3,"app_id":"fixture","files":[],"dlls":[],"backend":{"kind":"proton","min_version":"11.0"}})json");
    ASSERT_EQ(schema3.status, TL_PROFILE_STATUS_SUCCESS);
    EXPECT_EQ(schema3.profile.backend.kind, BackendKind::Proton);
    EXPECT_TRUE(schema3.profile.backend_declared);
    EXPECT_EQ(schema3.profile.backend.min_version, "11.0");

    const auto schema4 = parse_rust(
        R"json({"schema":4,"app_id":"fixture","files":[],"dlls":[],"backend":{"kind":"native"},"extension":"7zip"})json");
    ASSERT_EQ(schema4.status, TL_PROFILE_STATUS_SUCCESS);
    EXPECT_EQ(schema4.profile.schema, 4U);
    EXPECT_EQ(schema4.profile.extension, "7zip");
    EXPECT_TRUE(schema4.profile.extension_declared);
}

TEST_F(RustProfileParserTest, FileAndDllEntriesPreserveCxxSemantics) {
    const auto result = parse_rust(
        R"json({"schema":2,"app_id":"fixture","files":[{"source":"config.dat","target":"C:\\Program Files\\Fixture\\config.dat"}],"dlls":[{"module":"Compat","source":"shim.dll"}]})json");
    ASSERT_EQ(result.status, TL_PROFILE_STATUS_SUCCESS);
    ASSERT_EQ(result.profile.files.size(), 1U);
    ASSERT_EQ(result.profile.dlls.size(), 1U);
    EXPECT_EQ(result.profile.files[0].source, std::filesystem::path{"config.dat"});
    EXPECT_EQ(result.profile.files[0].target, "C:\\Program Files\\Fixture\\config.dat");
    EXPECT_EQ(result.profile.dlls[0].module, "compat.dll");
}

TEST_F(RustProfileParserTest, InvalidAndUnsupportedProfilesAreStructured) {
    const auto unknown_field = parse_rust(
        R"json({"schema":1,"app_id":"fixture","unknown":true})json");
    EXPECT_EQ(unknown_field.status, TL_PROFILE_STATUS_MALFORMED);
    EXPECT_EQ(unknown_field.error.code, TL_PROFILE_ERROR_JSON_SYNTAX);

    const auto unknown_schema = parse_rust(R"json({"schema":9,"app_id":"fixture"})json");
    EXPECT_EQ(unknown_schema.status, TL_PROFILE_STATUS_UNSUPPORTED_FORMAT);
    EXPECT_EQ(unknown_schema.error.code, TL_PROFILE_ERROR_SCHEMA);

    const auto mismatch = parse_rust(R"json({"schema":1,"app_id":"other"})json");
    EXPECT_EQ(mismatch.status, TL_PROFILE_STATUS_MALFORMED);
    EXPECT_EQ(mismatch.error.code, TL_PROFILE_ERROR_IDENTITY);

    const auto traversal = parse_rust(
        R"json({"schema":1,"app_id":"fixture","files":[{"source":"../x","target":"C:\\x"}]})json");
    EXPECT_EQ(traversal.status, TL_PROFILE_STATUS_MALFORMED);
    EXPECT_EQ(traversal.error.code, TL_PROFILE_ERROR_PATH);

    const auto extension_on_schema3 = parse_rust(
        R"json({"schema":3,"app_id":"fixture","files":[],"extension":"7zip"})json");
    EXPECT_EQ(extension_on_schema3.status, TL_PROFILE_STATUS_MALFORMED);
}

TEST_F(RustProfileParserTest, SizeFillAndWireDecoderHonorBufferContract) {
    const std::string input_text = R"json({"schema":1,"app_id":"fixture"})json";
    const auto input = bytes(input_text);
    const tl_profile_identity_v1 identity{
        reinterpret_cast<const std::uint8_t*>("fixture"), 7, nullptr, 0, nullptr, 0};
    std::uint64_t required = 0;
    std::uint64_t error_required = 0;
    tl_profile_error_v1 error{};
    std::array<char, 128> message{};
    ASSERT_EQ(tl_profile_parse_v1_size(
                  reinterpret_cast<const std::uint8_t*>(input.data()), input.size(), &identity,
                  &required, &error, message.data(), message.size(), &error_required),
              TL_PROFILE_STATUS_SUCCESS);
    ASSERT_GT(required, static_cast<std::uint64_t>(TL_PROFILE_WIRE_HEADER_SIZE));

    std::vector<std::uint8_t> guarded(static_cast<std::size_t>(required) + 32U, 0xa5U);
    ASSERT_EQ(tl_profile_parse_v1_fill(
                  reinterpret_cast<const std::uint8_t*>(input.data()), input.size(), &identity,
                  guarded.data() + 16U, required - 1U, &required, &error, message.data(),
                  message.size(), &error_required),
              TL_PROFILE_STATUS_BUFFER_TOO_SMALL);
    for (const auto value : guarded) EXPECT_EQ(value, 0xa5U);

    ASSERT_EQ(tl_profile_parse_v1_fill(
                  reinterpret_cast<const std::uint8_t*>(input.data()), input.size(), &identity,
                  guarded.data() + 16U, required + 16U, &required, &error, message.data(),
                  message.size(), &error_required),
              TL_PROFILE_STATUS_SUCCESS);
    Profile decoded;
    tl_profile_error_v1 decode_error{};
    std::string decode_message;
    ASSERT_TRUE(decode_tlpr_v1({guarded.data() + 16U, static_cast<std::size_t>(required)}, decoded,
                               decode_error, decode_message))
        << decode_message;
    EXPECT_EQ(decoded.app_id, "fixture");

    std::array<std::uint8_t, TL_PROFILE_WIRE_HEADER_SIZE> invalid_wire{};
    EXPECT_FALSE(decode_tlpr_v1(invalid_wire, decoded, decode_error, decode_message));
    EXPECT_EQ(decode_error.code, TL_PROFILE_ERROR_WIRE_FORMAT);
}

TEST_F(RustProfileParserTest, DifferentialResultMatchesCppLoaderWithPhysicalChecks) {
    const auto paths = prefix::get_environment_paths(root_);
    std::ofstream source(paths.compat_files_dir / "fixture.dat", std::ios::binary);
    ASSERT_TRUE(source);
    source << "fixture\n";
    source.close();
    const std::string profile_text =
        R"json({"schema":1,"app_id":"fixture","files":[{"source":"fixture.dat","target":"C:\\Fixture\\injected.dat"}]})json";
    std::ofstream profile_file(profile_path(root_), std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(profile_file);
    profile_file << profile_text;
    profile_file.close();

    const auto cpp = load_profile(root_, "fixture");
    const auto rust = parse_rust(profile_text);
    ASSERT_EQ(cpp.status, ProfileStatus::Loaded);
    ASSERT_EQ(rust.status, TL_PROFILE_STATUS_SUCCESS);
    EXPECT_EQ(rust.profile.schema, cpp.profile.schema);
    EXPECT_EQ(rust.profile.app_id, cpp.profile.app_id);
    ASSERT_EQ(rust.profile.files.size(), cpp.profile.files.size());
    EXPECT_EQ(rust.profile.files[0].source, cpp.profile.files[0].source);
    EXPECT_EQ(rust.profile.files[0].target, cpp.profile.files[0].target);
}

TEST_F(RustProfileParserTest, CallsAreStatelessAndCanRunConcurrently) {
    const std::string input = R"json({"schema":1,"app_id":"fixture"})json";
    std::array<bool, 8> results{};
    std::array<std::thread, 8> workers{};
    for (std::size_t index = 0; index < workers.size(); ++index) {
        workers[index] = std::thread([&, index] {
            results[index] = parse_rust(input).status == TL_PROFILE_STATUS_SUCCESS;
        });
    }
    for (auto& worker : workers) worker.join();
    for (const bool result : results) EXPECT_TRUE(result);
}

}  // namespace
}  // namespace tradutorlinux::compat
