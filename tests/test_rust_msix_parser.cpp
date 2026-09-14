#include "test_win32_common.hpp"

#include "tradutorlinux/package/rust_msix_parser.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <zlib.h>

namespace tradutorlinux::package {
namespace {

struct ZipEntrySpec {
    std::string name;
    std::string data;
    std::uint16_t method = 0;
    std::uint16_t flags = 0;
    std::uint16_t version_made_by = 20;
    std::uint32_t external_attributes = 0;
    bool force_zip64 = false;
};

struct ZipImage {
    std::vector<std::uint8_t> bytes;
    std::vector<std::size_t> central_offsets;
    std::vector<std::size_t> local_offsets;
};

void append_le16(std::vector<std::uint8_t>& output, const std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void append_le32(std::vector<std::uint8_t>& output, const std::uint32_t value) {
    append_le16(output, static_cast<std::uint16_t>(value));
    append_le16(output, static_cast<std::uint16_t>(value >> 16U));
}

void append_le64(std::vector<std::uint8_t>& output, const std::uint64_t value) {
    append_le32(output, static_cast<std::uint32_t>(value));
    append_le32(output, static_cast<std::uint32_t>(value >> 32U));
}

void put_le16(std::vector<std::uint8_t>& output, const std::size_t offset,
              const std::uint16_t value) {
    ASSERT_LE(offset + 2U, output.size());
    output[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    output[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void put_le32(std::vector<std::uint8_t>& output, const std::size_t offset,
              const std::uint32_t value) {
    ASSERT_LE(offset + 4U, output.size());
    for (std::size_t index = 0; index < 4U; ++index) {
        output[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

void put_le64(std::vector<std::uint8_t>& output, const std::size_t offset,
              const std::uint64_t value) {
    ASSERT_LE(offset + 8U, output.size());
    for (std::size_t index = 0; index < 8U; ++index) {
        output[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

[[nodiscard]] std::uint32_t read_le32(const std::vector<std::uint8_t>& input,
                                       const std::size_t offset) {
    return static_cast<std::uint32_t>(input[offset]) |
           (static_cast<std::uint32_t>(input[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(input[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(input[offset + 3U]) << 24U);
}

[[nodiscard]] std::uint16_t read_le16(const std::vector<std::uint8_t>& input,
                                       const std::size_t offset) {
    const std::uint32_t value = static_cast<std::uint32_t>(input[offset]) |
                                (static_cast<std::uint32_t>(input[offset + 1U]) << 8U);
    return static_cast<std::uint16_t>(value);
}

[[nodiscard]] std::uint64_t read_le64(const std::vector<std::uint8_t>& input,
                                       const std::size_t offset) {
    std::uint64_t result = 0;
    for (std::size_t index = 0; index < 8U; ++index) {
        result |= static_cast<std::uint64_t>(input[offset + index]) << (index * 8U);
    }
    return result;
}

void append_string(std::vector<std::uint8_t>& output, const std::string_view value) {
    output.insert(output.end(), value.begin(), value.end());
}

[[nodiscard]] std::uint32_t crc32_of(const std::string_view data) {
    return static_cast<std::uint32_t>(crc32(
        crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(data.data()),
        static_cast<uInt>(data.size())));
}

[[nodiscard]] ZipImage make_zip(const std::vector<ZipEntrySpec>& entries) {
    ZipImage image;
    image.local_offsets.reserve(entries.size());
    image.central_offsets.reserve(entries.size());

    for (const ZipEntrySpec& entry : entries) {
        const std::vector<std::byte> compressed_bytes =
            entry.method == 8 ? raw_deflate(entry.data) : std::vector<std::byte>{};
        const std::vector<std::uint8_t> compressed = [&]() {
            if (entry.method == 8) {
                std::vector<std::uint8_t> result;
                result.reserve(compressed_bytes.size());
                for (const std::byte byte : compressed_bytes) {
                    result.push_back(static_cast<std::uint8_t>(byte));
                }
                return result;
            }
            return std::vector<std::uint8_t>(entry.data.begin(), entry.data.end());
        }();
        const std::uint32_t checksum = crc32_of(entry.data);
        const std::uint16_t flags = entry.flags;

        image.local_offsets.push_back(image.bytes.size());
        append_le32(image.bytes, 0x04034B50U);
        append_le16(image.bytes, 20);
        append_le16(image.bytes, flags);
        append_le16(image.bytes, entry.method);
        append_le16(image.bytes, 0);
        append_le16(image.bytes, 0);
        append_le32(image.bytes, (flags & 0x0008U) != 0U ? 0U : checksum);
        append_le32(image.bytes, (flags & 0x0008U) != 0U
                                      ? 0U
                                      : static_cast<std::uint32_t>(compressed.size()));
        append_le32(image.bytes, (flags & 0x0008U) != 0U
                                      ? 0U
                                      : static_cast<std::uint32_t>(entry.data.size()));
        append_le16(image.bytes, static_cast<std::uint16_t>(entry.name.size()));
        append_le16(image.bytes, 0);
        append_string(image.bytes, entry.name);
        image.bytes.insert(image.bytes.end(), compressed.begin(), compressed.end());
        if ((flags & 0x0008U) != 0U) {
            append_le32(image.bytes, 0x08074B50U);
            append_le32(image.bytes, checksum);
            append_le32(image.bytes, static_cast<std::uint32_t>(compressed.size()));
            append_le32(image.bytes, static_cast<std::uint32_t>(entry.data.size()));
        }
    }

    const std::size_t central_start = image.bytes.size();
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const ZipEntrySpec& entry = entries[index];
        const std::vector<std::byte> compressed_bytes =
            entry.method == 8 ? raw_deflate(entry.data) : std::vector<std::byte>{};
        const std::size_t compressed_size = entry.method == 8
                                                ? compressed_bytes.size()
                                                : entry.data.size();
        const std::uint16_t flags = entry.flags;
        image.central_offsets.push_back(image.bytes.size());
        append_le32(image.bytes, 0x02014B50U);
        append_le16(image.bytes, entry.version_made_by);
        append_le16(image.bytes, 20);
        append_le16(image.bytes, flags);
        append_le16(image.bytes, entry.method);
        append_le16(image.bytes, 0);
        append_le16(image.bytes, 0);
        append_le32(image.bytes, crc32_of(entry.data));
        append_le32(image.bytes, entry.force_zip64 ? 0xFFFFFFFFU
                                                   : static_cast<std::uint32_t>(compressed_size));
        append_le32(image.bytes, entry.force_zip64 ? 0xFFFFFFFFU
                                                   : static_cast<std::uint32_t>(entry.data.size()));
        append_le16(image.bytes, static_cast<std::uint16_t>(entry.name.size()));
        append_le16(image.bytes, entry.force_zip64 ? 28U : 0U);
        append_le16(image.bytes, 0);
        append_le16(image.bytes, 0);
        append_le16(image.bytes, 0);
        append_le32(image.bytes, entry.external_attributes);
        append_le32(image.bytes, entry.force_zip64
                                  ? 0xFFFFFFFFU
                                  : static_cast<std::uint32_t>(image.local_offsets[index]));
        append_string(image.bytes, entry.name);
        if (entry.force_zip64) {
            append_le16(image.bytes, 0x0001U);
            append_le16(image.bytes, 24U);
            append_le64(image.bytes, static_cast<std::uint64_t>(entry.data.size()));
            append_le64(image.bytes, static_cast<std::uint64_t>(compressed_size));
            append_le64(image.bytes, static_cast<std::uint64_t>(image.local_offsets[index]));
        }
    }
    const std::uint32_t central_size = static_cast<std::uint32_t>(image.bytes.size() - central_start);
    append_le32(image.bytes, 0x06054B50U);
    append_le16(image.bytes, 0);
    append_le16(image.bytes, 0);
    append_le16(image.bytes, static_cast<std::uint16_t>(entries.size()));
    append_le16(image.bytes, static_cast<std::uint16_t>(entries.size()));
    append_le32(image.bytes, central_size);
    append_le32(image.bytes, static_cast<std::uint32_t>(central_start));
    append_le16(image.bytes, 0);
    return image;
}

[[nodiscard]] ZipImage make_zip64(ZipImage image) {
    const std::size_t eocd = image.bytes.size() - 22U;
    const std::uint32_t central_size = read_le32(image.bytes, eocd + 12U);
    const std::uint32_t central_offset = read_le32(image.bytes, eocd + 16U);
    const std::uint64_t zip64_offset = eocd;
    const auto eocd_difference = static_cast<std::vector<std::uint8_t>::difference_type>(eocd);
    std::vector<std::uint8_t> bytes(image.bytes.begin(), image.bytes.begin() + eocd_difference);
    append_le32(bytes, 0x06064B50U);
    append_le64(bytes, 44U);
    append_le16(bytes, 45U);
    append_le16(bytes, 45U);
    append_le32(bytes, 0U);
    append_le32(bytes, 0U);
    append_le64(bytes, read_le16(image.bytes, eocd + 8U));
    append_le64(bytes, read_le16(image.bytes, eocd + 10U));
    append_le64(bytes, central_size);
    append_le64(bytes, central_offset);
    append_le32(bytes, 0x07064B50U);
    append_le32(bytes, 0U);
    append_le64(bytes, zip64_offset);
    append_le32(bytes, 1U);
    append_le32(bytes, 0x06054B50U);
    append_le16(bytes, 0U);
    append_le16(bytes, 0U);
    append_le16(bytes, 0xFFFFU);
    append_le16(bytes, 0xFFFFU);
    append_le32(bytes, 0xFFFFFFFFU);
    append_le32(bytes, 0xFFFFFFFFU);
    append_le16(bytes, 0U);
    image.bytes = std::move(bytes);
    return image;
}

[[nodiscard]] std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), {});
}

void expect_semantically_equal(const AppxPackageInfo& expected,
                               const AppxPackageInfo& actual) {
    EXPECT_EQ(actual.package_name, expected.package_name);
    EXPECT_EQ(actual.publisher, expected.publisher);
    EXPECT_EQ(actual.version, expected.version);
    EXPECT_EQ(actual.main_executable, expected.main_executable);
    ASSERT_EQ(actual.applications.size(), expected.applications.size());
    for (std::size_t index = 0; index < expected.applications.size(); ++index) {
        EXPECT_EQ(actual.applications[index].id, expected.applications[index].id);
        EXPECT_EQ(actual.applications[index].executable,
                  expected.applications[index].executable);
        EXPECT_EQ(actual.applications[index].display_name,
                  expected.applications[index].display_name);
        EXPECT_EQ(actual.applications[index].entry_point,
                  expected.applications[index].entry_point);
    }
}

const char kManifest[] = "\xEF\xBB\xBF" R"(<?xml version="1.0"?>
<!-- ignored -->
<Package xmlns="urn:appx" xmlns:uap="urn:uap">
  <Identity Name="Example.App" Publisher="CN=Example &amp; Co" Version="1.2.3.4" />
  <Applications>
    <![CDATA[ignored application markup]]>
    <Application Id="App" Executable="bin\Example.exe" EntryPoint="Windows.FullTrustApplication">
      <uap:VisualElements DisplayName="Example &#x41;pp" />
    </Application>
  </Applications>
</Package>)";

const char kManifestWithoutExecutable[] = R"(<Package>
  <Identity Name="NoExecutable" Publisher="CN=Example" Version="1.0.0.0" />
  <Applications><Application Id="OnlyId" EntryPoint="Windows.FullTrustApplication" /></Applications>
</Package>)";

TEST(RustMsixParserTest, StoredAndDeflatedPackagesMatchCppSemantics) {
    TempDirFixture fixture;
    for (const std::uint16_t method : {std::uint16_t{0}, std::uint16_t{8}}) {
        const ZipImage image = make_zip({
            {"AppxManifest.xml", kManifest, method,
             static_cast<std::uint16_t>(method == 8 ? 0x0008U : 0U)},
            {"bin/Example.exe", "MZ", 0, 0},
        });
        const std::filesystem::path path = fixture.path(method == 0 ? "stored.msix" : "deflated.msix");
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.good());
        output.write(reinterpret_cast<const char*>(image.bytes.data()),
                     static_cast<std::streamsize>(image.bytes.size()));
        ASSERT_TRUE(output.good());
        output.close();

        const auto cpp_info = inspect_msix_package(path);
        ASSERT_TRUE(cpp_info.has_value());
        const RustMsixParseResult rust_result = parse_msix_rust(read_file(path));
        ASSERT_EQ(rust_result.status, TL_MSIX_STATUS_SUCCESS);
        ASSERT_FALSE(rust_result.internal_failure);
        expect_semantically_equal(*cpp_info, rust_result.info);
    }
}

TEST(RustMsixParserTest, Zip64SingleDiskAndEntryExtrasMatchCppSemantics) {
    TempDirFixture fixture;
    const ZipImage image = make_zip64(make_zip({
        {"AppxManifest.xml", kManifest, 8, 0, 45, 0, true},
        {"bin/Example.exe", "MZ", 0, 0},
    }));
    const std::filesystem::path path = fixture.path("zip64.msix");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good());
    output.write(reinterpret_cast<const char*>(image.bytes.data()),
                 static_cast<std::streamsize>(image.bytes.size()));
    output.close();

    const RustMsixParseResult rust = parse_msix_rust(image.bytes);
    ASSERT_EQ(rust.status, TL_MSIX_STATUS_SUCCESS) << rust.error_message;
    const auto cpp = inspect_msix_package(path);
    ASSERT_TRUE(cpp.has_value());
    expect_semantically_equal(*cpp, rust.info);

    const std::filesystem::path destination = fixture.path("extracted");
    std::error_code cleanup_error;
    std::filesystem::remove_all(destination, cleanup_error);
    ASSERT_FALSE(cleanup_error);
    const auto extracted = extract_msix_package(path, destination);
    ASSERT_TRUE(extracted.has_value());
    EXPECT_EQ(extracted->filename(), "Example.exe");
    EXPECT_TRUE(std::filesystem::is_regular_file(*extracted));
    std::filesystem::remove_all(destination, cleanup_error);
    EXPECT_FALSE(cleanup_error);
}

TEST(RustMsixParserTest, FfiSizeFillAndInsufficientOutputPreserveSentinel) {
    const ZipImage image = make_zip({{"AppxManifest.xml", kManifest, 0, 0}});
    std::uint64_t required = 0;
    tl_msix_error_v1 error{};
    std::array<char, 256> message{};
    std::uint64_t error_required = 0;
    ASSERT_EQ(tl_msix_parse_v1_size(image.bytes.data(), image.bytes.size(), &required, &error,
                                    message.data(), message.size(), &error_required),
              TL_MSIX_STATUS_SUCCESS);
    ASSERT_GT(required, 128U);
    ASSERT_EQ(error.code, TL_MSIX_ERROR_NONE);

    std::vector<std::uint8_t> too_small(required - 1U, 0xA5U);
    std::uint64_t fill_required = 0;
    ASSERT_EQ(tl_msix_parse_v1_fill(image.bytes.data(), image.bytes.size(), too_small.data(),
                                    too_small.size(), &fill_required, &error, message.data(),
                                    message.size(), &error_required),
              TL_MSIX_STATUS_BUFFER_TOO_SMALL);
    EXPECT_EQ(fill_required, required);
    EXPECT_TRUE(std::all_of(too_small.begin(), too_small.end(),
                            [](const std::uint8_t byte) { return byte == 0xA5U; }));

    std::vector<std::uint8_t> wire(required, 0);
    ASSERT_EQ(tl_msix_parse_v1_fill(image.bytes.data(), image.bytes.size(), wire.data(), wire.size(),
                                    &fill_required, &error, message.data(), message.size(),
                                    &error_required),
              TL_MSIX_STATUS_SUCCESS);
    EXPECT_EQ(std::string(wire.begin(), wire.begin() + 4), "TLMS");
    EXPECT_EQ(read_le32(wire, TL_MSIX_WIRE_HEADER_SIZE_OFFSET), TL_MSIX_WIRE_HEADER_SIZE);
    EXPECT_EQ(read_le64(wire, TL_MSIX_WIRE_HEADER_TOTAL_SIZE_OFFSET), wire.size());
    const std::size_t info_descriptor = TL_MSIX_WIRE_TABLE_DESCRIPTOR_OFFSET;
    const std::size_t application_descriptor =
        info_descriptor + TL_MSIX_WIRE_TABLE_DESCRIPTOR_SIZE;
    const std::size_t strings_descriptor =
        application_descriptor + TL_MSIX_WIRE_TABLE_DESCRIPTOR_SIZE;
    EXPECT_EQ(read_le64(wire, info_descriptor + TL_MSIX_WIRE_DESCRIPTOR_OFFSET_FIELD), 128U);
    EXPECT_EQ(read_le64(wire, info_descriptor + TL_MSIX_WIRE_DESCRIPTOR_COUNT_FIELD), 1U);
    EXPECT_EQ(read_le32(wire, info_descriptor + TL_MSIX_WIRE_DESCRIPTOR_STRIDE_FIELD), 64U);
    EXPECT_EQ(read_le64(wire, application_descriptor + TL_MSIX_WIRE_DESCRIPTOR_COUNT_FIELD), 1U);
    const std::uint64_t strings_offset =
        read_le64(wire, strings_descriptor + TL_MSIX_WIRE_DESCRIPTOR_OFFSET_FIELD);
    EXPECT_NE(strings_offset, 0U);
    EXPECT_EQ(strings_offset & 7U, 0U);
    EXPECT_GT(read_le64(wire, strings_descriptor + TL_MSIX_WIRE_DESCRIPTOR_COUNT_FIELD), 0U);
    AppxPackageInfo decoded;
    std::string decode_message;
    ASSERT_TRUE(decode_tlms_v1(wire, decoded, error, decode_message)) << decode_message;
    EXPECT_EQ(decoded.package_name, "Example.App");
}

TEST(RustMsixParserTest, PreservesApplicationsWithoutExecutable) {
    TempDirFixture fixture;
    const ZipImage image = make_zip({{"AppxManifest.xml", kManifestWithoutExecutable, 0, 0}});
    const std::filesystem::path path = fixture.path("no-executable.msix");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good());
    output.write(reinterpret_cast<const char*>(image.bytes.data()),
                 static_cast<std::streamsize>(image.bytes.size()));
    output.close();
    const auto cpp_info = inspect_msix_package(path);
    ASSERT_TRUE(cpp_info.has_value());
    const RustMsixParseResult rust_result = parse_msix_rust(image.bytes);
    ASSERT_EQ(rust_result.status, TL_MSIX_STATUS_SUCCESS);
    expect_semantically_equal(*cpp_info, rust_result.info);
    ASSERT_EQ(rust_result.info.applications.size(), 1U);
    EXPECT_FALSE(rust_result.info.main_executable.has_value());
}

TEST(RustMsixParserTest, RejectsInvalidZipAndManifestWithoutFallback) {
    TempDirFixture fixture;
    const auto expect_rejected = [&](ZipImage image, const char* name) {
        const std::filesystem::path path = fixture.path(name);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.good());
        output.write(reinterpret_cast<const char*>(image.bytes.data()),
                     static_cast<std::streamsize>(image.bytes.size()));
        ASSERT_TRUE(output.good());
        output.close();
        EXPECT_FALSE(inspect_msix_package(path).has_value());
        const RustMsixParseResult rust_result = parse_msix_rust(read_file(path));
        EXPECT_NE(rust_result.status, TL_MSIX_STATUS_SUCCESS);
    };

    ZipImage bad_crc = make_zip({{"AppxManifest.xml", kManifest, 0, 0}});
    put_le32(bad_crc.bytes, bad_crc.central_offsets[0] + 16U, 0x12345678U);
    expect_rejected(std::move(bad_crc), "bad-crc.msix");

    expect_rejected(make_zip({{"../AppxManifest.xml", kManifest, 0, 0}}), "traversal.msix");
    expect_rejected(make_zip({{"a\\b", "x", 0, 0}, {"a/b", kManifest, 0, 0}}),
                    "collision.msix");
    expect_rejected(make_zip({{"AppxBundleManifest.xml", kManifest, 0, 0}}), "bundle.msix");
    expect_rejected(make_zip({{"AppxManifest.xml", "<!DOCTYPE Package SYSTEM 'x'><Package />", 0, 0}}),
                    "dtd.msix");
    expect_rejected(make_zip({{"AppxManifest.xml", kManifest, 99, 0}}), "method.msix");
    expect_rejected(make_zip({{"AppxManifest.xml", kManifest, 0, 1U}}), "encrypted.msix");
    expect_rejected(make_zip({{"AppxManifest.xml", kManifest, 0, 0, 0x0314U, 0120000U << 16U}}),
                    "symlink.msix");

    ZipImage multi_disk = make_zip64(make_zip({{"AppxManifest.xml", kManifest, 0, 0, 45, 0, true}}));
    const std::size_t classic_eocd = multi_disk.bytes.size() - 22U;
    put_le32(multi_disk.bytes, classic_eocd - 20U + 4U, 1U);
    expect_rejected(std::move(multi_disk), "zip64-multi-disk.msix");
}

TEST(RustMsixParserTest, DecoderRejectsWireHeaderAndReferenceMutations) {
    const ZipImage image = make_zip({{"AppxManifest.xml", kManifest, 0, 0}});
    const RustMsixParseResult parsed = parse_msix_rust(image.bytes);
    ASSERT_EQ(parsed.status, TL_MSIX_STATUS_SUCCESS);

    std::uint64_t required = 0;
    tl_msix_error_v1 error{};
    std::array<char, 128> message{};
    std::uint64_t error_required = 0;
    ASSERT_EQ(tl_msix_parse_v1_size(image.bytes.data(), image.bytes.size(), &required, &error,
                                    message.data(), message.size(), &error_required),
              TL_MSIX_STATUS_SUCCESS);
    std::vector<std::uint8_t> wire(required);
    ASSERT_EQ(tl_msix_parse_v1_fill(image.bytes.data(), image.bytes.size(), wire.data(), wire.size(),
                                    &required, &error, message.data(), message.size(),
                                    &error_required),
              TL_MSIX_STATUS_SUCCESS);

    const auto expect_wire_rejected = [&](std::vector<std::uint8_t> candidate) {
        AppxPackageInfo info;
        std::string diagnostic;
        tl_msix_error_v1 decode_error{};
        EXPECT_FALSE(decode_tlms_v1(candidate, info, decode_error, diagnostic));
        EXPECT_EQ(decode_error.code, TL_MSIX_ERROR_WIRE_FORMAT);
    };

    {
        auto candidate = wire;
        candidate[0] = 'X';
        expect_wire_rejected(std::move(candidate));
    }
    {
        auto candidate = wire;
        put_le16(candidate, TL_MSIX_WIRE_HEADER_MAJOR_OFFSET, 2);
        expect_wire_rejected(std::move(candidate));
    }
    {
        auto candidate = wire;
        put_le32(candidate, TL_MSIX_WIRE_HEADER_FLAGS_OFFSET, 1);
        expect_wire_rejected(std::move(candidate));
    }
    {
        auto candidate = wire;
        put_le32(candidate, TL_MSIX_WIRE_TABLE_DESCRIPTOR_OFFSET +
                              TL_MSIX_WIRE_DESCRIPTOR_STRIDE_FIELD,
                  1);
        expect_wire_rejected(std::move(candidate));
    }
    {
        auto candidate = wire;
        put_le64(candidate, TL_MSIX_WIRE_TABLE_DESCRIPTOR_OFFSET +
                              3U * TL_MSIX_WIRE_TABLE_DESCRIPTOR_SIZE +
                              TL_MSIX_WIRE_DESCRIPTOR_COUNT_FIELD,
                  1);
        expect_wire_rejected(std::move(candidate));
    }
}

TEST(RustMsixParserTest, FfiRejectsInvalidPointersAndTerminatesDiagnostics) {
    std::uint64_t required = 99;
    tl_msix_error_v1 error{};
    std::array<char, 128> message{};
    std::uint64_t error_required = 0;
    EXPECT_EQ(tl_msix_parse_v1_size(nullptr, 1, &required, &error, message.data(), message.size(),
                                    &error_required),
              TL_MSIX_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(required, 0U);
    EXPECT_EQ(message[error_required - 1U], '\0');
    EXPECT_EQ(error.code, TL_MSIX_ERROR_INVALID_ARGUMENT);

    const ZipImage image = make_zip({{"AppxManifest.xml", kManifest, 0, 0}});
    EXPECT_EQ(tl_msix_parse_v1_fill(image.bytes.data(), image.bytes.size(), nullptr, 1, &required,
                                    &error, message.data(), message.size(), &error_required),
              TL_MSIX_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(required, 0U);
    EXPECT_EQ(error.code, TL_MSIX_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(tl_msix_parse_v1_size(image.bytes.data(), image.bytes.size(), &required, &error,
                                    nullptr, 0, &error_required),
              TL_MSIX_STATUS_BUFFER_TOO_SMALL);
    EXPECT_GE(error_required, 1U);
}

TEST(RustMsixParserTest, RepeatedConcurrentCallsAreStateless) {
    const ZipImage image = make_zip({{"AppxManifest.xml", kManifest, 0, 0}});
    std::array<std::thread, 4> workers;
    std::array<std::uint32_t, 4> statuses{};
    for (std::size_t index = 0; index < workers.size(); ++index) {
        workers[index] = std::thread([&image, &statuses, index] {
            for (int iteration = 0; iteration < 20; ++iteration) {
                statuses[index] = parse_msix_rust(image.bytes).status;
                if (statuses[index] != TL_MSIX_STATUS_SUCCESS) return;
            }
        });
    }
    for (std::thread& worker : workers) worker.join();
    for (const std::uint32_t status : statuses) EXPECT_EQ(status, TL_MSIX_STATUS_SUCCESS);
}

}  // namespace
}  // namespace tradutorlinux::package
