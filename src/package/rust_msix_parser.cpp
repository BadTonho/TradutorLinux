#include "tradutorlinux/package/rust_msix_parser.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tradutorlinux::package {
namespace {

constexpr std::size_t kHeaderSize = TL_MSIX_WIRE_HEADER_SIZE;
constexpr std::size_t kDescriptorSize = TL_MSIX_WIRE_TABLE_DESCRIPTOR_SIZE;
constexpr std::size_t kDescriptorOffset = TL_MSIX_WIRE_TABLE_DESCRIPTOR_OFFSET;
constexpr std::size_t kTableCount = TL_MSIX_WIRE_TABLE_COUNT;
constexpr std::uint64_t kUnknownOffset = TL_MSIX_ERROR_OFFSET_UNKNOWN;

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

    [[nodiscard]] bool fail(const std::uint32_t code, const std::uint32_t phase,
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
                                const std::uint32_t phase) {
        if (!has_range(offset, 2U, bytes_.size())) {
            return fail(TL_MSIX_ERROR_WIRE_FORMAT, phase, offset, 2U,
                        "registro TLMS truncado");
        }
        const std::size_t index = static_cast<std::size_t>(offset);
        value = static_cast<std::uint16_t>(bytes_[index]) |
                static_cast<std::uint16_t>(bytes_[index + 1U] << 8U);
        return true;
    }

    [[nodiscard]] bool read_u32(const std::uint64_t offset, std::uint32_t& value,
                                const std::uint32_t phase) {
        if (!has_range(offset, 4U, bytes_.size())) {
            return fail(TL_MSIX_ERROR_WIRE_FORMAT, phase, offset, 4U,
                        "registro TLMS truncado");
        }
        const std::size_t index = static_cast<std::size_t>(offset);
        value = static_cast<std::uint32_t>(bytes_[index]) |
                (static_cast<std::uint32_t>(bytes_[index + 1U]) << 8U) |
                (static_cast<std::uint32_t>(bytes_[index + 2U]) << 16U) |
                (static_cast<std::uint32_t>(bytes_[index + 3U]) << 24U);
        return true;
    }

    [[nodiscard]] bool read_u64(const std::uint64_t offset, std::uint64_t& value,
                                const std::uint32_t phase) {
        if (!has_range(offset, 8U, bytes_.size())) {
            return fail(TL_MSIX_ERROR_WIRE_FORMAT, phase, offset, 8U,
                        "registro TLMS truncado");
        }
        const std::size_t index = static_cast<std::size_t>(offset);
        value = 0;
        for (std::size_t item = 0; item < 8U; ++item) {
            value |= static_cast<std::uint64_t>(bytes_[index + item]) << (item * 8U);
        }
        return true;
    }

    [[nodiscard]] bool zero_range(const std::uint64_t offset, const std::uint64_t length,
                                  const std::uint32_t phase) {
        if (!has_range(offset, length, bytes_.size())) {
            return fail(TL_MSIX_ERROR_WIRE_FORMAT, phase, offset, length,
                        "range TLMS fora do buffer");
        }
        const std::size_t begin = static_cast<std::size_t>(offset);
        const std::size_t end = begin + static_cast<std::size_t>(length);
        if (!std::all_of(bytes_.begin() + static_cast<std::ptrdiff_t>(begin),
                         bytes_.begin() + static_cast<std::ptrdiff_t>(end),
                         [](const std::uint8_t value) { return value == 0; })) {
            return fail(TL_MSIX_ERROR_WIRE_FORMAT, phase, offset, length,
                        "campo reservado TLMS não está zerado");
        }
        return true;
    }

    [[nodiscard]] bool read_ref(const std::uint64_t offset,
                                const std::vector<StringRecord>& strings,
                                std::string& value, const std::uint32_t phase) {
        std::uint64_t data_offset = 0;
        std::uint64_t length = 0;
        if (!read_u64(offset, data_offset, phase) || !read_u64(offset + 8U, length, phase)) {
            return false;
        }
        if (data_offset == 0 && length == 0) {
            value.clear();
            return true;
        }
        if (data_offset == 0 || length == 0 || length > std::numeric_limits<std::size_t>::max()) {
            return fail(TL_MSIX_ERROR_WIRE_FORMAT, phase, offset, data_offset,
                        "referência de string TLMS inválida");
        }
        for (const StringRecord& record : strings) {
            if (record.data_offset == data_offset && record.length == length) {
                value.assign(reinterpret_cast<const char*>(bytes_.data() +
                                                           static_cast<std::size_t>(data_offset)),
                             static_cast<std::size_t>(length));
                return true;
            }
        }
        return fail(TL_MSIX_ERROR_WIRE_FORMAT, phase, offset, data_offset,
                    "referência de string TLMS não aponta para registro");
    }

    [[nodiscard]] bool fixed_record(const Descriptor& descriptor, const std::uint64_t index,
                                    std::uint64_t& offset, const std::uint32_t phase) {
        std::uint64_t relative = 0;
        if (!multiply_u64(index, descriptor.stride, relative) ||
            !add_u64(descriptor.offset, relative, offset) ||
            !has_range(offset, descriptor.stride, bytes_.size())) {
            return fail(TL_MSIX_ERROR_WIRE_FORMAT, phase, descriptor.offset, index,
                        "registro TLMS fora da tabela");
        }
        return true;
    }

    [[nodiscard]] const tl_msix_error_v1& error() const noexcept { return error_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

private:
    std::span<const std::uint8_t> bytes_;
    tl_msix_error_v1 error_{TL_MSIX_ERROR_WIRE_FORMAT, TL_MSIX_ERROR_PHASE_WIRE,
                            kUnknownOffset, 0};
    std::string message_;
};

[[nodiscard]] bool descriptor_range(Decoder& decoder, const Descriptor& descriptor,
                                    const std::uint32_t expected_stride,
                                    const std::uint32_t expected_flags,
                                    const std::uint64_t total,
                                    const std::uint32_t phase,
                                    std::uint64_t& end) {
    if (descriptor.count == 0) {
        if (descriptor.offset != 0 || descriptor.stride != 0 || descriptor.flags != 0) {
            return decoder.fail(TL_MSIX_ERROR_WIRE_FORMAT, phase, descriptor.offset,
                                descriptor.count, "descritor vazio TLMS não está zerado");
        }
        end = 0;
        return true;
    }
    if (descriptor.offset < TL_MSIX_WIRE_HEADER_SIZE ||
        (descriptor.offset & 7U) != 0U || descriptor.stride != expected_stride ||
        descriptor.flags != expected_flags) {
        return decoder.fail(TL_MSIX_ERROR_WIRE_FORMAT, phase, descriptor.offset,
                            descriptor.stride, "descritor TLMS inválido");
    }
    if (expected_stride == 0U) {
        if (!has_range(descriptor.offset, 0, total)) {
            return decoder.fail(TL_MSIX_ERROR_WIRE_FORMAT, phase, descriptor.offset,
                                descriptor.count, "tabela variável TLMS fora do buffer");
        }
        end = total;
        return true;
    }
    std::uint64_t length = 0;
    if (!multiply_u64(descriptor.count, descriptor.stride, length) ||
        !add_u64(descriptor.offset, length, end) || !has_range(descriptor.offset, length, total)) {
        return decoder.fail(TL_MSIX_ERROR_WIRE_FORMAT, phase, descriptor.offset,
                            descriptor.count, "intervalo TLMS inválido");
    }
    return true;
}

[[nodiscard]] bool ranges_do_not_overlap(const std::array<Descriptor, kTableCount>& tables,
                                         const std::array<std::uint64_t, kTableCount>& ends,
                                         Decoder& decoder) {
    for (std::size_t left = 0; left < kTableCount; ++left) {
        if (tables[left].count == 0) continue;
        for (std::size_t right = left + 1U; right < kTableCount; ++right) {
            if (tables[right].count == 0) continue;
            const bool overlap = tables[left].offset < ends[right] &&
                                 tables[right].offset < ends[left];
            if (overlap) {
                return decoder.fail(TL_MSIX_ERROR_WIRE_FORMAT, TL_MSIX_ERROR_PHASE_WIRE,
                                    tables[right].offset, left,
                                    "tabelas TLMS sobrepostas");
            }
        }
    }
    return true;
}

[[nodiscard]] bool decode_wire_impl(const std::span<const std::uint8_t> wire,
                                    AppxPackageInfo& info,
                                    tl_msix_error_v1& error,
                                    std::string& error_message) {
    Decoder decoder(wire);
    if (wire.size() < kHeaderSize) {
        error = {TL_MSIX_ERROR_WIRE_FORMAT, TL_MSIX_ERROR_PHASE_WIRE, wire.size(), kHeaderSize};
        error_message = "buffer TLMS menor que o cabeçalho";
        return false;
    }
    if (wire[0] != TL_MSIX_WIRE_MAGIC_0 || wire[1] != TL_MSIX_WIRE_MAGIC_1 ||
        wire[2] != TL_MSIX_WIRE_MAGIC_2 || wire[3] != TL_MSIX_WIRE_MAGIC_3) {
        error = {TL_MSIX_ERROR_WIRE_FORMAT, TL_MSIX_ERROR_PHASE_WIRE, 0, 0};
        error_message = "magic TLMS inválido";
        return false;
    }
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint32_t header_size = 0;
    std::uint64_t total_size = 0;
    std::uint32_t table_count = 0;
    std::uint32_t header_flags = 0;
    std::uint32_t header_reserved = 0;
    if (!decoder.read_u16(4, major, TL_MSIX_ERROR_PHASE_WIRE) ||
        !decoder.read_u16(6, minor, TL_MSIX_ERROR_PHASE_WIRE) ||
        !decoder.read_u32(8, header_size, TL_MSIX_ERROR_PHASE_WIRE) ||
        !decoder.read_u64(12, total_size, TL_MSIX_ERROR_PHASE_WIRE) ||
        !decoder.read_u32(20, table_count, TL_MSIX_ERROR_PHASE_WIRE) ||
        !decoder.read_u32(24, header_flags, TL_MSIX_ERROR_PHASE_WIRE) ||
        !decoder.read_u32(28, header_reserved, TL_MSIX_ERROR_PHASE_WIRE)) {
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    if (major != TL_MSIX_WIRE_MAJOR || minor != TL_MSIX_WIRE_MINOR ||
        header_size != TL_MSIX_WIRE_HEADER_SIZE || total_size != wire.size() ||
        total_size > TL_MSIX_LIMIT_MAX_SERIALIZED_BYTES || table_count != kTableCount ||
        header_flags != 0 || header_reserved != 0) {
        error = {TL_MSIX_ERROR_WIRE_FORMAT, TL_MSIX_ERROR_PHASE_WIRE, 4, major};
        error_message = "versão ou cabeçalho TLMS inválido";
        return false;
    }

    std::array<Descriptor, kTableCount> tables{};
    for (std::size_t index = 0; index < kTableCount; ++index) {
        const std::uint64_t position = kDescriptorOffset + index * kDescriptorSize;
        if (!decoder.read_u64(position, tables[index].offset, TL_MSIX_ERROR_PHASE_WIRE) ||
            !decoder.read_u64(position + 8U, tables[index].count, TL_MSIX_ERROR_PHASE_WIRE) ||
            !decoder.read_u32(position + 16U, tables[index].stride, TL_MSIX_ERROR_PHASE_WIRE) ||
            !decoder.read_u32(position + 20U, tables[index].flags, TL_MSIX_ERROR_PHASE_WIRE)) {
            error = decoder.error();
            error_message = decoder.message();
            return false;
        }
    }
    std::array<std::uint64_t, kTableCount> ends{};
    if (!descriptor_range(decoder, tables[TL_MSIX_WIRE_TABLE_INFO], TL_MSIX_WIRE_INFO_STRIDE, 0,
                          total_size, TL_MSIX_ERROR_PHASE_WIRE, ends[0]) ||
        !descriptor_range(decoder, tables[TL_MSIX_WIRE_TABLE_APPLICATIONS],
                          TL_MSIX_WIRE_APPLICATION_STRIDE, 0, total_size,
                          TL_MSIX_ERROR_PHASE_WIRE, ends[1]) ||
        !descriptor_range(decoder, tables[TL_MSIX_WIRE_TABLE_STRINGS], 0,
                          TL_MSIX_WIRE_TABLE_FLAG_VARIABLE_RECORDS, total_size,
                          TL_MSIX_ERROR_PHASE_WIRE, ends[2]) ||
        !descriptor_range(decoder, tables[TL_MSIX_WIRE_TABLE_RESERVED], 0, 0, total_size,
                          TL_MSIX_ERROR_PHASE_WIRE, ends[3])) {
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    if (tables[TL_MSIX_WIRE_TABLE_INFO].count != 1U ||
        tables[TL_MSIX_WIRE_TABLE_APPLICATIONS].count > TL_MSIX_LIMIT_MAX_ZIP_ENTRIES ||
        tables[TL_MSIX_WIRE_TABLE_STRINGS].count > TL_MSIX_LIMIT_MAX_STRINGS ||
        tables[TL_MSIX_WIRE_TABLE_RESERVED].count != 0U ||
        !ranges_do_not_overlap(tables, ends, decoder)) {
        error = decoder.error();
        error_message = decoder.message().empty() ? "contagens TLMS inválidas" : decoder.message();
        return false;
    }

    std::vector<StringRecord> strings;
    const Descriptor& string_table = tables[TL_MSIX_WIRE_TABLE_STRINGS];
    if (string_table.count != 0) {
        strings.reserve(static_cast<std::size_t>(string_table.count));
        std::uint64_t cursor = string_table.offset;
        for (std::uint64_t index = 0; index < string_table.count; ++index) {
            std::uint32_t length = 0;
            std::uint32_t reserved = 0;
            if (!decoder.read_u32(cursor, length, TL_MSIX_ERROR_PHASE_WIRE) ||
                !decoder.read_u32(cursor + 4U, reserved, TL_MSIX_ERROR_PHASE_WIRE) ||
                reserved != 0) {
                if (reserved != 0 && decoder.message().empty()) {
                    (void)decoder.fail(TL_MSIX_ERROR_WIRE_FORMAT, TL_MSIX_ERROR_PHASE_WIRE,
                                       cursor + 4U, reserved,
                                       "registro de string TLMS possui campo reservado");
                }
                error = decoder.error();
                if (error.code == TL_MSIX_ERROR_WIRE_FORMAT && error_message.empty()) {
                    error_message = "registro de string TLMS inválido";
                } else {
                    error_message = decoder.message();
                }
                return false;
            }
            if (length == 0) {
                error = {TL_MSIX_ERROR_WIRE_FORMAT, TL_MSIX_ERROR_PHASE_WIRE, cursor, 0};
                error_message = "registro de string TLMS vazio";
                return false;
            }
            std::uint64_t data_offset = 0;
            if (!add_u64(cursor, TL_MSIX_WIRE_STRING_RECORD_HEADER_SIZE, data_offset) ||
                !has_range(data_offset, length, ends[2])) {
                error = decoder.error();
                error_message = decoder.message();
                return false;
            }
            std::uint64_t record_end = 0;
            std::uint64_t next = 0;
            if (!add_u64(data_offset, length, record_end) || !align8(record_end, next) ||
                next > ends[2]) {
                error = decoder.error();
                error_message = "registro de string TLMS excede a tabela";
                return false;
            }
            if (!decoder.zero_range(record_end, next - record_end, TL_MSIX_ERROR_PHASE_WIRE)) {
                error = decoder.error();
                error_message = decoder.message();
                return false;
            }
            cursor = next;
            strings.push_back({data_offset, length});
        }
        if (cursor != ends[2]) {
            error = {TL_MSIX_ERROR_WIRE_FORMAT, TL_MSIX_ERROR_PHASE_WIRE, cursor, ends[2]};
            error_message = "tabela de strings TLMS possui bytes extras";
            return false;
        }
    }

    const Descriptor& info_table = tables[TL_MSIX_WIRE_TABLE_INFO];
    std::uint64_t info_record = 0;
    if (!decoder.fixed_record(info_table, 0, info_record, TL_MSIX_ERROR_PHASE_WIRE) ||
        !decoder.read_ref(info_record + TL_MSIX_WIRE_INFO_PACKAGE_NAME_OFFSET, strings,
                          info.package_name, TL_MSIX_ERROR_PHASE_WIRE) ||
        !decoder.read_ref(info_record + TL_MSIX_WIRE_INFO_PUBLISHER_OFFSET, strings,
                          info.publisher, TL_MSIX_ERROR_PHASE_WIRE) ||
        !decoder.read_ref(info_record + TL_MSIX_WIRE_INFO_VERSION_OFFSET, strings,
                          info.version, TL_MSIX_ERROR_PHASE_WIRE)) {
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    std::string main_executable;
    if (!decoder.read_ref(info_record + TL_MSIX_WIRE_INFO_MAIN_EXECUTABLE_OFFSET, strings,
                          main_executable, TL_MSIX_ERROR_PHASE_WIRE)) {
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    info.main_executable = main_executable.empty() ? std::nullopt
                                                    : std::optional<std::string>{main_executable};

    info.applications.clear();
    const Descriptor& application_table = tables[TL_MSIX_WIRE_TABLE_APPLICATIONS];
    info.applications.reserve(static_cast<std::size_t>(application_table.count));
    for (std::uint64_t index = 0; index < application_table.count; ++index) {
        std::uint64_t record = 0;
        AppxApplication application;
        if (!decoder.fixed_record(application_table, index, record, TL_MSIX_ERROR_PHASE_WIRE) ||
            !decoder.read_ref(record + TL_MSIX_WIRE_APPLICATION_ID_OFFSET, strings,
                              application.id, TL_MSIX_ERROR_PHASE_WIRE) ||
            !decoder.read_ref(record + TL_MSIX_WIRE_APPLICATION_EXECUTABLE_OFFSET, strings,
                              application.executable, TL_MSIX_ERROR_PHASE_WIRE) ||
            !decoder.read_ref(record + TL_MSIX_WIRE_APPLICATION_DISPLAY_NAME_OFFSET, strings,
                              application.display_name, TL_MSIX_ERROR_PHASE_WIRE) ||
            !decoder.read_ref(record + TL_MSIX_WIRE_APPLICATION_ENTRY_POINT_OFFSET, strings,
                              application.entry_point, TL_MSIX_ERROR_PHASE_WIRE)) {
            error = decoder.error();
            error_message = decoder.message();
            return false;
        }
        info.applications.push_back(std::move(application));
    }
    error = {TL_MSIX_ERROR_NONE, TL_MSIX_ERROR_PHASE_NONE, kUnknownOffset, 0};
    error_message.clear();
    return true;
}

struct FfiCall {
    std::uint32_t status{};
    std::uint64_t required{};
    tl_msix_error_v1 error{};
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
        result.message = terminator == message.end()
                             ? "mensagem FFI sem terminador NUL"
                             : std::string(message.data(),
                                           static_cast<std::size_t>(terminator - message.begin()));
        if (result.status != TL_MSIX_STATUS_BUFFER_TOO_SMALL || message_required <= capacity) {
            return result;
        }
        if (message_required > kMaximumMessage ||
            message_required > std::numeric_limits<std::size_t>::max()) {
            result.status = TL_MSIX_STATUS_INTERNAL;
            result.error = {TL_MSIX_ERROR_INTERNAL, TL_MSIX_ERROR_PHASE_INPUT,
                            TL_MSIX_ERROR_OFFSET_UNKNOWN, message_required};
            result.message = "mensagem FFI excede o limite do adaptador";
            return result;
        }
        capacity = static_cast<std::size_t>(message_required);
    }
}

[[nodiscard]] RustMsixParseResult internal_result(const tl_msix_error_v1 error,
                                                  std::string message) {
    RustMsixParseResult result;
    result.status = TL_MSIX_STATUS_INTERNAL;
    result.internal_failure = true;
    result.error = error;
    result.error_message = std::move(message);
    return result;
}

}  // namespace

bool decode_tlms_v1(const std::span<const std::uint8_t> wire, AppxPackageInfo& info,
                    tl_msix_error_v1& error, std::string& error_message) {
    return decode_wire_impl(wire, info, error, error_message);
}

RustMsixParseResult parse_msix_rust(const std::span<const std::uint8_t> input) {
    try {
        if (input.size() > std::numeric_limits<std::uint64_t>::max()) {
            return internal_result({TL_MSIX_ERROR_INPUT_TOO_LARGE, TL_MSIX_ERROR_PHASE_INPUT,
                                    TL_MSIX_ERROR_OFFSET_UNKNOWN, input.size()},
                                   "entrada excede o limite da ABI Rust");
        }
        const auto* input_bytes = input.data();
        const FfiCall sized = invoke_with_message([&](std::uint64_t* required,
                                                       tl_msix_error_v1* error,
                                                       char* message, std::size_t capacity,
                                                       std::uint64_t* message_required) {
            return tl_msix_parse_v1_size(input_bytes, input.size(), required, error, message,
                                         capacity, message_required);
        });
        if (sized.status != TL_MSIX_STATUS_SUCCESS) {
            if (sized.status > TL_MSIX_STATUS_UNSUPPORTED_MECHANISM) {
                return internal_result(sized.error, sized.message.empty()
                                                       ? "falha interna no parser Rust"
                                                       : sized.message);
            }
            RustMsixParseResult result;
            result.status = sized.status;
            result.error = sized.error;
            result.error_message = sized.message;
            return result;
        }
        if (sized.required < TL_MSIX_WIRE_HEADER_SIZE ||
            sized.required > TL_MSIX_LIMIT_MAX_SERIALIZED_BYTES ||
            sized.required > std::numeric_limits<std::size_t>::max()) {
            return internal_result({TL_MSIX_ERROR_OUTPUT_TOO_LARGE, TL_MSIX_ERROR_PHASE_SERIALIZE,
                                    TL_MSIX_ERROR_OFFSET_UNKNOWN, sized.required},
                                   "tamanho de saída TLMS inválido");
        }
        std::vector<std::uint8_t> wire(static_cast<std::size_t>(sized.required), 0);
        const FfiCall filled = invoke_with_message([&](std::uint64_t* required,
                                                        tl_msix_error_v1* error,
                                                        char* message, std::size_t capacity,
                                                        std::uint64_t* message_required) {
            return tl_msix_parse_v1_fill(input_bytes, input.size(), wire.data(), wire.size(),
                                         required, error, message, capacity, message_required);
        });
        if (filled.status != TL_MSIX_STATUS_SUCCESS || filled.required != sized.required) {
            return internal_result(
                filled.error.code == TL_MSIX_ERROR_NONE
                    ? tl_msix_error_v1{TL_MSIX_ERROR_INTERNAL, TL_MSIX_ERROR_PHASE_WIRE,
                                       TL_MSIX_ERROR_OFFSET_UNKNOWN, filled.required}
                    : filled.error,
                filled.status == TL_MSIX_STATUS_SUCCESS
                    ? "size e fill TLMS produziram tamanhos diferentes"
                    : (filled.message.empty() ? "fill TLMS falhou" : filled.message));
        }
        AppxPackageInfo info;
        tl_msix_error_v1 decode_error{};
        std::string decode_message;
        if (!decode_tlms_v1(wire, info, decode_error, decode_message)) {
            return internal_result(decode_error,
                                   decode_message.empty() ? "wire TLMS inválido" : decode_message);
        }
        RustMsixParseResult result;
        result.info = std::move(info);
        result.error = {TL_MSIX_ERROR_NONE, TL_MSIX_ERROR_PHASE_NONE,
                        TL_MSIX_ERROR_OFFSET_UNKNOWN, 0};
        return result;
    } catch (const std::exception& exception) {
        return internal_result({TL_MSIX_ERROR_INTERNAL, TL_MSIX_ERROR_PHASE_NONE,
                                TL_MSIX_ERROR_OFFSET_UNKNOWN, 0},
                               exception.what());
    } catch (...) {
        return internal_result({TL_MSIX_ERROR_INTERNAL, TL_MSIX_ERROR_PHASE_NONE,
                                TL_MSIX_ERROR_OFFSET_UNKNOWN, 0},
                               "exceção desconhecida no adaptador TLMS");
    }
}

}  // namespace tradutorlinux::package
