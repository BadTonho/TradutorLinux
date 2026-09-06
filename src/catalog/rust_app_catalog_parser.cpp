#include "tradutorlinux/catalog/rust_app_catalog_parser.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tradutorlinux::catalog {
namespace {

constexpr std::uint64_t kUnknownOffset = TL_APP_CATALOG_ERROR_OFFSET_UNKNOWN;
constexpr std::size_t kHeaderSize = TL_APP_CATALOG_WIRE_HEADER_SIZE;
constexpr std::size_t kDescriptorSize = TL_APP_CATALOG_WIRE_TABLE_DESCRIPTOR_SIZE;
constexpr std::size_t kDescriptorOffset = TL_APP_CATALOG_WIRE_TABLE_DESCRIPTOR_OFFSET;
constexpr std::size_t kTableCount = TL_APP_CATALOG_WIRE_TABLE_COUNT;

struct Descriptor {
    std::uint64_t offset{};
    std::uint64_t count{};
    std::uint32_t stride{};
    std::uint32_t flags{};
};

struct StringRecord {
    std::uint64_t data_offset{};
    std::uint64_t length{};
};

struct RefKey {
    std::uint64_t offset{};
    std::uint64_t length{};

    friend bool operator==(const RefKey&, const RefKey&) = default;
};

struct RefKeyHash {
    [[nodiscard]] std::size_t operator()(const RefKey& key) const noexcept {
        const std::size_t first = std::hash<std::uint64_t>{}(key.offset);
        const std::size_t second = std::hash<std::uint64_t>{}(key.length);
        return first ^ (second + static_cast<std::size_t>(0x9e3779b9U) +
                        (first << 6U) + (first >> 2U));
    }
};

[[nodiscard]] bool add_u64(const std::uint64_t left, const std::uint64_t right,
                           std::uint64_t& result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool multiply_u64(const std::uint64_t left, const std::uint64_t right,
                                std::uint64_t& result) noexcept {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) return false;
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

[[nodiscard]] bool is_safe_app_id(const std::string_view value) noexcept {
    if (value.empty() || value.size() > 128U || value == "." || value == "..") return false;
    return std::all_of(value.begin(), value.end(), [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
               (byte >= '0' && byte <= '9') || character == '_' || character == '-' ||
               character == '.';
    });
}

class Decoder {
public:
    explicit Decoder(const std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    bool fail(const std::uint32_t code, const std::uint32_t phase,
              const std::uint64_t offset, const std::uint64_t detail, const char* message) {
        if (!message_.empty()) return false;
        error_ = {code, phase, offset, detail};
        message_ = message;
        return false;
    }

    [[nodiscard]] bool read_u16(const std::uint64_t offset, std::uint16_t& value) {
        if (!has_range(offset, 2U, size_u64())) {
            return fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                        offset, 2U, "campo TLAC truncado");
        }
        const auto index = static_cast<std::size_t>(offset);
        value = static_cast<std::uint16_t>(bytes_[index]) |
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes_[index + 1U]) << 8U);
        return true;
    }

    [[nodiscard]] bool read_u32(const std::uint64_t offset, std::uint32_t& value) {
        if (!has_range(offset, 4U, size_u64())) {
            return fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                        offset, 4U, "campo TLAC truncado");
        }
        const auto index = static_cast<std::size_t>(offset);
        value = static_cast<std::uint32_t>(bytes_[index]) |
                (static_cast<std::uint32_t>(bytes_[index + 1U]) << 8U) |
                (static_cast<std::uint32_t>(bytes_[index + 2U]) << 16U) |
                (static_cast<std::uint32_t>(bytes_[index + 3U]) << 24U);
        return true;
    }

    [[nodiscard]] bool read_u64(const std::uint64_t offset, std::uint64_t& value) {
        if (!has_range(offset, 8U, size_u64())) {
            return fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                        offset, 8U, "campo TLAC truncado");
        }
        const auto index = static_cast<std::size_t>(offset);
        value = 0;
        for (std::size_t item = 0; item < 8U; ++item) {
            value |= static_cast<std::uint64_t>(bytes_[index + item]) << (item * 8U);
        }
        return true;
    }

    [[nodiscard]] bool zero_range(const std::uint64_t offset, const std::uint64_t length) {
        if (!has_range(offset, length, size_u64())) {
            return fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                        offset, length, "range TLAC fora do buffer");
        }
        const auto begin = static_cast<std::size_t>(offset);
        const auto end = begin + static_cast<std::size_t>(length);
        for (std::size_t index = begin; index < end; ++index) {
            if (bytes_[index] != 0U) {
                return fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                            offset, length, "padding ou campo reservado TLAC nao esta zerado");
            }
        }
        return true;
    }

    [[nodiscard]] bool fixed_record(const Descriptor& descriptor, const std::uint64_t index,
                                    std::uint64_t& offset) {
        std::uint64_t relative = 0;
        if (!multiply_u64(index, descriptor.stride, relative) ||
            !add_u64(descriptor.offset, relative, offset) ||
            !has_range(offset, descriptor.stride, size_u64())) {
            return fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                        descriptor.offset, index, "registro TLAC fora da tabela");
        }
        return true;
    }

    [[nodiscard]] bool read_ref(const std::uint64_t offset,
                                const std::unordered_map<RefKey, std::size_t, RefKeyHash>& refs,
                                const std::vector<StringRecord>& strings, std::string& value) {
        std::uint64_t data_offset = 0;
        std::uint64_t length = 0;
        std::uint64_t length_offset = 0;
        if (!add_u64(offset, 8U, length_offset) || !read_u64(offset, data_offset) ||
            !read_u64(length_offset, length)) {
            return false;
        }
        if (data_offset == 0U && length == 0U) {
            value.clear();
            return true;
        }
        if (data_offset == 0U || length == 0U || length > TL_APP_CATALOG_LIMIT_MAX_STRING_BYTES ||
            length > std::numeric_limits<std::size_t>::max()) {
            return fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                        offset, data_offset, "referencia de string TLAC invalida");
        }
        const RefKey key{data_offset, length};
        const auto found = refs.find(key);
        if (found == refs.end() || found->second >= strings.size()) {
            return fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                        offset, data_offset, "referencia de string TLAC sem registro");
        }
        const auto begin = static_cast<std::size_t>(data_offset);
        value.assign(reinterpret_cast<const char*>(bytes_.data() + begin),
                     static_cast<std::size_t>(length));
        return true;
    }

    [[nodiscard]] const tl_app_catalog_error_v1& error() const noexcept { return error_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

private:
    [[nodiscard]] std::uint64_t size_u64() const noexcept {
        return static_cast<std::uint64_t>(bytes_.size());
    }

    std::span<const std::uint8_t> bytes_;
    tl_app_catalog_error_v1 error_{TL_APP_CATALOG_ERROR_WIRE_FORMAT,
                                   TL_APP_CATALOG_ERROR_PHASE_WIRE, kUnknownOffset, 0};
    std::string message_;
};

[[nodiscard]] bool descriptor_range(Decoder& decoder, const Descriptor& descriptor,
                                    const std::uint32_t expected_stride,
                                    const std::uint32_t expected_flags,
                                    const std::uint64_t count_limit,
                                    const std::uint64_t total, std::uint64_t& end) {
    if (descriptor.count > count_limit) {
        return decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                            descriptor.offset, descriptor.count, "contagem TLAC excede o limite");
    }
    if (descriptor.count == 0U) {
        // The Rust serializer keeps the canonical stride/flag in empty fixed
        // tables and keeps the variable-record flag in an empty strings table.
        if (descriptor.offset != 0U || descriptor.stride != expected_stride ||
            descriptor.flags != expected_flags) {
            return decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT,
                                TL_APP_CATALOG_ERROR_PHASE_WIRE, descriptor.offset,
                                descriptor.count, "descritor vazio TLAC invalido");
        }
        end = 0;
        return true;
    }
    if (descriptor.offset < kHeaderSize || (descriptor.offset & 7U) != 0U ||
        descriptor.stride != expected_stride || descriptor.flags != expected_flags) {
        return decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                            descriptor.offset, descriptor.stride, "descritor TLAC invalido");
    }
    if (expected_stride == 0U) {
        end = total;
        return has_range(descriptor.offset, 0U, total) ||
               decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                            descriptor.offset, descriptor.count,
                            "tabela variavel TLAC fora do buffer");
    }
    std::uint64_t length = 0;
    if (!multiply_u64(descriptor.count, descriptor.stride, length) ||
        !add_u64(descriptor.offset, length, end) ||
        !has_range(descriptor.offset, length, total)) {
        return decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                            descriptor.offset, descriptor.count, "intervalo TLAC invalido");
    }
    return true;
}

[[nodiscard]] bool fail_from_decoder(Decoder& decoder, tl_app_catalog_error_v1& error,
                                     std::string& error_message) {
    error = decoder.error();
    error_message = decoder.message();
    return false;
}

struct FfiCall {
    tl_app_catalog_status_t status{};
    std::uint64_t required{};
    tl_app_catalog_error_v1 error{};
    std::string message;
};

template <typename Function>
[[nodiscard]] FfiCall invoke_with_message(Function&& function) {
    constexpr std::size_t kInitialMessageCapacity = 256U;
    constexpr std::size_t kMaximumMessageCapacity = 1024U * 1024U;
    std::size_t capacity = kInitialMessageCapacity;
    for (;;) {
        std::vector<char> message(capacity, '\0');
        FfiCall result;
        std::uint64_t error_required = 0;
        result.status = function(&result.required, &result.error, message.data(),
                                 static_cast<std::uint64_t>(capacity), &error_required);
        if (error_required == 0U) {
            result.status = TL_APP_CATALOG_STATUS_INTERNAL;
            result.error = {TL_APP_CATALOG_ERROR_INTERNAL, TL_APP_CATALOG_ERROR_PHASE_INPUT,
                            kUnknownOffset, 0};
            result.message = "retorno FFI TLAC sem error_required";
            return result;
        }
        if (result.status == TL_APP_CATALOG_STATUS_BUFFER_TOO_SMALL &&
            error_required > static_cast<std::uint64_t>(capacity)) {
            if (error_required > kMaximumMessageCapacity ||
                error_required > std::numeric_limits<std::size_t>::max()) {
                result.status = TL_APP_CATALOG_STATUS_INTERNAL;
                result.error = {TL_APP_CATALOG_ERROR_INTERNAL,
                                TL_APP_CATALOG_ERROR_PHASE_INPUT, kUnknownOffset,
                                error_required};
                result.message = "mensagem FFI TLAC excede o limite do adaptador";
                return result;
            }
            capacity = static_cast<std::size_t>(error_required);
            continue;
        }
        const auto terminator = std::find(message.begin(), message.end(), '\0');
        if (terminator == message.end()) {
            result.status = TL_APP_CATALOG_STATUS_INTERNAL;
            result.error = {TL_APP_CATALOG_ERROR_INTERNAL, TL_APP_CATALOG_ERROR_PHASE_INPUT,
                            kUnknownOffset, 0};
            result.message = "mensagem FFI TLAC sem terminador NUL";
            return result;
        }
        result.message.assign(message.data(), static_cast<std::size_t>(
                                                terminator - message.begin()));
        return result;
    }
}

[[nodiscard]] bool known_status(const tl_app_catalog_status_t status) noexcept {
    return status == TL_APP_CATALOG_STATUS_SUCCESS ||
           status == TL_APP_CATALOG_STATUS_MALFORMED ||
           status == TL_APP_CATALOG_STATUS_UNSUPPORTED_FORMAT ||
           status == TL_APP_CATALOG_STATUS_INVALID_ARGUMENT ||
           status == TL_APP_CATALOG_STATUS_BUFFER_TOO_SMALL ||
           status == TL_APP_CATALOG_STATUS_INPUT_TOO_LARGE ||
           status == TL_APP_CATALOG_STATUS_OUTPUT_TOO_LARGE ||
           status == TL_APP_CATALOG_STATUS_INTERNAL;
}

[[nodiscard]] RustAppCatalogParseResult internal_result(const tl_app_catalog_error_v1 error,
                                                        std::string message) {
    RustAppCatalogParseResult result;
    result.status = TL_APP_CATALOG_STATUS_INTERNAL;
    result.internal_failure = true;
    result.error = error;
    result.error_message = std::move(message);
    return result;
}

}  // namespace

bool decode_tlac_v1(const std::span<const std::uint8_t> wire, std::vector<AppEntry>& apps,
                    tl_app_catalog_error_v1& error, std::string& error_message) {
    Decoder decoder(wire);
    if (wire.size() < kHeaderSize) {
        decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                     wire.size(), kHeaderSize, "buffer TLAC menor que o cabecalho");
        return fail_from_decoder(decoder, error, error_message);
    }
    if (wire.size() > TL_APP_CATALOG_LIMIT_MAX_SERIALIZED_BYTES) {
        decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                     0, wire.size(), "buffer TLAC excede o limite");
        return fail_from_decoder(decoder, error, error_message);
    }
    if (wire[0] != TL_APP_CATALOG_WIRE_MAGIC_0 || wire[1] != TL_APP_CATALOG_WIRE_MAGIC_1 ||
        wire[2] != TL_APP_CATALOG_WIRE_MAGIC_2 || wire[3] != TL_APP_CATALOG_WIRE_MAGIC_3) {
        decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE, 0, 0,
                     "magic TLAC invalido");
        return fail_from_decoder(decoder, error, error_message);
    }

    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint32_t header_size = 0;
    std::uint64_t total_size = 0;
    std::uint32_t table_count = 0;
    std::uint32_t header_flags = 0;
    std::uint32_t header_reserved = 0;
    if (!decoder.read_u16(4, major) || !decoder.read_u16(6, minor) ||
        !decoder.read_u32(8, header_size) || !decoder.read_u64(12, total_size) ||
        !decoder.read_u32(20, table_count) || !decoder.read_u32(24, header_flags) ||
        !decoder.read_u32(28, header_reserved)) {
        return fail_from_decoder(decoder, error, error_message);
    }
    if (major != TL_APP_CATALOG_WIRE_MAJOR || minor != TL_APP_CATALOG_WIRE_MINOR ||
        header_size != TL_APP_CATALOG_WIRE_HEADER_SIZE || total_size != wire.size() ||
        total_size > TL_APP_CATALOG_LIMIT_MAX_SERIALIZED_BYTES ||
        table_count != TL_APP_CATALOG_WIRE_TABLE_COUNT || header_flags != 0U ||
        header_reserved != 0U) {
        decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE, 4,
                     major, "versao ou cabecalho TLAC invalido");
        return fail_from_decoder(decoder, error, error_message);
    }

    std::array<Descriptor, kTableCount> tables{};
    for (std::size_t index = 0; index < kTableCount; ++index) {
        const std::size_t base = kDescriptorOffset + index * kDescriptorSize;
        if (!decoder.read_u64(base + TL_APP_CATALOG_WIRE_DESCRIPTOR_OFFSET_FIELD,
                              tables[index].offset) ||
            !decoder.read_u64(base + TL_APP_CATALOG_WIRE_DESCRIPTOR_COUNT_FIELD,
                              tables[index].count) ||
            !decoder.read_u32(base + TL_APP_CATALOG_WIRE_DESCRIPTOR_STRIDE_FIELD,
                              tables[index].stride) ||
            !decoder.read_u32(base + TL_APP_CATALOG_WIRE_DESCRIPTOR_FLAGS_FIELD,
                              tables[index].flags)) {
            return fail_from_decoder(decoder, error, error_message);
        }
    }

    if (tables[TL_APP_CATALOG_WIRE_TABLE_INFO].offset != kHeaderSize) {
        decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                     tables[TL_APP_CATALOG_WIRE_TABLE_INFO].offset, kHeaderSize,
                     "tabela info TLAC deve seguir o cabecalho");
        return fail_from_decoder(decoder, error, error_message);
    }
    std::array<std::uint64_t, kTableCount> ends{};
    if (!descriptor_range(decoder, tables[TL_APP_CATALOG_WIRE_TABLE_INFO],
                          TL_APP_CATALOG_WIRE_INFO_STRIDE, 0U, 1U, total_size, ends[0]) ||
        tables[TL_APP_CATALOG_WIRE_TABLE_INFO].count != 1U ||
        !descriptor_range(decoder, tables[TL_APP_CATALOG_WIRE_TABLE_APPS],
                          TL_APP_CATALOG_WIRE_APP_STRIDE, 0U,
                          TL_APP_CATALOG_LIMIT_MAX_APPS, total_size, ends[1]) ||
        !descriptor_range(decoder, tables[TL_APP_CATALOG_WIRE_TABLE_ARGS],
                          TL_APP_CATALOG_WIRE_ARG_STRIDE, 0U,
                          TL_APP_CATALOG_LIMIT_MAX_ARGS, total_size, ends[2]) ||
        !descriptor_range(decoder, tables[TL_APP_CATALOG_WIRE_TABLE_STRINGS], 0U,
                          TL_APP_CATALOG_WIRE_TABLE_FLAG_VARIABLE_RECORDS,
                          TL_APP_CATALOG_LIMIT_MAX_STRINGS, total_size, ends[3])) {
        return fail_from_decoder(decoder, error, error_message);
    }

    std::uint64_t last_end = ends[0];
    std::array<std::size_t, kTableCount> ordered_tables{
        TL_APP_CATALOG_WIRE_TABLE_INFO, TL_APP_CATALOG_WIRE_TABLE_APPS,
        TL_APP_CATALOG_WIRE_TABLE_ARGS, TL_APP_CATALOG_WIRE_TABLE_STRINGS};
    for (const std::size_t table : ordered_tables) {
        if (tables[table].count == 0U) continue;
        if (table != TL_APP_CATALOG_WIRE_TABLE_INFO && tables[table].offset < last_end) {
            decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                         tables[table].offset, table, "tabelas TLAC sobrepostas ou fora de ordem");
            return fail_from_decoder(decoder, error, error_message);
        }
        if (table != TL_APP_CATALOG_WIRE_TABLE_INFO) last_end = ends[table];
    }
    if (tables[TL_APP_CATALOG_WIRE_TABLE_STRINGS].count == 0U && total_size != last_end) {
        decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                     last_end, total_size, "bytes extras apos as tabelas TLAC");
        return fail_from_decoder(decoder, error, error_message);
    }

    std::uint64_t gap_cursor = kHeaderSize;
    for (const std::size_t table : ordered_tables) {
        if (tables[table].count == 0U) continue;
        if (tables[table].offset < gap_cursor ||
            !decoder.zero_range(gap_cursor, tables[table].offset - gap_cursor)) {
            return fail_from_decoder(decoder, error, error_message);
        }
        gap_cursor = ends[table];
    }
    if (!decoder.zero_range(gap_cursor, total_size - gap_cursor)) {
        return fail_from_decoder(decoder, error, error_message);
    }

    std::vector<StringRecord> strings;
    std::unordered_map<RefKey, std::size_t, RefKeyHash> string_refs;
    const Descriptor& string_table = tables[TL_APP_CATALOG_WIRE_TABLE_STRINGS];
    if (string_table.count != 0U) {
        strings.reserve(static_cast<std::size_t>(string_table.count));
        string_refs.reserve(static_cast<std::size_t>(string_table.count));
        std::unordered_set<std::string> string_values;
        string_values.reserve(static_cast<std::size_t>(string_table.count));
        std::uint64_t cursor = string_table.offset;
        std::uint64_t decoded_string_bytes = 0;
        for (std::uint64_t index = 0; index < string_table.count; ++index) {
            std::uint32_t length32 = 0;
            std::uint32_t reserved = 0;
            if (!decoder.read_u32(cursor, length32) || !decoder.read_u32(cursor + 4U, reserved)) {
                return fail_from_decoder(decoder, error, error_message);
            }
            const std::uint64_t length = length32;
            if (reserved != 0U || length == 0U ||
                length > TL_APP_CATALOG_LIMIT_MAX_STRING_BYTES) {
                decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                             cursor, length, "registro de string TLAC invalido");
                return fail_from_decoder(decoder, error, error_message);
            }
            std::uint64_t payload = 0;
            std::uint64_t record_end = 0;
            std::uint64_t next = 0;
            if (!add_u64(cursor, TL_APP_CATALOG_WIRE_STRING_RECORD_HEADER_SIZE, payload) ||
                !add_u64(payload, length, record_end) || !align8(record_end, next) ||
                next > total_size || !has_range(payload, length, total_size)) {
                decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                             cursor, length, "registro de string TLAC excede a tabela");
                return fail_from_decoder(decoder, error, error_message);
            }
            if (!decoder.zero_range(record_end, next - record_end)) {
                return fail_from_decoder(decoder, error, error_message);
            }
            if (length > TL_APP_CATALOG_LIMIT_MAX_DECODED_STRING_BYTES - decoded_string_bytes) {
                decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                             cursor, length, "bytes de strings TLAC excedem o limite");
                return fail_from_decoder(decoder, error, error_message);
            }
            const auto payload_begin = static_cast<std::size_t>(payload);
            std::string string_value(reinterpret_cast<const char*>(wire.data() + payload_begin),
                                     static_cast<std::size_t>(length));
            if (!string_values.insert(string_value).second) {
                decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                             cursor, length, "string TLAC duplicada");
                return fail_from_decoder(decoder, error, error_message);
            }
            const std::size_t record_index = strings.size();
            strings.push_back({payload, length});
            string_refs.emplace(RefKey{payload, length}, record_index);
            decoded_string_bytes += length;
            cursor = next;
        }
        if (cursor != total_size || decoded_string_bytes > TL_APP_CATALOG_LIMIT_MAX_DECODED_STRING_BYTES) {
            decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                         cursor, total_size, "tabela de strings TLAC possui bytes extras");
            return fail_from_decoder(decoder, error, error_message);
        }
    }

    std::uint64_t info_record = 0;
    std::uint32_t info_version = 0;
    std::uint32_t info_flags = 0;
    std::uint64_t info_app_count = 0;
    std::uint64_t info_arg_count = 0;
    if (!decoder.fixed_record(tables[TL_APP_CATALOG_WIRE_TABLE_INFO], 0, info_record) ||
        !decoder.read_u32(info_record + TL_APP_CATALOG_WIRE_INFO_VERSION_OFFSET, info_version) ||
        !decoder.read_u32(info_record + TL_APP_CATALOG_WIRE_INFO_FLAGS_OFFSET, info_flags) ||
        !decoder.read_u64(info_record + TL_APP_CATALOG_WIRE_INFO_APP_COUNT_OFFSET,
                          info_app_count) ||
        !decoder.read_u64(info_record + TL_APP_CATALOG_WIRE_INFO_ARG_COUNT_OFFSET,
                          info_arg_count) ||
        !decoder.zero_range(info_record + TL_APP_CATALOG_WIRE_INFO_RESERVED_OFFSET, 8U)) {
        return fail_from_decoder(decoder, error, error_message);
    }
    if (info_version != 1U || info_flags != 0U ||
        info_app_count != tables[TL_APP_CATALOG_WIRE_TABLE_APPS].count ||
        info_arg_count != tables[TL_APP_CATALOG_WIRE_TABLE_ARGS].count) {
        decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                     info_record, info_version, "registro info TLAC invalido");
        return fail_from_decoder(decoder, error, error_message);
    }

    std::vector<std::string> decoded_args;
    decoded_args.reserve(static_cast<std::size_t>(info_arg_count));
    const Descriptor& args_table = tables[TL_APP_CATALOG_WIRE_TABLE_ARGS];
    for (std::uint64_t index = 0; index < args_table.count; ++index) {
        std::uint64_t record = 0;
        std::string value;
        if (!decoder.fixed_record(args_table, index, record) ||
            !decoder.read_ref(record + TL_APP_CATALOG_WIRE_ARG_STRING_OFFSET, string_refs,
                              strings, value)) {
            return fail_from_decoder(decoder, error, error_message);
        }
        decoded_args.push_back(std::move(value));
    }

    std::vector<AppEntry> decoded_apps;
    decoded_apps.reserve(static_cast<std::size_t>(info_app_count));
    std::unordered_set<std::string> ids;
    ids.reserve(static_cast<std::size_t>(info_app_count));
    std::uint64_t expected_arg_index = 0;
    const Descriptor& app_table = tables[TL_APP_CATALOG_WIRE_TABLE_APPS];
    for (std::uint64_t index = 0; index < app_table.count; ++index) {
        std::uint64_t record = 0;
        AppEntry entry;
        std::uint64_t arg_index = 0;
        std::uint64_t arg_count = 0;
        if (!decoder.fixed_record(app_table, index, record) ||
            !decoder.read_ref(record + TL_APP_CATALOG_WIRE_APP_ID_OFFSET, string_refs, strings,
                              entry.id) ||
            !decoder.read_ref(record + TL_APP_CATALOG_WIRE_APP_NAME_OFFSET, string_refs, strings,
                              entry.name) ||
            !decoder.read_ref(record + TL_APP_CATALOG_WIRE_APP_EXECUTABLE_PATH_OFFSET,
                              string_refs, strings, entry.executable_path) ||
            !decoder.read_ref(record + TL_APP_CATALOG_WIRE_APP_PREFIX_PATH_OFFSET, string_refs,
                              strings, entry.prefix_path) ||
            !decoder.read_ref(record + TL_APP_CATALOG_WIRE_APP_ICON_PATH_OFFSET, string_refs,
                              strings, entry.icon_path) ||
            !decoder.read_ref(record + TL_APP_CATALOG_WIRE_APP_WORKING_DIRECTORY_OFFSET,
                              string_refs, strings, entry.working_directory) ||
            !decoder.read_ref(record + TL_APP_CATALOG_WIRE_APP_SHA256_OFFSET, string_refs, strings,
                              entry.app_sha256) ||
            !decoder.read_ref(record + TL_APP_CATALOG_WIRE_APP_VERSION_OFFSET, string_refs,
                              strings, entry.app_version) ||
            !decoder.read_ref(record + TL_APP_CATALOG_WIRE_APP_CREATED_AT_OFFSET, string_refs,
                              strings, entry.created_at) ||
            !decoder.read_u64(record + TL_APP_CATALOG_WIRE_APP_ARGS_INDEX_OFFSET, arg_index) ||
            !decoder.read_u64(record + TL_APP_CATALOG_WIRE_APP_ARGS_COUNT_OFFSET, arg_count) ||
            !decoder.read_u64(record + TL_APP_CATALOG_WIRE_APP_CPU_LIMIT_OFFSET,
                              entry.cpu_limit_seconds) ||
            !decoder.read_u64(record + TL_APP_CATALOG_WIRE_APP_MEMORY_LIMIT_OFFSET,
                              entry.memory_limit_mib) ||
            !decoder.zero_range(record + TL_APP_CATALOG_WIRE_APP_RESERVED_OFFSET, 16U)) {
            return fail_from_decoder(decoder, error, error_message);
        }
        if (!is_safe_app_id(entry.id) || entry.executable_path.empty() ||
            !ids.insert(entry.id).second) {
            decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                         record, index, "entrada de aplicativo TLAC invalida");
            return fail_from_decoder(decoder, error, error_message);
        }
        if (arg_count == 0U) {
            if (arg_index != 0U) {
                decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                             record + TL_APP_CATALOG_WIRE_APP_ARGS_INDEX_OFFSET, arg_index,
                             "indice de argumentos TLAC invalido");
                return fail_from_decoder(decoder, error, error_message);
            }
        } else if (arg_index != expected_arg_index || arg_index > info_arg_count ||
                   arg_count > info_arg_count - arg_index) {
            decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                         record + TL_APP_CATALOG_WIRE_APP_ARGS_INDEX_OFFSET, arg_index,
                         "intervalo de argumentos TLAC invalido");
            return fail_from_decoder(decoder, error, error_message);
        }
        const auto first_arg = static_cast<std::size_t>(arg_index);
        const auto number_of_args = static_cast<std::size_t>(arg_count);
        entry.args.insert(entry.args.end(), decoded_args.begin() +
                                             static_cast<std::ptrdiff_t>(first_arg),
                          decoded_args.begin() + static_cast<std::ptrdiff_t>(first_arg +
                                                                                number_of_args));
        expected_arg_index += arg_count;
        decoded_apps.push_back(std::move(entry));
    }
    if (expected_arg_index != info_arg_count) {
        decoder.fail(TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE,
                     TL_APP_CATALOG_WIRE_INFO_ARG_COUNT_OFFSET, expected_arg_index,
                     "argumentos TLAC nao foram consumidos");
        return fail_from_decoder(decoder, error, error_message);
    }

    apps = std::move(decoded_apps);
    error = {TL_APP_CATALOG_ERROR_NONE, TL_APP_CATALOG_ERROR_PHASE_NONE, kUnknownOffset, 0};
    error_message.clear();
    return true;
}

RustAppCatalogParseResult parse_app_catalog_rust(const std::span<const std::byte> input) {
    RustAppCatalogParseResult result;
    const auto* input_bytes = reinterpret_cast<const std::uint8_t*>(input.data());
    const auto input_length = static_cast<std::uint64_t>(input.size());

    const FfiCall size_call = invoke_with_message([&](std::uint64_t* output_required,
                                                      tl_app_catalog_error_v1* error,
                                                      char* message, const std::uint64_t capacity,
                                                      std::uint64_t* error_required) {
        return tl_app_catalog_parse_v1_size(input_bytes, input_length, output_required, error,
                                             message, capacity, error_required);
    });
    if (size_call.status != TL_APP_CATALOG_STATUS_SUCCESS) {
        if (!known_status(size_call.status) ||
            size_call.status == TL_APP_CATALOG_STATUS_INVALID_ARGUMENT ||
            size_call.status == TL_APP_CATALOG_STATUS_BUFFER_TOO_SMALL ||
            size_call.status == TL_APP_CATALOG_STATUS_INTERNAL) {
            return internal_result(size_call.error, size_call.message);
        }
        result.status = size_call.status;
        result.error = size_call.error;
        result.error_message = size_call.message;
        return result;
    }
    if (size_call.required == 0U ||
        size_call.required > TL_APP_CATALOG_LIMIT_MAX_SERIALIZED_BYTES ||
        size_call.required > std::numeric_limits<std::size_t>::max()) {
        return internal_result(
            {TL_APP_CATALOG_ERROR_INTERNAL, TL_APP_CATALOG_ERROR_PHASE_SERIALIZE, kUnknownOffset,
             size_call.required},
            "tamanho TLAC retornado pelo Rust e invalido");
    }

    std::vector<std::uint8_t> wire(static_cast<std::size_t>(size_call.required), 0U);
    const FfiCall fill_call = invoke_with_message([&](std::uint64_t* output_required,
                                                      tl_app_catalog_error_v1* error,
                                                      char* message, const std::uint64_t capacity,
                                                      std::uint64_t* error_required) {
        return tl_app_catalog_parse_v1_fill(input_bytes, input_length, wire.data(),
                                             static_cast<std::uint64_t>(wire.size()),
                                             output_required, error, message, capacity,
                                             error_required);
    });
    if (fill_call.status != TL_APP_CATALOG_STATUS_SUCCESS) {
        if (!known_status(fill_call.status) ||
            fill_call.status == TL_APP_CATALOG_STATUS_INVALID_ARGUMENT ||
            fill_call.status == TL_APP_CATALOG_STATUS_BUFFER_TOO_SMALL ||
            fill_call.status == TL_APP_CATALOG_STATUS_INTERNAL) {
            return internal_result(fill_call.error, fill_call.message);
        }
        result.status = fill_call.status;
        result.error = fill_call.error;
        result.error_message = fill_call.message;
        return result;
    }
    if (fill_call.required != size_call.required) {
        return internal_result(
            {TL_APP_CATALOG_ERROR_WIRE_FORMAT, TL_APP_CATALOG_ERROR_PHASE_WIRE, kUnknownOffset,
             fill_call.required},
            "size TLAC divergiu de fill");
    }

    tl_app_catalog_error_v1 decode_error{};
    std::string decode_message;
    if (!decode_tlac_v1(wire, result.apps, decode_error, decode_message)) {
        return internal_result(decode_error, std::move(decode_message));
    }
    result.status = TL_APP_CATALOG_STATUS_SUCCESS;
    result.error = {TL_APP_CATALOG_ERROR_NONE, TL_APP_CATALOG_ERROR_PHASE_NONE, kUnknownOffset, 0};
    return result;
}

}  // namespace tradutorlinux::catalog
