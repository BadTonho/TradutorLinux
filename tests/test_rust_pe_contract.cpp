#include "tradutorlinux/ffi/rust_pe_parser.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ByteVector = std::vector<std::uint8_t>;

constexpr std::size_t kHeaderSize = TL_PE_WIRE_HEADER_SIZE;
constexpr std::size_t kDescriptorSize = TL_PE_WIRE_TABLE_DESCRIPTOR_SIZE;
constexpr std::size_t kDescriptorOffset = TL_PE_WIRE_TABLE_DESCRIPTOR_OFFSET;
constexpr std::size_t kStringRefSize = TL_PE_WIRE_STRING_REF_SIZE;

void write_u16(ByteVector& bytes, const std::size_t offset, const std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & UINT16_C(0xff));
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & UINT16_C(0xff));
}

void write_u32(ByteVector& bytes, const std::size_t offset, const std::uint32_t value) {
    for (std::size_t index = 0; index < 4U; ++index) {
        const unsigned int shift = static_cast<unsigned int>(index * 8U);
        bytes[offset + index] = static_cast<std::uint8_t>((value >> shift) & UINT32_C(0xff));
    }
}

void write_u64(ByteVector& bytes, const std::size_t offset, const std::uint64_t value) {
    for (std::size_t index = 0; index < 8U; ++index) {
        const unsigned int shift = static_cast<unsigned int>(index * 8U);
        bytes[offset + index] = static_cast<std::uint8_t>((value >> shift) & UINT64_C(0xff));
    }
}

std::uint16_t read_u16(const ByteVector& bytes, const std::size_t offset) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset]) |
                                      static_cast<std::uint16_t>(bytes[offset + 1U] << 8U));
}

std::uint32_t read_u32(const ByteVector& bytes, const std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4U; ++index) {
        const unsigned int shift = static_cast<unsigned int>(index * 8U);
        value |= static_cast<std::uint32_t>(bytes[offset + index]) << shift;
    }
    return value;
}

std::uint64_t read_u64(const ByteVector& bytes, const std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8U; ++index) {
        const unsigned int shift = static_cast<unsigned int>(index * 8U);
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << shift;
    }
    return value;
}

void write_descriptor(ByteVector& bytes,
                      const std::uint32_t table,
                      const std::uint64_t offset,
                      const std::uint64_t count,
                      const std::uint32_t stride,
                      const std::uint32_t flags = 0) {
    const std::size_t descriptor = kDescriptorOffset +
                                   static_cast<std::size_t>(table) * kDescriptorSize;
    write_u64(bytes, descriptor, offset);
    write_u64(bytes, descriptor + 8U, count);
    write_u32(bytes, descriptor + 16U, stride);
    write_u32(bytes, descriptor + 20U, flags);
}

struct Descriptor {
    std::uint64_t offset{};
    std::uint64_t count{};
    std::uint32_t stride{};
    std::uint32_t flags{};
};

Descriptor read_descriptor(const ByteVector& bytes, const std::uint32_t table) {
    const std::size_t descriptor = kDescriptorOffset +
                                   static_cast<std::size_t>(table) * kDescriptorSize;
    return {.offset = read_u64(bytes, descriptor),
            .count = read_u64(bytes, descriptor + 8U),
            .stride = read_u32(bytes, descriptor + 16U),
            .flags = read_u32(bytes, descriptor + 20U)};
}

bool has_range(const std::uint64_t offset,
               const std::uint64_t length,
               const std::uint64_t total) {
    return offset <= total && length <= total - offset;
}

bool validate_wire(const ByteVector& bytes) {
    if (bytes.size() < kHeaderSize || bytes.size() > TL_PE_LIMIT_MAX_SERIALIZED_BYTES) {
        return false;
    }
    if (bytes[0] != TL_PE_WIRE_MAGIC_0 || bytes[1] != TL_PE_WIRE_MAGIC_1 ||
        bytes[2] != TL_PE_WIRE_MAGIC_2 || bytes[3] != TL_PE_WIRE_MAGIC_3 ||
        read_u16(bytes, TL_PE_WIRE_HEADER_MAJOR_OFFSET) != TL_PE_WIRE_MAJOR ||
        read_u16(bytes, TL_PE_WIRE_HEADER_MINOR_OFFSET) != TL_PE_WIRE_MINOR ||
        read_u32(bytes, TL_PE_WIRE_HEADER_SIZE_OFFSET) != TL_PE_WIRE_HEADER_SIZE ||
        read_u64(bytes, TL_PE_WIRE_HEADER_TOTAL_SIZE_OFFSET) != bytes.size() ||
        read_u32(bytes, TL_PE_WIRE_HEADER_TABLE_COUNT_OFFSET) != TL_PE_WIRE_TABLE_COUNT ||
        read_u64(bytes, 24U) != 0U) {
        return false;
    }

    const std::uint64_t total = static_cast<std::uint64_t>(bytes.size());
    for (std::uint32_t table = 0; table < TL_PE_WIRE_TABLE_COUNT; ++table) {
        const Descriptor descriptor = read_descriptor(bytes, table);
        if (descriptor.count == 0U) {
            if (descriptor.offset != 0U || descriptor.stride != 0U || descriptor.flags != 0U) {
                return false;
            }
            continue;
        }
        if (table == TL_PE_WIRE_TABLE_RESERVED) return false;
        if (descriptor.offset < kHeaderSize || descriptor.offset >= total) return false;
        if (table == TL_PE_WIRE_TABLE_STRINGS) {
            if (descriptor.stride != 0U ||
                descriptor.flags != TL_PE_WIRE_TABLE_FLAG_VARIABLE_RECORDS) {
                return false;
            }
            std::uint64_t cursor = descriptor.offset;
            for (std::uint64_t index = 0; index < descriptor.count; ++index) {
                if (!has_range(cursor, TL_PE_WIRE_STRING_RECORD_HEADER_SIZE, total)) return false;
                if (read_u32(bytes, static_cast<std::size_t>(cursor) + 4U) != 0U) return false;
                const std::uint32_t length = read_u32(bytes, static_cast<std::size_t>(cursor));
                if (length > TL_PE_LIMIT_MAX_STRING_BYTES) return false;
                const std::uint64_t record_size =
                    static_cast<std::uint64_t>(TL_PE_WIRE_STRING_RECORD_HEADER_SIZE) + length;
                const std::uint64_t padding = (8U - (record_size % 8U)) % 8U;
                if (!has_range(cursor, record_size + padding, total)) return false;
                cursor += record_size + padding;
            }
            continue;
        }
        if (descriptor.stride == 0U || descriptor.flags != 0U ||
            descriptor.count > (total - descriptor.offset) / descriptor.stride) {
            return false;
        }
    }

    const Descriptor info = read_descriptor(bytes, TL_PE_WIRE_TABLE_INFO);
    return info.count == 1U && info.stride == TL_PE_WIRE_INFO_STRIDE;
}

ByteVector make_header(const std::size_t total_size) {
    ByteVector bytes(total_size, 0U);
    bytes[0] = TL_PE_WIRE_MAGIC_0;
    bytes[1] = TL_PE_WIRE_MAGIC_1;
    bytes[2] = TL_PE_WIRE_MAGIC_2;
    bytes[3] = TL_PE_WIRE_MAGIC_3;
    write_u16(bytes, TL_PE_WIRE_HEADER_MAJOR_OFFSET, TL_PE_WIRE_MAJOR);
    write_u16(bytes, TL_PE_WIRE_HEADER_MINOR_OFFSET, TL_PE_WIRE_MINOR);
    write_u32(bytes, TL_PE_WIRE_HEADER_SIZE_OFFSET, TL_PE_WIRE_HEADER_SIZE);
    write_u64(bytes, TL_PE_WIRE_HEADER_TOTAL_SIZE_OFFSET,
              static_cast<std::uint64_t>(total_size));
    write_u32(bytes, TL_PE_WIRE_HEADER_TABLE_COUNT_OFFSET, TL_PE_WIRE_TABLE_COUNT);
    write_descriptor(bytes, TL_PE_WIRE_TABLE_INFO, kHeaderSize, 1U, TL_PE_WIRE_INFO_STRIDE);
    return bytes;
}

ByteVector make_feature_vector(const std::uint32_t feature_table,
                               const std::uint32_t stride) {
    const std::size_t record_offset = kHeaderSize + TL_PE_WIRE_INFO_STRIDE;
    const std::size_t total_size = record_offset + static_cast<std::size_t>(stride);
    ByteVector bytes = make_header(total_size);
    write_descriptor(bytes, feature_table, record_offset, 1U, stride);
    return bytes;
}

ByteVector make_minimal_vector() {
    constexpr std::size_t kInfoOffset = kHeaderSize;
    constexpr std::size_t kSectionOffset = kInfoOffset + TL_PE_WIRE_INFO_STRIDE;
    constexpr std::size_t kStringOffset = kSectionOffset + TL_PE_WIRE_SECTION_STRIDE;
    constexpr std::size_t kStringBytesOffset = kStringOffset + 8U;
    constexpr std::size_t kStringLength = 5U;
    constexpr std::size_t kStringRecordSize = 8U + kStringLength + 3U;
    ByteVector bytes = make_header(kStringBytesOffset + kStringRecordSize);
    write_descriptor(bytes, TL_PE_WIRE_TABLE_SECTIONS, kSectionOffset, 1U,
                     TL_PE_WIRE_SECTION_STRIDE);
    write_descriptor(bytes, TL_PE_WIRE_TABLE_STRINGS, kStringOffset, 1U, 0U,
                     TL_PE_WIRE_TABLE_FLAG_VARIABLE_RECORDS);
    write_u32(bytes, kInfoOffset, TL_PE_WIRE_INFO_FLAG_PE32_PLUS);
    write_u16(bytes, kInfoOffset + 4U, UINT16_C(0x8664));
    write_u16(bytes, kInfoOffset + 6U, 1U);
    write_u32(bytes, kInfoOffset + 8U, UINT32_C(0x1000));
    write_u64(bytes, kInfoOffset + 12U, UINT64_C(0x140000000));
    write_u32(bytes, kInfoOffset + 24U, UINT32_C(0x3000));
    write_u32(bytes, kStringOffset, kStringLength);
    bytes[kStringBytesOffset + 0U] = 't';
    bytes[kStringBytesOffset + 1U] = 'e';
    bytes[kStringBytesOffset + 2U] = 'x';
    bytes[kStringBytesOffset + 3U] = 't';
    bytes[kStringBytesOffset + 4U] = 0U;
    write_u64(bytes, kSectionOffset, kStringBytesOffset);
    write_u32(bytes, kSectionOffset + 8U, kStringLength);
    write_u32(bytes, kSectionOffset + 16U, UINT32_C(0x1000));
    return bytes;
}

void set_string_ref(ByteVector& bytes,
                    const std::size_t record_offset,
                    const std::size_t string_offset,
                    const std::size_t string_length) {
    write_u64(bytes, record_offset, static_cast<std::uint64_t>(string_offset));
    write_u32(bytes, record_offset + 8U, static_cast<std::uint32_t>(string_length));
    write_u32(bytes, record_offset + 12U, 0U);
}

ByteVector make_nested_vector() {
    constexpr std::size_t kInfoOffset = kHeaderSize;
    constexpr std::size_t kSectionsOffset = kInfoOffset + TL_PE_WIRE_INFO_STRIDE;
    constexpr std::size_t kImportsOffset = kSectionsOffset + TL_PE_WIRE_SECTION_STRIDE;
    constexpr std::size_t kImportSymbolsOffset = kImportsOffset + TL_PE_WIRE_IMPORT_DLL_STRIDE;
    constexpr std::size_t kRuntimeOffset =
        kImportSymbolsOffset + TL_PE_WIRE_IMPORT_SYMBOL_STRIDE;
    constexpr std::size_t kUnwindInfoOffset = kRuntimeOffset + TL_PE_WIRE_RUNTIME_FUNCTION_STRIDE;
    constexpr std::size_t kUnwindCodeOffset = kUnwindInfoOffset + TL_PE_WIRE_UNWIND_INFO_STRIDE;
    constexpr std::size_t kUnwindEpilogOffset = kUnwindCodeOffset + TL_PE_WIRE_UNWIND_CODE_STRIDE;
    constexpr std::size_t kRelocBlockOffset = kUnwindEpilogOffset + TL_PE_WIRE_UNWIND_EPILOG_STRIDE;
    constexpr std::size_t kRelocEntryOffset = kRelocBlockOffset + TL_PE_WIRE_RELOC_BLOCK_STRIDE;
    constexpr std::size_t kStringsOffset = kRelocEntryOffset + TL_PE_WIRE_RELOC_ENTRY_STRIDE;
    constexpr std::size_t kStringRecordSize = 16U;
    ByteVector result = make_header(kStringsOffset + kStringRecordSize);

    write_descriptor(result, TL_PE_WIRE_TABLE_SECTIONS, kSectionsOffset, 1U,
                     TL_PE_WIRE_SECTION_STRIDE);
    write_descriptor(result, TL_PE_WIRE_TABLE_IMPORT_DLLS, kImportsOffset, 1U,
                     TL_PE_WIRE_IMPORT_DLL_STRIDE);
    write_descriptor(result, TL_PE_WIRE_TABLE_IMPORT_SYMBOLS, kImportSymbolsOffset, 1U,
                     TL_PE_WIRE_IMPORT_SYMBOL_STRIDE);
    write_descriptor(result, TL_PE_WIRE_TABLE_RUNTIME_FUNCTIONS, kRuntimeOffset, 1U,
                     TL_PE_WIRE_RUNTIME_FUNCTION_STRIDE);
    write_descriptor(result, TL_PE_WIRE_TABLE_UNWIND_INFOS, kUnwindInfoOffset, 1U,
                     TL_PE_WIRE_UNWIND_INFO_STRIDE);
    write_descriptor(result, TL_PE_WIRE_TABLE_UNWIND_CODES, kUnwindCodeOffset, 1U,
                     TL_PE_WIRE_UNWIND_CODE_STRIDE);
    write_descriptor(result, TL_PE_WIRE_TABLE_UNWIND_EPILOGS, kUnwindEpilogOffset, 1U,
                     TL_PE_WIRE_UNWIND_EPILOG_STRIDE);
    write_descriptor(result, TL_PE_WIRE_TABLE_RELOC_BLOCKS, kRelocBlockOffset, 1U,
                     TL_PE_WIRE_RELOC_BLOCK_STRIDE);
    write_descriptor(result, TL_PE_WIRE_TABLE_RELOC_ENTRIES, kRelocEntryOffset, 1U,
                     TL_PE_WIRE_RELOC_ENTRY_STRIDE);
    write_descriptor(result, TL_PE_WIRE_TABLE_STRINGS, kStringsOffset, 1U, 0U,
                     TL_PE_WIRE_TABLE_FLAG_VARIABLE_RECORDS);

    write_u32(result, kStringsOffset, 1U);
    result[kStringsOffset + 8U] = 's';
    set_string_ref(result, kSectionsOffset, kStringsOffset + 8U, 1U);
    write_u64(result, kImportsOffset + 16U, 0U);
    write_u64(result, kImportsOffset + 24U, 1U);
    write_u64(result, kRuntimeOffset + 12U, 0U);
    write_u64(result, kUnwindInfoOffset + 8U, 0U);
    write_u64(result, kUnwindInfoOffset + 16U, 1U);
    write_u64(result, kUnwindInfoOffset + 24U, 0U);
    write_u64(result, kUnwindInfoOffset + 32U, 1U);
    write_u64(result, kRelocBlockOffset + 8U, 0U);
    write_u64(result, kRelocBlockOffset + 16U, 1U);
    return result;
}

TEST(RustPeContractTest, HeaderAndDiagnosticLayoutAreStable) {
    static_assert(sizeof(tl_pe_error_v1) == 24U);
    static_assert(alignof(tl_pe_error_v1) == 8U);
    static_assert(kStringRefSize == 16U);
    EXPECT_EQ(TL_PE_WIRE_HEADER_SIZE,
              TL_PE_WIRE_TABLE_DESCRIPTOR_OFFSET +
                  TL_PE_WIRE_TABLE_COUNT * TL_PE_WIRE_TABLE_DESCRIPTOR_SIZE);
    EXPECT_EQ(TL_PE_STATUS_SUCCESS, 0U);
    EXPECT_EQ(TL_PE_STATUS_UNSUPPORTED_MECHANISM, 5U);
    EXPECT_EQ(TL_PE_STATUS_INTERNAL, 10U);
    EXPECT_EQ(TL_PE_LIMIT_MAX_SERIALIZED_BYTES, UINT64_C(268435456));
}

TEST(RustPeContractTest, MinimalVectorUsesLittleEndianFlatTables) {
    const ByteVector bytes = make_minimal_vector();

    ASSERT_TRUE(validate_wire(bytes));
    EXPECT_EQ(read_u32(bytes, kHeaderSize), TL_PE_WIRE_INFO_FLAG_PE32_PLUS);
    EXPECT_EQ(read_u16(bytes, kHeaderSize + 4U), UINT16_C(0x8664));
    EXPECT_EQ(read_u64(bytes, kHeaderSize + TL_PE_WIRE_INFO_STRIDE + 0U),
              kHeaderSize + TL_PE_WIRE_INFO_STRIDE + TL_PE_WIRE_SECTION_STRIDE + 8U);
}

TEST(RustPeContractTest, NestedTablesUseExplicitIndicesAndRanges) {
    const ByteVector bytes = make_nested_vector();

    ASSERT_TRUE(validate_wire(bytes));
    const Descriptor imports = read_descriptor(bytes, TL_PE_WIRE_TABLE_IMPORT_DLLS);
    const Descriptor import_symbols = read_descriptor(bytes, TL_PE_WIRE_TABLE_IMPORT_SYMBOLS);
    EXPECT_EQ(read_u64(bytes, static_cast<std::size_t>(imports.offset) + 16U), 0U);
    EXPECT_EQ(read_u64(bytes, static_cast<std::size_t>(imports.offset) + 24U), 1U);
    EXPECT_EQ(import_symbols.count, 1U);

    const Descriptor unwind = read_descriptor(bytes, TL_PE_WIRE_TABLE_UNWIND_INFOS);
    EXPECT_EQ(read_u64(bytes, static_cast<std::size_t>(unwind.offset) + 16U), 1U);
    EXPECT_EQ(read_u64(bytes, static_cast<std::size_t>(unwind.offset) + 32U), 1U);
    const Descriptor reloc = read_descriptor(bytes, TL_PE_WIRE_TABLE_RELOC_BLOCKS);
    EXPECT_EQ(read_u64(bytes, static_cast<std::size_t>(reloc.offset) + 16U), 1U);
}

TEST(RustPeContractTest, FeatureVectorsCoverEveryNestedParserResult) {
    struct Feature {
        std::uint32_t table;
        std::uint32_t stride;
        std::string_view name;
    };
    const std::array<Feature, 7> features{
        Feature{TL_PE_WIRE_TABLE_IMPORT_DLLS, TL_PE_WIRE_IMPORT_DLL_STRIDE, "imports"},
        Feature{TL_PE_WIRE_TABLE_EXPORTS, TL_PE_WIRE_EXPORT_STRIDE, "exports"},
        Feature{TL_PE_WIRE_TABLE_DELAY_IMPORT_DLLS, TL_PE_WIRE_IMPORT_DLL_STRIDE,
                "delay-imports"},
        Feature{TL_PE_WIRE_TABLE_TLS_CALLBACKS, TL_PE_WIRE_TLS_CALLBACK_STRIDE, "tls"},
        Feature{TL_PE_WIRE_TABLE_RUNTIME_FUNCTIONS, TL_PE_WIRE_RUNTIME_FUNCTION_STRIDE,
                "unwind"},
        Feature{TL_PE_WIRE_TABLE_UNWIND_CODES, TL_PE_WIRE_UNWIND_CODE_STRIDE, "unwind-codes"},
        Feature{TL_PE_WIRE_TABLE_RELOC_BLOCKS, TL_PE_WIRE_RELOC_BLOCK_STRIDE, "relocations"},
    };

    for (const Feature& feature : features) {
        const ByteVector bytes = make_feature_vector(feature.table, feature.stride);
        EXPECT_TRUE(validate_wire(bytes)) << feature.name;
        EXPECT_EQ(read_descriptor(bytes, feature.table).stride, feature.stride) << feature.name;
    }
}

TEST(RustPeContractTest, RejectsInvalidMagicAndVersion) {
    ByteVector bytes = make_minimal_vector();
    bytes[0] = 'X';
    EXPECT_FALSE(validate_wire(bytes));

    bytes = make_minimal_vector();
    write_u16(bytes, TL_PE_WIRE_HEADER_MAJOR_OFFSET, 2U);
    EXPECT_FALSE(validate_wire(bytes));

    bytes = make_minimal_vector();
    write_u16(bytes, TL_PE_WIRE_HEADER_MINOR_OFFSET, 1U);
    EXPECT_FALSE(validate_wire(bytes));
}

TEST(RustPeContractTest, RejectsTableOverflowAndTruncatedString) {
    ByteVector bytes = make_minimal_vector();
    write_u64(bytes, kDescriptorOffset +
                         static_cast<std::size_t>(TL_PE_WIRE_TABLE_SECTIONS) *
                             kDescriptorSize +
                         TL_PE_WIRE_DESCRIPTOR_COUNT_FIELD,
              std::numeric_limits<std::uint64_t>::max());
    EXPECT_FALSE(validate_wire(bytes));

    bytes = make_minimal_vector();
    const Descriptor strings = read_descriptor(bytes, TL_PE_WIRE_TABLE_STRINGS);
    write_u32(bytes, static_cast<std::size_t>(strings.offset), UINT32_C(0xFFFFFFFF));
    EXPECT_FALSE(validate_wire(bytes));
}

TEST(RustPeContractTest, RejectsNonzeroReservedFieldsAndReservedTable) {
    ByteVector bytes = make_minimal_vector();
    bytes[24] = 1U;
    EXPECT_FALSE(validate_wire(bytes));

    bytes = make_minimal_vector();
    write_u32(bytes, static_cast<std::size_t>(read_descriptor(bytes, TL_PE_WIRE_TABLE_STRINGS).offset) +
                         4U,
              1U);
    EXPECT_FALSE(validate_wire(bytes));

    bytes = make_minimal_vector();
    write_descriptor(bytes, TL_PE_WIRE_TABLE_RESERVED, kHeaderSize, 1U, 8U);
    bytes.resize(bytes.size() + 8U, 0U);
    write_u64(bytes, TL_PE_WIRE_HEADER_TOTAL_SIZE_OFFSET,
              static_cast<std::uint64_t>(bytes.size()));
    EXPECT_FALSE(validate_wire(bytes));
}

TEST(RustPeContractTest, LimitsMirrorTheCurrentCppParser) {
    EXPECT_EQ(TL_PE_LIMIT_MAX_SECTIONS, UINT64_C(65535));
    EXPECT_EQ(TL_PE_LIMIT_MAX_IMPORT_DLLS, UINT64_C(1024));
    EXPECT_EQ(TL_PE_LIMIT_MAX_SYMBOLS_PER_DLL, UINT64_C(4096));
    EXPECT_EQ(TL_PE_LIMIT_MAX_RELOC_BLOCKS, UINT64_C(4096));
    EXPECT_EQ(TL_PE_LIMIT_MAX_RUNTIME_FUNCTIONS, UINT64_C(65536));
    EXPECT_EQ(TL_PE_LIMIT_MAX_STRING_BYTES, UINT64_C(65534));
    EXPECT_EQ(TL_PE_LIMIT_MAX_EXPORT_FUNCTIONS, UINT64_C(65536));
    EXPECT_EQ(TL_PE_LIMIT_MAX_EXPORT_NAMES, UINT64_C(65536));
    EXPECT_EQ(TL_PE_LIMIT_MAX_TLS_CALLBACKS, UINT64_C(65536));
}

}  // namespace
