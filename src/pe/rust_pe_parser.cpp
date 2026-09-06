#include "rust_pe_parser.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tradutorlinux::pe {
namespace {

constexpr std::size_t kHeaderSize = TL_PE_WIRE_HEADER_SIZE;
constexpr std::size_t kDescriptorSize = TL_PE_WIRE_TABLE_DESCRIPTOR_SIZE;
constexpr std::size_t kDescriptorOffset = TL_PE_WIRE_TABLE_DESCRIPTOR_OFFSET;
constexpr std::size_t kTableCount = TL_PE_WIRE_TABLE_COUNT;
constexpr std::size_t kTableReserved = TL_PE_WIRE_TABLE_RESERVED;
constexpr std::uint64_t kUnknownOffset = TL_PE_ERROR_OFFSET_UNKNOWN;
constexpr std::array<std::uint32_t, kTableCount> kExpectedStrides{
    TL_PE_WIRE_INFO_STRIDE,
    TL_PE_WIRE_SECTION_STRIDE,
    0,
    TL_PE_WIRE_IMPORT_DLL_STRIDE,
    TL_PE_WIRE_IMPORT_SYMBOL_STRIDE,
    TL_PE_WIRE_IMPORT_DLL_STRIDE,
    TL_PE_WIRE_IMPORT_SYMBOL_STRIDE,
    TL_PE_WIRE_EXPORT_STRIDE,
    TL_PE_WIRE_TLS_CALLBACK_STRIDE,
    TL_PE_WIRE_RUNTIME_FUNCTION_STRIDE,
    TL_PE_WIRE_UNWIND_INFO_STRIDE,
    TL_PE_WIRE_UNWIND_CODE_STRIDE,
    TL_PE_WIRE_UNWIND_EPILOG_STRIDE,
    TL_PE_WIRE_RELOC_BLOCK_STRIDE,
    TL_PE_WIRE_RELOC_ENTRY_STRIDE,
    0,
};

struct Descriptor {
    std::uint64_t offset{};
    std::uint64_t count{};
    std::uint32_t stride{};
    std::uint32_t flags{};
};

struct StringRecord {
    std::uint64_t data_offset{};
    std::uint32_t length{};
};

[[nodiscard]] bool add_u64(const std::uint64_t left, const std::uint64_t right,
                           std::uint64_t& result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool multiply_u64(const std::uint64_t left, const std::uint64_t right,
                                std::uint64_t& result) noexcept {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) return false;
    result = left * right;
    return true;
}

[[nodiscard]] bool align8(const std::uint64_t value, std::uint64_t& result) noexcept {
    if (value > std::numeric_limits<std::uint64_t>::max() - 7U) return false;
    result = (value + 7U) & ~UINT64_C(7);
    return true;
}

[[nodiscard]] bool has_range(const std::uint64_t offset, const std::uint64_t length,
                             const std::uint64_t total) noexcept {
    return offset <= total && length <= total - offset;
}

class Decoder {
public:
    explicit Decoder(const std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    bool fail(const std::uint32_t code, const std::uint32_t phase,
              const std::uint64_t offset, const std::uint64_t detail,
              const char* message) {
        if (!message_.empty()) return false;
        error_.code = code;
        error_.phase = phase;
        error_.input_offset = offset;
        error_.detail_value = detail;
        message_ = message;
        return false;
    }

    [[nodiscard]] bool read_u16(const std::uint64_t offset, std::uint16_t& value,
                                const std::uint32_t code, const std::uint32_t phase) {
        if (!has_range(offset, 2U, bytes_.size())) {
            return fail(TL_PE_ERROR_WIRE_FORMAT, phase, offset, 2U,
                        "registro TLPE truncado");
        }
        const std::size_t index = static_cast<std::size_t>(offset);
        value = static_cast<std::uint16_t>(bytes_[index]) |
                static_cast<std::uint16_t>(bytes_[index + 1U] << 8U);
        (void)code;
        return true;
    }

    [[nodiscard]] bool read_u8(const std::uint64_t offset, std::uint8_t& value,
                               const std::uint32_t phase) {
        if (!has_range(offset, 1U, bytes_.size())) {
            return fail(TL_PE_ERROR_WIRE_FORMAT, phase, offset, 1U,
                        "registro TLPE truncado");
        }
        value = bytes_[static_cast<std::size_t>(offset)];
        return true;
    }

    [[nodiscard]] bool read_u32(const std::uint64_t offset, std::uint32_t& value,
                                const std::uint32_t code, const std::uint32_t phase) {
        if (!has_range(offset, 4U, bytes_.size())) {
            return fail(TL_PE_ERROR_WIRE_FORMAT, phase, offset, 4U,
                        "registro TLPE truncado");
        }
        const std::size_t index = static_cast<std::size_t>(offset);
        value = static_cast<std::uint32_t>(bytes_[index]) |
                (static_cast<std::uint32_t>(bytes_[index + 1U]) << 8U) |
                (static_cast<std::uint32_t>(bytes_[index + 2U]) << 16U) |
                (static_cast<std::uint32_t>(bytes_[index + 3U]) << 24U);
        (void)code;
        return true;
    }

    [[nodiscard]] bool read_u64(const std::uint64_t offset, std::uint64_t& value,
                                const std::uint32_t code, const std::uint32_t phase) {
        if (!has_range(offset, 8U, bytes_.size())) {
            return fail(TL_PE_ERROR_WIRE_FORMAT, phase, offset, 8U,
                        "registro TLPE truncado");
        }
        const std::size_t index = static_cast<std::size_t>(offset);
        value = 0;
        for (std::size_t item = 0; item < 8U; ++item) {
            value |= static_cast<std::uint64_t>(bytes_[index + item]) << (item * 8U);
        }
        (void)code;
        return true;
    }

    [[nodiscard]] bool zero_range(const std::uint64_t offset, const std::uint64_t length,
                                  const std::uint32_t phase) {
        if (!has_range(offset, length, bytes_.size())) {
            return fail(TL_PE_ERROR_WIRE_FORMAT, phase, offset, length,
                        "range TLPE fora do buffer");
        }
        const std::size_t begin = static_cast<std::size_t>(offset);
        const std::size_t end = begin + static_cast<std::size_t>(length);
        if (!std::all_of(bytes_.begin() + static_cast<std::ptrdiff_t>(begin),
                         bytes_.begin() + static_cast<std::ptrdiff_t>(end),
                         [](const std::uint8_t value) { return value == 0; })) {
            return fail(TL_PE_ERROR_WIRE_FORMAT, phase, offset, length,
                        "campo reservado TLPE não está zerado");
        }
        return true;
    }

    [[nodiscard]] bool read_ref(const std::uint64_t offset,
                                const std::vector<StringRecord>& strings,
                                std::string& value, const std::uint32_t phase) {
        std::uint64_t data_offset = 0;
        std::uint32_t length = 0;
        std::uint32_t reserved = 0;
        if (!read_u64(offset, data_offset, TL_PE_ERROR_WIRE_FORMAT, phase) ||
            !read_u32(offset + 8U, length, TL_PE_ERROR_WIRE_FORMAT, phase) ||
            !read_u32(offset + 12U, reserved, TL_PE_ERROR_WIRE_FORMAT, phase)) {
            return false;
        }
        if (reserved != 0) {
            return fail(TL_PE_ERROR_WIRE_FORMAT, phase, offset + 12U, reserved,
                        "referência de string TLPE tem flags reservadas");
        }
        if (data_offset == 0 && length == 0) {
            value.clear();
            return true;
        }
        for (const StringRecord& record : strings) {
            if (record.data_offset == data_offset && record.length == length) {
                value.assign(reinterpret_cast<const char*>(bytes_.data() +
                                                            static_cast<std::size_t>(data_offset)),
                             length);
                return true;
            }
        }
        return fail(TL_PE_ERROR_WIRE_FORMAT, phase, offset, data_offset,
                    "referência de string TLPE inválida");
    }

    [[nodiscard]] bool fixed_record(const std::array<Descriptor, kTableCount>& tables,
                                    const std::uint32_t table, const std::uint64_t index,
                                    const std::uint64_t size, std::uint64_t& offset,
                                    const std::uint32_t phase) {
        const Descriptor& descriptor = tables[table];
        if (index >= descriptor.count || size > descriptor.stride) {
            return fail(TL_PE_ERROR_WIRE_FORMAT, phase, index, size,
                        "índice de tabela TLPE inválido");
        }
        std::uint64_t relative = 0;
        if (!multiply_u64(index, descriptor.stride, relative) ||
            !add_u64(descriptor.offset, relative, offset) ||
            !has_range(offset, size, bytes_.size())) {
            return fail(TL_PE_ERROR_WIRE_FORMAT, phase, index, size,
                        "registro TLPE fora do buffer");
        }
        return true;
    }

    [[nodiscard]] bool index_range(const std::array<Descriptor, kTableCount>& tables,
                                   const std::uint32_t table, const std::uint64_t offset,
                                   const std::uint64_t count, const std::uint32_t phase) {
        if (offset > tables[table].count || count > tables[table].count - offset) {
            return fail(TL_PE_ERROR_WIRE_FORMAT, phase, offset, count,
                        "intervalo de índices TLPE inválido");
        }
        return true;
    }

    [[nodiscard]] const tl_pe_error_v1& error() const noexcept { return error_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

private:
    std::span<const std::uint8_t> bytes_;
    tl_pe_error_v1 error_{TL_PE_ERROR_NONE, TL_PE_ERROR_PHASE_NONE, kUnknownOffset, 0};
    std::string message_;
};

[[nodiscard]] bool valid_unwind_operation(const std::uint8_t operation) noexcept {
    return operation <= 5U || operation == 8U || operation == 9U || operation == 10U;
}

[[nodiscard]] bool decode_wire_impl(const std::span<const std::uint8_t> bytes,
                                    PeInfo& info, tl_pe_error_v1& error,
                                    std::string& error_message) {
    Decoder decoder{bytes};
    if (bytes.size() < kHeaderSize || bytes.size() > TL_PE_LIMIT_MAX_SERIALIZED_BYTES) {
        decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE, bytes.size(),
                     kHeaderSize, "tamanho total TLPE inválido");
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    constexpr std::array<std::uint8_t, 4> magic{TL_PE_WIRE_MAGIC_0, TL_PE_WIRE_MAGIC_1,
                                                 TL_PE_WIRE_MAGIC_2, TL_PE_WIRE_MAGIC_3};
    if (!std::equal(magic.begin(), magic.end(), bytes.begin())) {
        decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE, 0, 0,
                     "magic TLPE inválido");
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint32_t header_size = 0;
    std::uint64_t total_size = 0;
    std::uint32_t table_count = 0;
    if (!decoder.read_u16(4, major, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u16(6, minor, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u32(8, header_size, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u64(12, total_size, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u32(20, table_count, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE)) {
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    if (major != TL_PE_WIRE_MAJOR || minor != TL_PE_WIRE_MINOR) {
        decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE, 4,
                     (static_cast<std::uint64_t>(major) << 32U) | minor,
                     "versão TLPE não suportada");
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    if (header_size != TL_PE_WIRE_HEADER_SIZE || total_size != bytes.size() ||
        table_count != TL_PE_WIRE_TABLE_COUNT) {
        decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE, 8, total_size,
                     "cabeçalho TLPE incompatível");
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    std::uint64_t reserved_header = 0;
    if (!decoder.read_u64(24, reserved_header, TL_PE_ERROR_WIRE_FORMAT,
                          TL_PE_ERROR_PHASE_WIRE) || reserved_header != 0) {
        decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE, 24,
                     reserved_header, "campo reservado do cabeçalho TLPE não está zerado");
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }

    std::array<Descriptor, kTableCount> tables{};
    std::uint64_t cursor = kHeaderSize;
    for (std::size_t table = 0; table < kTableCount; ++table) {
        const std::uint64_t descriptor_offset = kDescriptorOffset + table * kDescriptorSize;
        if (!decoder.read_u64(descriptor_offset, tables[table].offset,
                              TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
            !decoder.read_u64(descriptor_offset + 8U, tables[table].count,
                              TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
            !decoder.read_u32(descriptor_offset + 16U, tables[table].stride,
                              TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
            !decoder.read_u32(descriptor_offset + 20U, tables[table].flags,
                              TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE)) {
            error = decoder.error();
            error_message = decoder.message();
            return false;
        }
        const Descriptor& descriptor = tables[table];
        if (descriptor.count == 0) {
            if (descriptor.offset != 0 || descriptor.stride != 0 || descriptor.flags != 0) {
                decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE,
                             descriptor_offset, descriptor.count,
                             "descritor vazio TLPE não está zerado");
                error = decoder.error();
                error_message = decoder.message();
                return false;
            }
            continue;
        }
        if (table == kTableReserved || descriptor.offset < kHeaderSize ||
            descriptor.offset % 8U != 0) {
            decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE,
                         descriptor_offset, descriptor.offset,
                         "offset de tabela TLPE inválido");
            error = decoder.error();
            error_message = decoder.message();
            return false;
        }
        std::uint64_t aligned_cursor = 0;
        if (!align8(cursor, aligned_cursor) || descriptor.offset != aligned_cursor) {
            decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE,
                         descriptor_offset, descriptor.offset,
                         "tabelas TLPE não estão contíguas e alinhadas");
            error = decoder.error();
            error_message = decoder.message();
            return false;
        }
        std::uint64_t table_bytes = 0;
        if (table == TL_PE_WIRE_TABLE_STRINGS) {
            if (descriptor.stride != 0 || descriptor.flags != TL_PE_WIRE_TABLE_FLAG_VARIABLE_RECORDS) {
                decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE,
                             descriptor_offset, descriptor.stride,
                             "descritor de strings TLPE inválido");
                error = decoder.error();
                error_message = decoder.message();
                return false;
            }
        } else {
            if (descriptor.stride == 0 || descriptor.flags != 0 ||
                descriptor.stride != kExpectedStrides[table] ||
                !multiply_u64(descriptor.count, descriptor.stride, table_bytes) ||
                !has_range(descriptor.offset, table_bytes, bytes.size())) {
                decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE,
                             descriptor_offset, descriptor.count,
                             "tamanho de tabela TLPE inválido");
                error = decoder.error();
                error_message = decoder.message();
                return false;
            }
        }
        if (table == TL_PE_WIRE_TABLE_STRINGS) {
            if (descriptor.count > bytes.size() / TL_PE_WIRE_STRING_RECORD_HEADER_SIZE) {
                decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE,
                             descriptor_offset, descriptor.count,
                             "quantidade de strings TLPE impossível");
                error = decoder.error();
                error_message = decoder.message();
                return false;
            }
            std::uint64_t string_cursor = descriptor.offset;
            for (std::uint64_t item = 0; item < descriptor.count; ++item) {
                std::uint32_t length = 0;
                std::uint32_t reserved = 0;
                if (!decoder.read_u32(string_cursor, length, TL_PE_ERROR_WIRE_FORMAT,
                                      TL_PE_ERROR_PHASE_WIRE) ||
                    !decoder.read_u32(string_cursor + 4U, reserved,
                                      TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
                    length > TL_PE_LIMIT_MAX_STRING_BYTES || reserved != 0) {
                    decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE,
                                 string_cursor, length, "registro de string TLPE inválido");
                    error = decoder.error();
                    error_message = decoder.message();
                    return false;
                }
                std::uint64_t record_bytes = 0;
                std::uint64_t aligned_bytes = 0;
                if (!add_u64(8U, length, record_bytes) || !align8(record_bytes, aligned_bytes) ||
                    !has_range(string_cursor, aligned_bytes, bytes.size()) ||
                    !decoder.zero_range(string_cursor + record_bytes,
                                        aligned_bytes - record_bytes,
                                        TL_PE_ERROR_PHASE_WIRE)) {
                    error = decoder.error();
                    error_message = decoder.message();
                    return false;
                }
                string_cursor += aligned_bytes;
            }
            table_bytes = string_cursor - descriptor.offset;
        }
        std::uint64_t end = 0;
        if (!add_u64(descriptor.offset, table_bytes, end) || end > bytes.size()) {
            decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE,
                         descriptor_offset, table_bytes, "fim de tabela TLPE inválido");
            error = decoder.error();
            error_message = decoder.message();
            return false;
        }
        cursor = end;
    }
    if (cursor != bytes.size()) {
        decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE, cursor,
                     bytes.size(), "bytes fora das tabelas TLPE");
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    if (tables[TL_PE_WIRE_TABLE_INFO].count != 1 ||
        tables[TL_PE_WIRE_TABLE_INFO].stride != TL_PE_WIRE_INFO_STRIDE ||
        tables[TL_PE_WIRE_TABLE_INFO].flags != 0) {
        decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE,
                     kDescriptorOffset, tables[TL_PE_WIRE_TABLE_INFO].count,
                     "tabela info TLPE inválida");
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }

    std::vector<StringRecord> strings;
    const Descriptor& string_table = tables[TL_PE_WIRE_TABLE_STRINGS];
    if (string_table.count != 0) {
        std::uint64_t string_cursor = string_table.offset;
        strings.reserve(static_cast<std::size_t>(string_table.count));
        for (std::uint64_t item = 0; item < string_table.count; ++item) {
            std::uint32_t length = 0;
            if (!decoder.read_u32(string_cursor, length, TL_PE_ERROR_WIRE_FORMAT,
                                  TL_PE_ERROR_PHASE_WIRE)) {
                error = decoder.error();
                error_message = decoder.message();
                return false;
            }
            std::uint64_t record_bytes = 0;
            std::uint64_t aligned_bytes = 0;
            if (!add_u64(8U, length, record_bytes) || !align8(record_bytes, aligned_bytes)) {
                decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE,
                             string_cursor, length, "overflow em string TLPE");
                error = decoder.error();
                error_message = decoder.message();
                return false;
            }
            strings.push_back({string_cursor + 8U, length});
            string_cursor += aligned_bytes;
        }
    }

    const std::uint64_t info_offset = tables[TL_PE_WIRE_TABLE_INFO].offset;
    std::uint32_t info_flags = 0;
    std::uint16_t reserved16 = 0;
    if (!decoder.read_u32(info_offset, info_flags, TL_PE_ERROR_WIRE_FORMAT,
                          TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u16(info_offset + 34U, reserved16, TL_PE_ERROR_WIRE_FORMAT,
                          TL_PE_ERROR_PHASE_WIRE) || (info_flags & ~UINT32_C(3)) != 0 ||
        reserved16 != 0) {
        decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE, info_offset,
                     info_flags, "flags da tabela info TLPE inválidas");
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    info = {};
    info.is_pe32_plus = (info_flags & TL_PE_WIRE_INFO_FLAG_PE32_PLUS) != 0;
    info.is_dll = (info_flags & TL_PE_WIRE_INFO_FLAG_DLL) != 0;
    if (!decoder.read_u16(info_offset + 4U, info.machine, TL_PE_ERROR_WIRE_FORMAT,
                          TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u16(info_offset + 6U, info.number_of_sections,
                          TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u32(info_offset + 8U, info.address_of_entry_point,
                          TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u64(info_offset + 12U, info.image_base, TL_PE_ERROR_WIRE_FORMAT,
                          TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u32(info_offset + 20U, info.section_alignment,
                          TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u32(info_offset + 24U, info.size_of_image, TL_PE_ERROR_WIRE_FORMAT,
                          TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u32(info_offset + 28U, info.size_of_headers,
                          TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u16(info_offset + 32U, info.subsystem, TL_PE_ERROR_WIRE_FORMAT,
                          TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u32(info_offset + 92U, info.export_ordinal_base,
                          TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u64(info_offset + 96U, info.tls_info.start_address_of_raw_data,
                          TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u64(info_offset + 104U, info.tls_info.end_address_of_raw_data,
                          TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u64(info_offset + 112U, info.tls_info.address_of_index,
                          TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u64(info_offset + 120U, info.tls_info.address_of_callbacks,
                          TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u32(info_offset + 128U, info.tls_info.size_of_zero_fill,
                          TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) ||
        !decoder.read_u32(info_offset + 132U, info.tls_info.characteristics,
                          TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE)) {
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    const auto read_directory = [&decoder, info_offset](const std::size_t index,
                                                         std::uint32_t& rva,
                                                         std::uint32_t& size) {
        return decoder.read_u32(info_offset + 36U + index * 8U, rva,
                                TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE) &&
               decoder.read_u32(info_offset + 40U + index * 8U, size,
                                TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE);
    };
    if (!read_directory(0, info.import_directory_rva, info.import_directory_size) ||
        !read_directory(1, info.export_directory_rva, info.export_directory_size) ||
        !read_directory(2, info.resource_directory_rva, info.resource_directory_size) ||
        !read_directory(3, info.exception_directory_rva, info.exception_directory_size) ||
        !read_directory(4, info.relocation_directory_rva, info.relocation_directory_size) ||
        !read_directory(5, info.delay_import_directory_rva, info.delay_import_directory_size) ||
        !read_directory(6, info.tls_directory_rva, info.tls_directory_size)) {
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }

    if (tables[TL_PE_WIRE_TABLE_SECTIONS].count > TL_PE_LIMIT_MAX_SECTIONS ||
        info.number_of_sections != tables[TL_PE_WIRE_TABLE_SECTIONS].count) {
        decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_WIRE, info_offset + 6U,
                     tables[TL_PE_WIRE_TABLE_SECTIONS].count,
                     "contagem de seções TLPE inconsistente");
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    const auto read_fixed = [&decoder, &tables](const std::uint32_t table,
                                                 const std::uint64_t index,
                                                 const std::uint64_t size,
                                                 std::uint64_t& offset,
                                                 const std::uint32_t phase) {
        return decoder.fixed_record(tables, table, index, size, offset, phase);
    };
    const auto read_index = [&decoder, &tables](const std::uint32_t table,
                                                 const std::uint64_t offset,
                                                 const std::uint64_t count,
                                                 const std::uint32_t phase) {
        return decoder.index_range(tables, table, offset, count, phase);
    };

    for (std::uint64_t index = 0; index < tables[TL_PE_WIRE_TABLE_SECTIONS].count; ++index) {
        std::uint64_t offset = 0;
        if (!read_fixed(TL_PE_WIRE_TABLE_SECTIONS, index, TL_PE_WIRE_SECTION_STRIDE, offset,
                        TL_PE_ERROR_PHASE_SECTIONS)) {
            error = decoder.error(); error_message = decoder.message(); return false;
        }
        SectionInfo section;
        std::uint32_t reserved = 0;
        if (!decoder.read_ref(offset, strings, section.name, TL_PE_ERROR_PHASE_SECTIONS) ||
            !decoder.read_u32(offset + 16U, section.virtual_address, TL_PE_ERROR_WIRE_FORMAT,
                              TL_PE_ERROR_PHASE_SECTIONS) ||
            !decoder.read_u32(offset + 20U, section.virtual_size, TL_PE_ERROR_WIRE_FORMAT,
                              TL_PE_ERROR_PHASE_SECTIONS) ||
            !decoder.read_u32(offset + 24U, section.raw_data_pointer, TL_PE_ERROR_WIRE_FORMAT,
                              TL_PE_ERROR_PHASE_SECTIONS) ||
            !decoder.read_u32(offset + 28U, section.raw_data_size, TL_PE_ERROR_WIRE_FORMAT,
                              TL_PE_ERROR_PHASE_SECTIONS) ||
            !decoder.read_u32(offset + 32U, section.characteristics, TL_PE_ERROR_WIRE_FORMAT,
                              TL_PE_ERROR_PHASE_SECTIONS) ||
            !decoder.read_u32(offset + 36U, reserved, TL_PE_ERROR_WIRE_FORMAT,
                              TL_PE_ERROR_PHASE_SECTIONS) || reserved != 0) {
            error = decoder.error(); error_message = decoder.message(); return false;
        }
        info.sections.push_back(std::move(section));
    }

    const auto read_dlls = [&decoder, &read_fixed, &read_index, &tables, &strings]
        (const std::uint32_t dll_table, const std::uint32_t symbol_table,
         std::vector<ImportedDll>& destination, const std::uint32_t phase) {
        if (tables[dll_table].count > TL_PE_LIMIT_MAX_IMPORT_DLLS) return false;
        for (std::uint64_t index = 0; index < tables[dll_table].count; ++index) {
            std::uint64_t offset = 0;
            if (!read_fixed(dll_table, index, TL_PE_WIRE_IMPORT_DLL_STRIDE, offset, phase)) return false;
            ImportedDll dll;
            std::uint64_t symbol_offset = 0;
            std::uint64_t symbol_count = 0;
            std::uint32_t reserved0 = 0;
            std::uint32_t reserved1 = 0;
            if (!decoder.read_ref(offset, strings, dll.name, phase) ||
                !decoder.read_u64(offset + 16U, symbol_offset, TL_PE_ERROR_WIRE_FORMAT, phase) ||
                !decoder.read_u64(offset + 24U, symbol_count, TL_PE_ERROR_WIRE_FORMAT, phase) ||
                !decoder.read_u32(offset + 32U, reserved0, TL_PE_ERROR_WIRE_FORMAT, phase) ||
                !decoder.read_u32(offset + 36U, reserved1, TL_PE_ERROR_WIRE_FORMAT, phase) ||
                reserved0 != 0 || reserved1 != 0 || symbol_count > TL_PE_LIMIT_MAX_SYMBOLS_PER_DLL ||
                !read_index(symbol_table, symbol_offset, symbol_count, phase)) return false;
            for (std::uint64_t item = 0; item < symbol_count; ++item) {
                std::uint64_t record = 0;
                if (!read_fixed(symbol_table, symbol_offset + item, TL_PE_WIRE_IMPORT_SYMBOL_STRIDE,
                                record, phase)) return false;
                std::uint32_t flags = 0;
                std::uint16_t ordinal = 0;
                std::uint16_t reserved = 0;
                std::uint32_t iat = 0;
                ImportedSymbol symbol;
                if (!decoder.read_u32(record, flags, TL_PE_ERROR_WIRE_FORMAT, phase) ||
                    !decoder.read_u16(record + 4U, ordinal, TL_PE_ERROR_WIRE_FORMAT, phase) ||
                    !decoder.read_u16(record + 6U, reserved, TL_PE_ERROR_WIRE_FORMAT, phase) ||
                    !decoder.read_ref(record + 8U, strings, symbol.name, phase) ||
                    !decoder.read_u32(record + 24U, iat, TL_PE_ERROR_WIRE_FORMAT, phase) ||
                    !decoder.zero_range(record + 28U, 4U, phase) || (flags & ~UINT32_C(1)) != 0 ||
                    reserved != 0 || ((flags & 1U) != 0 && !symbol.name.empty())) return false;
                symbol.by_ordinal = (flags & 1U) != 0;
                symbol.ordinal = ordinal;
                symbol.iat_rva = iat;
                dll.symbols.push_back(std::move(symbol));
            }
            destination.push_back(std::move(dll));
        }
        return true;
    };
    if (!read_dlls(TL_PE_WIRE_TABLE_IMPORT_DLLS, TL_PE_WIRE_TABLE_IMPORT_SYMBOLS,
                   info.imports, TL_PE_ERROR_PHASE_IMPORTS) ||
        !read_dlls(TL_PE_WIRE_TABLE_DELAY_IMPORT_DLLS, TL_PE_WIRE_TABLE_DELAY_IMPORT_SYMBOLS,
                   info.delay_imports, TL_PE_ERROR_PHASE_DELAY_IMPORTS)) {
        if (decoder.message().empty()) decoder.fail(TL_PE_ERROR_WIRE_FORMAT,
                                                     TL_PE_ERROR_PHASE_WIRE, 0, 0,
                                                     "tabela de imports TLPE inválida");
        error = decoder.error(); error_message = decoder.message(); return false;
    }

    if (tables[TL_PE_WIRE_TABLE_EXPORTS].count > TL_PE_LIMIT_MAX_EXPORT_FUNCTIONS) {
        decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_EXPORTS, 0,
                     tables[TL_PE_WIRE_TABLE_EXPORTS].count, "limite de exports TLPE excedido");
        error = decoder.error(); error_message = decoder.message(); return false;
    }
    std::uint64_t export_name_count = 0;
    for (std::uint64_t index = 0; index < tables[TL_PE_WIRE_TABLE_EXPORTS].count; ++index) {
        std::uint64_t offset = 0;
        if (!read_fixed(TL_PE_WIRE_TABLE_EXPORTS, index, TL_PE_WIRE_EXPORT_STRIDE, offset,
                        TL_PE_ERROR_PHASE_EXPORTS)) { error = decoder.error(); error_message = decoder.message(); return false; }
        std::uint32_t flags = 0;
        std::uint16_t ordinal = 0;
        std::uint16_t reserved16_export = 0;
        std::uint32_t rva = 0;
        if (!decoder.read_u32(offset, flags, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_EXPORTS) ||
            !decoder.read_u16(offset + 4U, ordinal, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_EXPORTS) ||
            !decoder.read_u16(offset + 6U, reserved16_export, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_EXPORTS) ||
            !decoder.read_u32(offset + 8U, rva, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_EXPORTS) ||
            !decoder.zero_range(offset + 12U, 4U, TL_PE_ERROR_PHASE_EXPORTS) ||
            (flags & ~UINT32_C(3)) != 0 || reserved16_export != 0) {
            error = decoder.error(); error_message = decoder.message(); return false;
        }
        ExportedSymbol symbol;
        if (!decoder.read_ref(offset + 16U, strings, symbol.name, TL_PE_ERROR_PHASE_EXPORTS) ||
            !decoder.read_ref(offset + 32U, strings, symbol.forwarder, TL_PE_ERROR_PHASE_EXPORTS) ||
            ((flags & 1U) == 0 && !symbol.name.empty()) ||
            ((flags & 2U) == 0 && !symbol.forwarder.empty())) {
            error = decoder.error(); error_message = decoder.message(); return false;
        }
        symbol.by_name = (flags & 1U) != 0;
        if (symbol.by_name && ++export_name_count > TL_PE_LIMIT_MAX_EXPORT_NAMES) {
            decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_EXPORTS, offset,
                         export_name_count, "limite de nomes de exports excedido");
            error = decoder.error(); error_message = decoder.message(); return false;
        }
        symbol.ordinal = ordinal;
        symbol.rva = rva;
        info.exports.push_back(std::move(symbol));
    }

    if (tables[TL_PE_WIRE_TABLE_TLS_CALLBACKS].count > TL_PE_LIMIT_MAX_TLS_CALLBACKS) {
        decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_TLS, 0,
                     tables[TL_PE_WIRE_TABLE_TLS_CALLBACKS].count,
                     "limite de callbacks TLS excedido");
        error = decoder.error(); error_message = decoder.message(); return false;
    }
    for (std::uint64_t index = 0; index < tables[TL_PE_WIRE_TABLE_TLS_CALLBACKS].count; ++index) {
        std::uint64_t offset = 0;
        if (!read_fixed(TL_PE_WIRE_TABLE_TLS_CALLBACKS, index, TL_PE_WIRE_TLS_CALLBACK_STRIDE,
                        offset, TL_PE_ERROR_PHASE_TLS) ||
            !decoder.read_u64(offset, info.tls_info.callback_vas.emplace_back(),
                              TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_TLS)) {
            error = decoder.error(); error_message = decoder.message(); return false;
        }
    }

    if (tables[TL_PE_WIRE_TABLE_UNWIND_INFOS].count !=
            tables[TL_PE_WIRE_TABLE_RUNTIME_FUNCTIONS].count ||
        tables[TL_PE_WIRE_TABLE_RUNTIME_FUNCTIONS].count > TL_PE_LIMIT_MAX_RUNTIME_FUNCTIONS) {
        decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND, 0,
                     tables[TL_PE_WIRE_TABLE_RUNTIME_FUNCTIONS].count,
                     "tabelas de unwind TLPE inconsistentes");
        error = decoder.error(); error_message = decoder.message(); return false;
    }
    std::vector<UnwindInfo> unwind_infos;
    unwind_infos.reserve(static_cast<std::size_t>(tables[TL_PE_WIRE_TABLE_UNWIND_INFOS].count));
    for (std::uint64_t index = 0; index < tables[TL_PE_WIRE_TABLE_UNWIND_INFOS].count; ++index) {
        std::uint64_t offset = 0;
        if (!read_fixed(TL_PE_WIRE_TABLE_UNWIND_INFOS, index, TL_PE_WIRE_UNWIND_INFO_STRIDE,
                        offset, TL_PE_ERROR_PHASE_UNWIND)) { error = decoder.error(); error_message = decoder.message(); return false; }
        UnwindInfo unwind;
        std::uint8_t wire_flags = 0;
        std::uint16_t reserved = 0;
        if (!decoder.read_u8(offset, unwind.version, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u8(offset + 1U, unwind.flags, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u8(offset + 2U, unwind.prolog_size, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u8(offset + 3U, unwind.frame_register, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u8(offset + 4U, unwind.frame_offset, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u8(offset + 5U, wire_flags, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u16(offset + 6U, reserved, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.zero_range(offset + 60U, 4U, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.zero_range(offset + 64U, 8U, TL_PE_ERROR_PHASE_UNWIND) ||
            (wire_flags & ~UINT8_C(3)) != 0 || reserved != 0) {
            error = decoder.error(); error_message = decoder.message(); return false;
        }
        std::uint64_t code_offset = 0, code_count = 0, epilog_offset = 0, epilog_count = 0;
        if (!decoder.read_u64(offset + 8U, code_offset, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u64(offset + 16U, code_count, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u64(offset + 24U, epilog_offset, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u64(offset + 32U, epilog_count, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND) ||
            !read_index(TL_PE_WIRE_TABLE_UNWIND_CODES, code_offset, code_count, TL_PE_ERROR_PHASE_UNWIND) ||
            !read_index(TL_PE_WIRE_TABLE_UNWIND_EPILOGS, epilog_offset, epilog_count, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u32(offset + 40U, unwind.handler_rva, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u32(offset + 44U, unwind.handler_data_rva, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u32(offset + 48U, unwind.chained_begin_rva, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u32(offset + 52U, unwind.chained_end_rva, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u32(offset + 56U, unwind.chained_unwind_info_rva, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND)) {
            error = decoder.error(); error_message = decoder.message(); return false;
        }
        unwind.has_extended_set_fpreg = (wire_flags & 1U) != 0;
        unwind.has_chained_function = (wire_flags & 2U) != 0;
        for (std::uint64_t item = 0; item < code_count; ++item) {
            std::uint64_t record = 0;
            std::uint8_t code_offset_value = 0, operation = 0, operation_info = 0;
            if (!read_fixed(TL_PE_WIRE_TABLE_UNWIND_CODES, code_offset + item,
                            TL_PE_WIRE_UNWIND_CODE_STRIDE, record, TL_PE_ERROR_PHASE_UNWIND) ||
                !decoder.read_u8(record, code_offset_value, TL_PE_ERROR_PHASE_UNWIND) ||
                !decoder.read_u8(record + 1U, operation, TL_PE_ERROR_PHASE_UNWIND) ||
                !decoder.read_u8(record + 2U, operation_info, TL_PE_ERROR_PHASE_UNWIND) ||
                !decoder.zero_range(record + 3U, 1U, TL_PE_ERROR_PHASE_UNWIND) ||
                !decoder.read_u32(record + 4U, unwind.codes.emplace_back().operand,
                                  TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND) ||
                !valid_unwind_operation(operation)) {
                error = decoder.error(); error_message = decoder.message(); return false;
            }
            UnwindCode& code = unwind.codes.back();
            code.code_offset = code_offset_value;
            code.operation = static_cast<UnwindOperation>(operation);
            code.operation_info = operation_info;
        }
        for (std::uint64_t item = 0; item < epilog_count; ++item) {
            std::uint64_t record = 0;
            if (!read_fixed(TL_PE_WIRE_TABLE_UNWIND_EPILOGS, epilog_offset + item,
                            TL_PE_WIRE_UNWIND_EPILOG_STRIDE, record, TL_PE_ERROR_PHASE_UNWIND) ||
                !decoder.read_u32(record, unwind.epilogs.emplace_back().begin_rva,
                                  TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND) ||
                !decoder.read_u32(record + 4U, unwind.epilogs.back().end_rva,
                                  TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND)) {
                error = decoder.error(); error_message = decoder.message(); return false;
            }
        }
        unwind_infos.push_back(std::move(unwind));
    }
    for (std::uint64_t index = 0; index < tables[TL_PE_WIRE_TABLE_RUNTIME_FUNCTIONS].count; ++index) {
        std::uint64_t offset = 0, unwind_index = 0;
        RuntimeFunction function;
        if (!read_fixed(TL_PE_WIRE_TABLE_RUNTIME_FUNCTIONS, index,
                        TL_PE_WIRE_RUNTIME_FUNCTION_STRIDE, offset, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u32(offset, function.begin_rva, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u32(offset + 4U, function.end_rva, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u32(offset + 8U, function.unwind_info_rva, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.read_u64(offset + 12U, unwind_index, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_UNWIND) ||
            !decoder.zero_range(offset + 20U, 4U, TL_PE_ERROR_PHASE_UNWIND) ||
            unwind_index >= unwind_infos.size()) {
            error = decoder.error(); error_message = decoder.message(); return false;
        }
        function.unwind = unwind_infos[static_cast<std::size_t>(unwind_index)];
        info.runtime_functions.push_back(std::move(function));
    }

    if (tables[TL_PE_WIRE_TABLE_RELOC_BLOCKS].count > TL_PE_LIMIT_MAX_RELOC_BLOCKS) {
        decoder.fail(TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_RELOCATIONS, 0,
                     tables[TL_PE_WIRE_TABLE_RELOC_BLOCKS].count,
                     "limite de blocos de relocation TLPE excedido");
        error = decoder.error(); error_message = decoder.message(); return false;
    }
    for (std::uint64_t index = 0; index < tables[TL_PE_WIRE_TABLE_RELOC_BLOCKS].count; ++index) {
        std::uint64_t offset = 0, entry_offset = 0, entry_count = 0;
        BaseRelocBlock block;
        if (!read_fixed(TL_PE_WIRE_TABLE_RELOC_BLOCKS, index, TL_PE_WIRE_RELOC_BLOCK_STRIDE,
                        offset, TL_PE_ERROR_PHASE_RELOCATIONS) ||
            !decoder.read_u32(offset, block.page_rva, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_RELOCATIONS) ||
            !decoder.zero_range(offset + 4U, 4U, TL_PE_ERROR_PHASE_RELOCATIONS) ||
            !decoder.read_u64(offset + 8U, entry_offset, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_RELOCATIONS) ||
            !decoder.read_u64(offset + 16U, entry_count, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_RELOCATIONS) ||
            !decoder.zero_range(offset + 24U, 8U, TL_PE_ERROR_PHASE_RELOCATIONS) ||
            !read_index(TL_PE_WIRE_TABLE_RELOC_ENTRIES, entry_offset, entry_count,
                        TL_PE_ERROR_PHASE_RELOCATIONS)) {
            error = decoder.error(); error_message = decoder.message(); return false;
        }
        for (std::uint64_t item = 0; item < entry_count; ++item) {
            std::uint64_t record = 0;
            BaseRelocEntry entry;
            if (!read_fixed(TL_PE_WIRE_TABLE_RELOC_ENTRIES, entry_offset + item,
                            TL_PE_WIRE_RELOC_ENTRY_STRIDE, record, TL_PE_ERROR_PHASE_RELOCATIONS) ||
                !decoder.read_u16(record, entry.type, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_RELOCATIONS) ||
                !decoder.read_u16(record + 2U, entry.offset, TL_PE_ERROR_WIRE_FORMAT, TL_PE_ERROR_PHASE_RELOCATIONS) ||
                !decoder.zero_range(record + 4U, 4U, TL_PE_ERROR_PHASE_RELOCATIONS)) {
                error = decoder.error(); error_message = decoder.message(); return false;
            }
            block.entries.push_back(entry);
        }
        info.relocations.push_back(std::move(block));
    }
    error = {TL_PE_ERROR_NONE, TL_PE_ERROR_PHASE_NONE, kUnknownOffset, 0};
    error_message.clear();
    return true;
}

struct FfiCall {
    std::uint32_t status{};
    std::uint64_t required{};
    tl_pe_error_v1 error{};
    std::string message;
};

template <typename Function>
[[nodiscard]] FfiCall invoke_with_message(Function&& function) {
    std::size_t capacity = 256;
    constexpr std::size_t kMaximumMessage = 1024U * 1024U;
    for (;;) {
        std::vector<char> message(capacity, '\0');
        FfiCall result;
        std::uint64_t message_required = 0;
        result.status = function(&result.required, &result.error, message.data(), capacity,
                                 &message_required);
        const auto terminator = std::find(message.begin(), message.end(), '\0');
        if (terminator == message.end()) {
            result.message = "mensagem FFI sem terminador NUL";
        } else {
            result.message.assign(message.data(),
                                  static_cast<std::size_t>(terminator - message.begin()));
        }
        if (result.status != TL_PE_STATUS_BUFFER_TOO_SMALL || message_required <= capacity) {
            return result;
        }
        if (message_required > kMaximumMessage || message_required > std::numeric_limits<std::size_t>::max()) {
            result.status = TL_PE_STATUS_INTERNAL;
            result.error = {TL_PE_ERROR_INTERNAL, TL_PE_ERROR_PHASE_INPUT,
                             TL_PE_ERROR_OFFSET_UNKNOWN, message_required};
            result.message = "mensagem FFI excede o limite do adaptador";
            return result;
        }
        capacity = static_cast<std::size_t>(message_required);
    }
}

[[nodiscard]] const char* fallback_message(const std::string& message) noexcept {
    return message.empty() ? "falha no parser Rust" : message.c_str();
}

[[nodiscard]] RustPeParseResult internal_result(const tl_pe_error_v1 error,
                                                std::string message) {
    RustPeParseResult result;
    result.status = ParseStatus::Malformed;
    result.internal_failure = true;
    result.error = error;
    result.error_message = std::move(message);
    return result;
}

}  // namespace

bool decode_tlpe_v1(const std::span<const std::uint8_t> wire, PeInfo& info,
                    tl_pe_error_v1& error, std::string& error_message) {
    return decode_wire_impl(wire, info, error, error_message);
}

RustPeParseResult parse_pe_rust(const std::span<const std::byte> input) {
    try {
        if (input.size() > std::numeric_limits<std::uint64_t>::max()) {
            return internal_result({TL_PE_ERROR_INPUT_TOO_LARGE, TL_PE_ERROR_PHASE_INPUT,
                                    TL_PE_ERROR_OFFSET_UNKNOWN, input.size()},
                                   "entrada excede o limite da ABI Rust");
        }
        const auto* input_bytes = reinterpret_cast<const std::uint8_t*>(input.data());
        const FfiCall sized = invoke_with_message([&](std::uint64_t* required,
                                                       tl_pe_error_v1* error,
                                                       char* message, std::size_t capacity,
                                                       std::uint64_t* message_required) {
            return tl_pe_parse_v1_size(input_bytes, input.size(), required, error, message,
                                       capacity, message_required);
        });
        if (sized.status != TL_PE_STATUS_SUCCESS) {
            if (sized.status > TL_PE_STATUS_UNSUPPORTED_MECHANISM) {
                return internal_result(sized.error, fallback_message(sized.message));
            }
            RustPeParseResult result;
            result.status = static_cast<ParseStatus>(sized.status);
            result.error = sized.error;
            result.error_message = sized.message;
            return result;
        }
        if (sized.required < TL_PE_WIRE_HEADER_SIZE ||
            sized.required > TL_PE_LIMIT_MAX_SERIALIZED_BYTES ||
            sized.required > std::numeric_limits<std::size_t>::max()) {
            return internal_result({TL_PE_ERROR_OUTPUT_TOO_LARGE, TL_PE_ERROR_PHASE_SERIALIZE,
                                    TL_PE_ERROR_OFFSET_UNKNOWN, sized.required},
                                   "tamanho de saída Rust inválido");
        }
        std::vector<std::uint8_t> wire(static_cast<std::size_t>(sized.required), 0);
        const FfiCall filled = invoke_with_message([&](std::uint64_t* required,
                                                        tl_pe_error_v1* error,
                                                        char* message, std::size_t capacity,
                                                        std::uint64_t* message_required) {
            return tl_pe_parse_v1_fill(input_bytes, input.size(), wire.data(), wire.size(),
                                       required, error, message, capacity, message_required);
        });
        if (filled.status != TL_PE_STATUS_SUCCESS || filled.required != sized.required) {
            return internal_result(
                filled.error.code == TL_PE_ERROR_NONE
                    ? tl_pe_error_v1{TL_PE_ERROR_INTERNAL, TL_PE_ERROR_PHASE_WIRE,
                                     TL_PE_ERROR_OFFSET_UNKNOWN, filled.required}
                    : filled.error,
                filled.status == TL_PE_STATUS_SUCCESS
                    ? "size e fill Rust produziram tamanhos diferentes"
                    : fallback_message(filled.message));
        }
        PeInfo info;
        tl_pe_error_v1 decode_error{};
        std::string decode_message;
        if (!decode_tlpe_v1(wire, info, decode_error, decode_message)) {
            return internal_result(decode_error, fallback_message(decode_message));
        }
        RustPeParseResult result;
        result.info = std::move(info);
        result.error = {TL_PE_ERROR_NONE, TL_PE_ERROR_PHASE_NONE, TL_PE_ERROR_OFFSET_UNKNOWN, 0};
        return result;
    } catch (const std::exception& exception) {
        return internal_result({TL_PE_ERROR_INTERNAL, TL_PE_ERROR_PHASE_NONE,
                                TL_PE_ERROR_OFFSET_UNKNOWN, 0}, exception.what());
    } catch (...) {
        return internal_result({TL_PE_ERROR_INTERNAL, TL_PE_ERROR_PHASE_NONE,
                                TL_PE_ERROR_OFFSET_UNKNOWN, 0},
                               "exceção desconhecida no adaptador Rust");
    }
}

}  // namespace tradutorlinux::pe
