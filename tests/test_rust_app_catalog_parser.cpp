#include "tradutorlinux/catalog/app_catalog.hpp"
#include "tradutorlinux/catalog/rust_app_catalog_parser.hpp"
#include "tradutorlinux/diagnostics/trace.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <unistd.h>

namespace tradutorlinux::catalog {
namespace {

[[nodiscard]] std::vector<std::byte> as_bytes(const std::string_view input) {
    std::vector<std::byte> bytes(input.size());
    std::transform(input.begin(), input.end(), bytes.begin(), [](const char value) {
        return static_cast<std::byte>(static_cast<unsigned char>(value));
    });
    return bytes;
}

[[nodiscard]] std::uint64_t read_le64(const std::vector<std::uint8_t>& input,
                                       const std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(input[offset + index]) << (index * 8U);
    }
    return value;
}

void put_le64(std::vector<std::uint8_t>& output, const std::size_t offset,
              const std::uint64_t value) {
    for (std::size_t index = 0; index < 8U; ++index) {
        output[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

struct WireResult {
    tl_app_catalog_status_t status{};
    std::uint64_t required{};
    tl_app_catalog_error_v1 error{};
    std::string message;
    std::vector<std::uint8_t> wire;
};

[[nodiscard]] WireResult make_wire(const std::string_view text) {
    const auto input = as_bytes(text);
    WireResult result;
    std::array<char, 512> message{};
    std::uint64_t error_required = 0;
    result.status = tl_app_catalog_parse_v1_size(
        reinterpret_cast<const std::uint8_t*>(input.data()), input.size(), &result.required,
        &result.error, message.data(), message.size(), &error_required);
    result.message.assign(message.data());
    if (result.status != TL_APP_CATALOG_STATUS_SUCCESS) return result;

    result.wire.resize(static_cast<std::size_t>(result.required));
    message.fill('\0');
    error_required = 0;
    result.status = tl_app_catalog_parse_v1_fill(
        reinterpret_cast<const std::uint8_t*>(input.data()), input.size(), result.wire.data(),
        result.wire.size(), &result.required, &result.error, message.data(), message.size(),
        &error_required);
    result.message.assign(message.data());
    return result;
}

const std::string kFullCatalog = R"json({
  "version": 1,
  "apps": [
    {
      "id": "editor",
      "name": "Editor \u00e9",
      "executable_path": "bin/editor.exe",
      "prefix_path": "/tmp/editor-prefix",
      "icon_path": "icons/editor.png",
      "working_directory": "C:\\work",
      "app_sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      "app_version": "1.2.3",
      "created_at": "2026-09-06T00:00:00Z",
      "cpu_limit_seconds": 12,
      "memory_limit_mib": 256,
      "args": ["--safe", "--name=\u00e1", "\u0000binary"]
    },
    {
      "id": "viewer-2",
      "name": "Viewer",
      "executable_path": "viewer.exe",
      "prefix_path": "",
      "icon_path": "",
      "working_directory": "",
      "app_sha256": "",
      "app_version": "",
      "created_at": "",
      "cpu_limit_seconds": 0,
      "memory_limit_mib": 0,
      "args": ["--safe"]
    }
  ]
})json";

void expect_entries_equal(const std::vector<AppEntry>& expected,
                          const std::vector<AppEntry>& actual) {
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const AppEntry& lhs = expected[index];
        const AppEntry& rhs = actual[index];
        EXPECT_EQ(rhs.id, lhs.id);
        EXPECT_EQ(rhs.name, lhs.name);
        EXPECT_EQ(rhs.executable_path, lhs.executable_path);
        EXPECT_EQ(rhs.prefix_path, lhs.prefix_path);
        EXPECT_EQ(rhs.icon_path, lhs.icon_path);
        EXPECT_EQ(rhs.working_directory, lhs.working_directory);
        EXPECT_EQ(rhs.app_sha256, lhs.app_sha256);
        EXPECT_EQ(rhs.app_version, lhs.app_version);
        EXPECT_EQ(rhs.created_at, lhs.created_at);
        EXPECT_EQ(rhs.cpu_limit_seconds, lhs.cpu_limit_seconds);
        EXPECT_EQ(rhs.memory_limit_mib, lhs.memory_limit_mib);
        EXPECT_EQ(rhs.args, lhs.args);
    }
}

[[nodiscard]] std::vector<AppEntry> cpp_entries(const std::string_view text) {
    const auto path = std::filesystem::temp_directory_path() /
                      ("tl-rust-app-catalog-" +
                       std::to_string(static_cast<unsigned long long>(::getpid())) + ".json");
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) return {};
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
    AppCatalog catalog;
    const bool loaded = catalog.load_from_file(path);
    std::filesystem::remove(path);
    if (!loaded) return {};
    return catalog.list_apps();
}

[[nodiscard]] std::filesystem::path write_catalog_file(const std::string_view text,
                                                        const std::string_view suffix) {
    const auto path = std::filesystem::temp_directory_path() /
                      ("tl-rust-app-catalog-" +
                       std::to_string(static_cast<unsigned long long>(::getpid())) + "-" +
                       std::string{suffix} + ".json");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return {};
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return path;
}

TEST(RustAppCatalogParserTest, ValidCatalogMatchesCppSemantics) {
    const RustAppCatalogParseResult rust = parse_app_catalog_rust(as_bytes(kFullCatalog));
    ASSERT_EQ(rust.status, TL_APP_CATALOG_STATUS_SUCCESS)
        << rust.error_message << " code=" << rust.error.code << " phase=" << rust.error.phase
        << " offset=" << rust.error.input_offset << " detail=" << rust.error.detail_value;
    ASSERT_FALSE(rust.internal_failure);
    expect_entries_equal(cpp_entries(kFullCatalog), rust.apps);
    ASSERT_EQ(rust.apps.size(), 2U);
    EXPECT_EQ(rust.apps[0].args.back().size(), 7U);
    EXPECT_EQ(static_cast<unsigned char>(rust.apps[0].args.back()[0]), 0U);
}

TEST(RustAppCatalogParserTest, PromotedLoadPublishesDecodedCatalog) {
    const auto path = write_catalog_file(kFullCatalog, "promoted");
    ASSERT_FALSE(path.empty());

    AppCatalog catalog;
    ASSERT_TRUE(catalog.load_from_file(path));
    const auto loaded = catalog.list_apps();
    ASSERT_EQ(loaded.size(), 2U);
    EXPECT_EQ(loaded[0].id, "editor");
    EXPECT_EQ(loaded[0].name, "Editor é");
    EXPECT_EQ(loaded[0].executable_path, "bin/editor.exe");
    EXPECT_EQ(loaded[0].args,
              (std::vector<std::string>{"--safe", "--name=á", std::string{"\0binary", 7}}));
    EXPECT_EQ(loaded[0].cpu_limit_seconds, 12U);
    EXPECT_EQ(loaded[0].memory_limit_mib, 256U);
    EXPECT_EQ(loaded[1].id, "viewer-2");
    EXPECT_TRUE(loaded[1].prefix_path.empty());
    EXPECT_TRUE(loaded[1].args == std::vector<std::string>{"--safe"});
    std::filesystem::remove(path);
}

TEST(RustAppCatalogParserTest, PromotedLoadRejectsCppPermissiveInputAtomically) {
    const auto path = write_catalog_file(
        R"json({"apps":[{"id":"legacy","executable_path":"legacy.exe"}]})json",
        "no-fallback");
    ASSERT_FALSE(path.empty());

    AppCatalog catalog;
    AppEntry sentinel;
    sentinel.id = "sentinel";
    sentinel.executable_path = "sentinel.exe";
    ASSERT_TRUE(catalog.add_app(sentinel));
    EXPECT_FALSE(catalog.load_from_file(path));
    EXPECT_TRUE(catalog.list_apps().empty());
    std::filesystem::remove(path);
}

TEST(RustAppCatalogParserTest, PromotedLoadRejectsInputAboveWireLimit) {
    const auto path = std::filesystem::temp_directory_path() /
                      ("tl-rust-app-catalog-" +
                       std::to_string(static_cast<unsigned long long>(::getpid())) +
                       "-limit.json");
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output);
        const std::string block(1024U * 1024U, 'x');
        for (std::size_t index = 0; index < 4U; ++index) {
            output.write(block.data(), static_cast<std::streamsize>(block.size()));
        }
        output.put('x');
    }

    AppCatalog catalog;
    EXPECT_FALSE(catalog.load_from_file(path));
    EXPECT_TRUE(catalog.list_apps().empty());
    std::filesystem::remove(path);
}

TEST(RustAppCatalogParserTest, CatalogParseTraceIsOptInAndStructured) {
    const auto valid_path = write_catalog_file(R"json({"version":1,"apps":[]})json", "trace");
    const auto invalid_path = write_catalog_file(R"json({"version":1,"apps":[)json", "trace-invalid");
    const auto missing_path = std::filesystem::temp_directory_path() /
                              ("tl-rust-app-catalog-" +
                               std::to_string(static_cast<unsigned long long>(::getpid())) +
                               "-trace-missing.json");
    std::filesystem::remove(missing_path);

    diagnostics::set_trace_requested(true);
    diagnostics::configure_trace_filter({diagnostics::TraceComponent::Runtime});
    testing::internal::CaptureStderr();
    AppCatalog valid;
    ASSERT_TRUE(valid.load_from_file(valid_path));
    const std::string valid_trace = testing::internal::GetCapturedStderr();
    EXPECT_NE(valid_trace.find("catalog-parse"), std::string::npos);
    EXPECT_NE(valid_trace.find("backend=\"rust\""), std::string::npos);
    EXPECT_NE(valid_trace.find("parser-status=\"success\""), std::string::npos);

    testing::internal::CaptureStderr();
    AppCatalog invalid;
    EXPECT_FALSE(invalid.load_from_file(invalid_path));
    const std::string invalid_trace = testing::internal::GetCapturedStderr();
    EXPECT_NE(invalid_trace.find("parser-status=\"malformed\""), std::string::npos);
    for (const std::string_view field : {"code=\"", "phase=\"", "input-offset=\"",
                                         "detail-value=\""}) {
        EXPECT_NE(invalid_trace.find(field), std::string::npos);
    }

    testing::internal::CaptureStderr();
    AppCatalog missing;
    EXPECT_FALSE(missing.load_from_file(missing_path));
    const std::string missing_trace = testing::internal::GetCapturedStderr();
    EXPECT_EQ(missing_trace.find("catalog-parse"), std::string::npos);

    diagnostics::set_trace_requested(false);
    diagnostics::configure_trace_all();
    std::filesystem::remove(valid_path);
    std::filesystem::remove(invalid_path);
}

TEST(RustAppCatalogParserTest, EmptyCatalogUsesCanonicalEmptyDescriptors) {
    const std::string text = R"json({"version":1,"apps":[]})json";
    const WireResult wire = make_wire(text);
    ASSERT_EQ(wire.status, TL_APP_CATALOG_STATUS_SUCCESS);
    ASSERT_EQ(wire.wire.size(), TL_APP_CATALOG_WIRE_HEADER_SIZE +
                                  TL_APP_CATALOG_WIRE_INFO_STRIDE);

    const std::size_t apps_descriptor = TL_APP_CATALOG_WIRE_TABLE_DESCRIPTOR_OFFSET +
                                         TL_APP_CATALOG_WIRE_TABLE_APPS *
                                             TL_APP_CATALOG_WIRE_TABLE_DESCRIPTOR_SIZE;
    EXPECT_EQ(read_le64(wire.wire, apps_descriptor), 0U);
    EXPECT_EQ(read_le64(wire.wire, apps_descriptor + 8U), 0U);
    EXPECT_EQ(read_le64(wire.wire, apps_descriptor + 16U),
              static_cast<std::uint64_t>(TL_APP_CATALOG_WIRE_APP_STRIDE));

    std::vector<AppEntry> decoded;
    tl_app_catalog_error_v1 error{};
    std::string message;
    ASSERT_TRUE(decode_tlac_v1(wire.wire, decoded, error, message)) << message;
    EXPECT_TRUE(decoded.empty());
}

TEST(RustAppCatalogParserTest, FillPreservesSentinelsWhenOutputIsTooSmall) {
    const auto input = as_bytes(kFullCatalog);
    std::uint64_t required = 0;
    tl_app_catalog_error_v1 error{};
    std::array<char, 512> message{};
    std::uint64_t error_required = 0;
    ASSERT_EQ(tl_app_catalog_parse_v1_size(
                  reinterpret_cast<const std::uint8_t*>(input.data()), input.size(), &required,
                  &error, message.data(), message.size(), &error_required),
              TL_APP_CATALOG_STATUS_SUCCESS);

    std::vector<std::uint8_t> guarded(static_cast<std::size_t>(required) + 32U, 0xa5U);
    std::uint64_t fill_required = 0;
    ASSERT_EQ(tl_app_catalog_parse_v1_fill(
                  reinterpret_cast<const std::uint8_t*>(input.data()), input.size(),
                  guarded.data() + 16U, required - 1U, &fill_required, &error, message.data(),
                  message.size(), &error_required),
              TL_APP_CATALOG_STATUS_BUFFER_TOO_SMALL);
    EXPECT_EQ(fill_required, required);
    EXPECT_TRUE(std::all_of(guarded.begin(), guarded.end(),
                            [](const std::uint8_t value) { return value == 0xa5U; }));

    ASSERT_EQ(tl_app_catalog_parse_v1_fill(
                  reinterpret_cast<const std::uint8_t*>(input.data()), input.size(),
                  guarded.data() + 16U, required, &fill_required, &error, message.data(),
                  message.size(), &error_required),
              TL_APP_CATALOG_STATUS_SUCCESS);
    EXPECT_EQ(guarded.front(), 0xa5U);
    EXPECT_EQ(guarded.back(), 0xa5U);
}

TEST(RustAppCatalogParserTest, ErrorMessagesAreCallerOwnedAndNulTerminated) {
    const auto input = as_bytes(R"json({"version":1,"apps":[{"id":"x)json");
    std::uint64_t required = 0;
    tl_app_catalog_error_v1 error{};
    std::array<char, 1> short_message{'x'};
    std::uint64_t error_required = 0;
    EXPECT_EQ(tl_app_catalog_parse_v1_size(
                  reinterpret_cast<const std::uint8_t*>(input.data()), input.size(), &required,
                  &error, short_message.data(), short_message.size(), &error_required),
              TL_APP_CATALOG_STATUS_BUFFER_TOO_SMALL);
    EXPECT_GT(error_required, short_message.size());
    EXPECT_EQ(short_message[0], '\0');

    std::vector<char> message(static_cast<std::size_t>(error_required), '\0');
    EXPECT_EQ(tl_app_catalog_parse_v1_size(
                  reinterpret_cast<const std::uint8_t*>(input.data()), input.size(), &required,
                  &error, message.data(), message.size(), &error_required),
              TL_APP_CATALOG_STATUS_MALFORMED);
    EXPECT_EQ(message.back(), '\0');
    EXPECT_NE(message.front(), '\0');
    EXPECT_EQ(error.code, TL_APP_CATALOG_ERROR_JSON_SYNTAX);
}

TEST(RustAppCatalogParserTest, InputLimitIsReportedBeforeDereferencingInput) {
    std::vector<std::byte> oversized(static_cast<std::size_t>(
        TL_APP_CATALOG_LIMIT_MAX_INPUT_BYTES + 1U));
    const RustAppCatalogParseResult result = parse_app_catalog_rust(oversized);
    EXPECT_EQ(result.status, TL_APP_CATALOG_STATUS_INPUT_TOO_LARGE);
    EXPECT_FALSE(result.internal_failure);
    EXPECT_EQ(result.error.code, TL_APP_CATALOG_ERROR_INPUT_TOO_LARGE);
    EXPECT_TRUE(result.apps.empty());
}

TEST(RustAppCatalogParserTest, InvalidJsonIsAtomicAndStructured) {
    const std::array<std::string_view, 7> invalid = {
        R"json({"version":1,"apps":[)json",
        R"json({"version":1,"apps":[],"extra":0})json",
        R"json({"version":1,"version":1,"apps":[]})json",
        R"json({"version":1,"apps":[{"id":"x","executable_path":"x",}]})json",
        R"json({"version":1,"apps":[{"id":"bad/id","executable_path":"x"}]})json",
        R"json({"version":1,"apps":[{"id":"x","executable_path":"x"},{"id":"x","executable_path":"y"}]})json",
        R"json({"version":1,"apps":[]} trailing)json",
    };
    for (const std::string_view text : invalid) {
        const RustAppCatalogParseResult result = parse_app_catalog_rust(as_bytes(text));
        EXPECT_NE(result.status, TL_APP_CATALOG_STATUS_SUCCESS) << text;
        EXPECT_FALSE(result.internal_failure) << text;
        EXPECT_TRUE(result.apps.empty()) << text;
        EXPECT_NE(result.error.code, TL_APP_CATALOG_ERROR_NONE) << text;
        EXPECT_FALSE(result.error_message.empty()) << text;
    }
    const RustAppCatalogParseResult unsupported =
        parse_app_catalog_rust(as_bytes(R"json({"version":2,"apps":[]})json"));
    EXPECT_EQ(unsupported.status, TL_APP_CATALOG_STATUS_UNSUPPORTED_FORMAT);
    EXPECT_EQ(unsupported.error.code, TL_APP_CATALOG_ERROR_SCHEMA);
}

TEST(RustAppCatalogParserTest, WireMutationsAreRejectedWithoutPartialOutput) {
    const WireResult original = make_wire(kFullCatalog);
    ASSERT_EQ(original.status, TL_APP_CATALOG_STATUS_SUCCESS);
    const std::size_t apps_descriptor = TL_APP_CATALOG_WIRE_TABLE_DESCRIPTOR_OFFSET +
                                         TL_APP_CATALOG_WIRE_TABLE_APPS *
                                             TL_APP_CATALOG_WIRE_TABLE_DESCRIPTOR_SIZE;
    const std::size_t apps_offset = static_cast<std::size_t>(read_le64(original.wire,
                                                                        apps_descriptor));

    const auto expect_rejected = [](std::vector<std::uint8_t> mutant) {
        std::vector<AppEntry> decoded(1);
        decoded[0].id = "sentinel";
        tl_app_catalog_error_v1 error{};
        std::string message;
        EXPECT_FALSE(decode_tlac_v1(mutant, decoded, error, message));
        EXPECT_EQ(error.code, TL_APP_CATALOG_ERROR_WIRE_FORMAT);
        EXPECT_EQ(decoded.size(), 1U);
        EXPECT_EQ(decoded[0].id, "sentinel");
    };

    auto mutant = original.wire;
    mutant[0] = 'X';
    expect_rejected(mutant);
    mutant = original.wire;
    mutant[4] = 2;
    expect_rejected(mutant);
    mutant = original.wire;
    put_le64(mutant, 12, original.wire.size() + 1U);
    expect_rejected(mutant);
    mutant = original.wire;
    put_le64(mutant, apps_descriptor, TL_APP_CATALOG_WIRE_HEADER_SIZE);
    expect_rejected(mutant);
    mutant = original.wire;
    mutant[apps_offset + TL_APP_CATALOG_WIRE_APP_RESERVED_OFFSET] = 1U;
    expect_rejected(mutant);
    mutant = original.wire;
    put_le64(mutant, apps_offset + TL_APP_CATALOG_WIRE_APP_ID_OFFSET, 1U);
    expect_rejected(mutant);
    expect_rejected(std::vector<std::uint8_t>(original.wire.begin(), original.wire.end() - 1));
}

TEST(RustAppCatalogParserTest, CallsAreStatelessAndConcurrent) {
    constexpr std::size_t kWorkers = 8U;
    std::array<std::atomic<bool>, kWorkers> results{};
    for (auto& result : results) result.store(false);
    std::array<std::thread, kWorkers> workers{};
    for (std::size_t index = 0; index < kWorkers; ++index) {
        workers[index] = std::thread([&, index] {
            results[index].store(parse_app_catalog_rust(as_bytes(kFullCatalog)).status ==
                                 TL_APP_CATALOG_STATUS_SUCCESS);
        });
    }
    for (auto& worker : workers) worker.join();
    for (const auto& result : results) EXPECT_TRUE(result.load());
}

}  // namespace
}  // namespace tradutorlinux::catalog
