#include "tradutorlinux/compat/rust_profile_parser.hpp"

#include "path_rules.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tradutorlinux::compat {
namespace {

constexpr std::size_t kHeaderSize = TL_PROFILE_WIRE_HEADER_SIZE;
constexpr std::size_t kDescriptorSize = TL_PROFILE_WIRE_TABLE_DESCRIPTOR_SIZE;
constexpr std::size_t kDescriptorOffset = TL_PROFILE_WIRE_TABLE_DESCRIPTOR_OFFSET;
constexpr std::size_t kTableCount = TL_PROFILE_WIRE_TABLE_COUNT;
constexpr std::uint64_t kUnknownOffset = TL_PROFILE_ERROR_OFFSET_UNKNOWN;

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
        error_ = {code, phase, offset, detail};
        message_ = message;
        return false;
    }

    [[nodiscard]] bool read_u32(const std::uint64_t offset, std::uint32_t& value) {
        if (!has_range(offset, 4U, bytes_.size())) {
            return fail(TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE, offset, 4U,
                        "registro TLPR truncado");
        }
        const auto index = static_cast<std::size_t>(offset);
        value = static_cast<std::uint32_t>(bytes_[index]) |
                (static_cast<std::uint32_t>(bytes_[index + 1U]) << 8U) |
                (static_cast<std::uint32_t>(bytes_[index + 2U]) << 16U) |
                (static_cast<std::uint32_t>(bytes_[index + 3U]) << 24U);
        return true;
    }

    [[nodiscard]] bool read_u64(const std::uint64_t offset, std::uint64_t& value) {
        if (!has_range(offset, 8U, bytes_.size())) {
            return fail(TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE, offset, 8U,
                        "registro TLPR truncado");
        }
        const auto index = static_cast<std::size_t>(offset);
        value = 0;
        for (std::size_t item = 0; item < 8U; ++item) {
            value |= static_cast<std::uint64_t>(bytes_[index + item]) << (item * 8U);
        }
        return true;
    }

    [[nodiscard]] bool zero_range(const std::uint64_t offset, const std::uint64_t length) {
        if (!has_range(offset, length, bytes_.size())) {
            return fail(TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE, offset,
                        length, "range TLPR fora do buffer");
        }
        const auto begin = static_cast<std::size_t>(offset);
        const auto end = begin + static_cast<std::size_t>(length);
        if (!std::all_of(bytes_.begin() + static_cast<std::ptrdiff_t>(begin),
                         bytes_.begin() + static_cast<std::ptrdiff_t>(end),
                         [](const std::uint8_t value) { return value == 0; })) {
            return fail(TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE, offset,
                        length, "campo reservado TLPR nao esta zerado");
        }
        return true;
    }

    [[nodiscard]] bool fixed_record(const Descriptor& descriptor, const std::uint64_t index,
                                    std::uint64_t& offset) {
        std::uint64_t relative = 0;
        if (!multiply_u64(index, descriptor.stride, relative) ||
            !add_u64(descriptor.offset, relative, offset) ||
            !has_range(offset, descriptor.stride, bytes_.size())) {
            return fail(TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE,
                        descriptor.offset, index, "registro TLPR fora da tabela");
        }
        return true;
    }

    [[nodiscard]] bool read_ref(const std::uint64_t offset,
                                const std::vector<StringRecord>& strings,
                                std::string& value) {
        std::uint64_t data_offset = 0;
        std::uint64_t length = 0;
        if (!read_u64(offset, data_offset) || !read_u64(offset + 8U, length)) return false;
        if (data_offset == 0 && length == 0) {
            value.clear();
            return true;
        }
        if (data_offset == 0 || length == 0 ||
            length > std::numeric_limits<std::size_t>::max()) {
            return fail(TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE, offset,
                        data_offset, "referencia de string TLPR invalida");
        }
        for (const StringRecord& record : strings) {
            if (record.data_offset == data_offset && record.length == length) {
                const auto begin = static_cast<std::size_t>(data_offset);
                value.assign(reinterpret_cast<const char*>(bytes_.data() + begin),
                             static_cast<std::size_t>(length));
                return true;
            }
        }
        return fail(TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE, offset,
                    data_offset, "referencia de string TLPR sem registro");
    }

    [[nodiscard]] const tl_profile_error_v1& error() const noexcept { return error_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

private:
    std::span<const std::uint8_t> bytes_;
    tl_profile_error_v1 error_{TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE,
                               kUnknownOffset, 0};
    std::string message_;
};

[[nodiscard]] bool descriptor_range(Decoder& decoder, const Descriptor& descriptor,
                                    const std::uint32_t expected_stride,
                                    const std::uint32_t expected_flags,
                                    const std::uint64_t total, std::uint64_t& end) {
    if (descriptor.count == 0) {
        if (descriptor.offset != 0 || descriptor.stride != 0 || descriptor.flags != 0) {
            return decoder.fail(TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE,
                                descriptor.offset, descriptor.count,
                                "descritor vazio TLPR nao esta zerado");
        }
        end = 0;
        return true;
    }
    if (descriptor.offset < TL_PROFILE_WIRE_HEADER_SIZE || (descriptor.offset & 7U) != 0U ||
        descriptor.stride != expected_stride || descriptor.flags != expected_flags) {
        return decoder.fail(TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE,
                            descriptor.offset, descriptor.stride, "descritor TLPR invalido");
    }
    if (expected_stride == 0) {
        end = total;
        return has_range(descriptor.offset, 0, total) ||
               decoder.fail(TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE,
                            descriptor.offset, descriptor.count,
                            "tabela variavel TLPR fora do buffer");
    }
    std::uint64_t length = 0;
    if (!multiply_u64(descriptor.count, descriptor.stride, length) ||
        !add_u64(descriptor.offset, length, end) ||
        !has_range(descriptor.offset, length, total)) {
        return decoder.fail(TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE,
                            descriptor.offset, descriptor.count, "intervalo TLPR invalido");
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
            if (tables[left].offset < ends[right] && tables[right].offset < ends[left]) {
                return decoder.fail(TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE,
                                    tables[right].offset, left, "tabelas TLPR sobrepostas");
            }
        }
    }
    return true;
}

[[nodiscard]] bool is_safe_app_id(const std::string_view value) noexcept {
    if (value.empty() || value.size() > 128U || value.find("..") != std::string_view::npos) {
        return false;
    }
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if ((byte < 'a' || byte > 'z') && (byte < 'A' || byte > 'Z') &&
            (byte < '0' || byte > '9') && character != '-' && character != '_' &&
            character != '.') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_hex_string(const std::string_view value, const std::size_t length) noexcept {
    if (value.size() != length) return false;
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') ||
              (byte >= 'A' && byte <= 'F'))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_valid_version(const std::string_view value) noexcept {
    if (value.empty() || value.size() > 128U) return false;
    for (const char character : value) {
        if (static_cast<unsigned char>(character) < 0x20U) return false;
    }
    return true;
}

[[nodiscard]] bool is_valid_backend_min_version(const std::string_view value) noexcept {
    if (value.empty() || value.size() > 64U) return false;
    bool digit_seen = false;
    bool component_digit_seen = false;
    std::size_t components = 0;
    for (const char character : value) {
        if (character >= '0' && character <= '9') {
            digit_seen = true;
            component_digit_seen = true;
        } else if (character == '.') {
            if (!component_digit_seen) return false;
            ++components;
            component_digit_seen = false;
        } else {
            return false;
        }
    }
    return digit_seen && component_digit_seen && components >= 1U && components <= 2U;
}

[[nodiscard]] bool normalized_dll_module(const std::string_view value) noexcept {
    if (value.empty() || value.size() > 255U || value == ".dll" ||
        !value.ends_with(".dll")) {
        return false;
    }
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if ((byte < 'a' || byte > 'z') && (byte < '0' || byte > '9') && character != '-' &&
            character != '_' && character != '.') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_target(const std::string_view first,
                               const std::string_view second) noexcept {
    if (first.size() != second.size()) return false;
    for (std::size_t index = 0; index < first.size(); ++index) {
        const auto normalize = [](const char value) {
            if (value == '/') return '\\';
            if (value >= 'A' && value <= 'Z') return static_cast<char>(value - 'A' + 'a');
            return value;
        };
        if (normalize(first[index]) != normalize(second[index])) return false;
    }
    return true;
}

[[nodiscard]] bool validate_decoded_profile(Decoder& decoder, const Profile& profile) {
    if (!is_safe_app_id(profile.app_id) ||
        (!profile.app_sha256.empty() && !is_hex_string(profile.app_sha256, 64U)) ||
        (!profile.app_version.empty() && !is_valid_version(profile.app_version)) ||
        (profile.schema == 1U && !profile.dlls.empty()) ||
        (profile.schema != 3U && profile.backend_declared) ||
        (profile.backend.kind == BackendKind::Native && !profile.backend.min_version.empty()) ||
        (profile.backend.kind == BackendKind::Proton && !profile.backend.min_version.empty() &&
         !is_valid_backend_min_version(profile.backend.min_version))) {
        return decoder.fail(TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE,
                            TL_PROFILE_WIRE_HEADER_SIZE, profile.schema,
                            "modelo de perfil TLPR invalido");
    }
    std::vector<std::string> modules;
    modules.reserve(profile.dlls.size());
    for (const DllMapping& mapping : profile.dlls) {
        const std::string source = mapping.source.string();
        if (!normalized_dll_module(mapping.module) || !path_rules::is_relative_source(mapping.source) ||
            source.find('\0') != std::string::npos ||
            std::find(modules.begin(), modules.end(), mapping.module) != modules.end()) {
            return decoder.fail(TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE,
                                TL_PROFILE_WIRE_HEADER_SIZE, modules.size(),
                                "mapeamento de DLL TLPR invalido");
        }
        modules.push_back(mapping.module);
    }
    for (std::size_t index = 0; index < profile.files.size(); ++index) {
        const FileMapping& mapping = profile.files[index];
        const std::string source = mapping.source.string();
        if (!path_rules::is_relative_source(mapping.source) || source.find('\0') != std::string::npos ||
            mapping.target.find('\0') != std::string::npos ||
            !path_rules::is_c_drive_path_lexically_confined(mapping.target) ||
            !path_rules::has_target_filename(mapping.target)) {
            return decoder.fail(TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE,
                                TL_PROFILE_WIRE_HEADER_SIZE, index,
                                "mapeamento de arquivo TLPR invalido");
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (profile.files[previous].source == mapping.source ||
                same_target(profile.files[previous].target, mapping.target)) {
                return decoder.fail(TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE,
                                    TL_PROFILE_WIRE_HEADER_SIZE, index,
                                    "mapeamento de arquivo TLPR duplicado");
            }
        }
    }
    return true;
}

[[nodiscard]] bool decode_wire_impl(const std::span<const std::uint8_t> wire, Profile& profile,
                                    tl_profile_error_v1& error, std::string& error_message) {
    Decoder decoder(wire);
    if (wire.size() < kHeaderSize) {
        error = {TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE, wire.size(),
                 kHeaderSize};
        error_message = "buffer TLPR menor que o cabecalho";
        return false;
    }
    if (wire[0] != TL_PROFILE_WIRE_MAGIC_0 || wire[1] != TL_PROFILE_WIRE_MAGIC_1 ||
        wire[2] != TL_PROFILE_WIRE_MAGIC_2 || wire[3] != TL_PROFILE_WIRE_MAGIC_3) {
        error = {TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE, 0, 0};
        error_message = "magic TLPR invalido";
        return false;
    }
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t header_size = 0;
    std::uint64_t total_size = 0;
    std::uint32_t table_count = 0;
    std::uint32_t header_flags = 0;
    std::uint32_t header_reserved = 0;
    std::uint32_t major16 = 0;
    std::uint32_t minor16 = 0;
    // The decoder reads the version fields as two little-endian bytes without
    // relying on host struct layout.
    if (!has_range(4, 2, wire.size()) || !has_range(6, 2, wire.size())) {
        error = {TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE, 4, 2};
        error_message = "versao TLPR truncada";
        return false;
    }
    major16 = static_cast<std::uint32_t>(wire[4]) |
              (static_cast<std::uint32_t>(wire[5]) << 8U);
    minor16 = static_cast<std::uint32_t>(wire[6]) |
              (static_cast<std::uint32_t>(wire[7]) << 8U);
    if (!decoder.read_u32(8, header_size) || !decoder.read_u64(12, total_size) ||
        !decoder.read_u32(20, table_count) || !decoder.read_u32(24, header_flags) ||
        !decoder.read_u32(28, header_reserved)) {
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    major = major16;
    minor = minor16;
    if (major != TL_PROFILE_WIRE_MAJOR || minor != TL_PROFILE_WIRE_MINOR ||
        header_size != TL_PROFILE_WIRE_HEADER_SIZE || total_size != wire.size() ||
        total_size > TL_PROFILE_LIMIT_MAX_SERIALIZED_BYTES ||
        table_count != TL_PROFILE_WIRE_TABLE_COUNT || header_flags != 0 || header_reserved != 0) {
        error = {TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE, 4, major};
        error_message = "versao ou cabecalho TLPR invalido";
        return false;
    }

    std::array<Descriptor, kTableCount> tables{};
    for (std::size_t index = 0; index < kTableCount; ++index) {
        const auto position = kDescriptorOffset + index * kDescriptorSize;
        if (!decoder.read_u64(position, tables[index].offset) ||
            !decoder.read_u64(position + 8U, tables[index].count) ||
            !decoder.read_u32(position + 16U, tables[index].stride) ||
            !decoder.read_u32(position + 20U, tables[index].flags)) {
            error = decoder.error();
            error_message = decoder.message();
            return false;
        }
    }
    std::array<std::uint64_t, kTableCount> ends{};
    if (!descriptor_range(decoder, tables[TL_PROFILE_WIRE_TABLE_INFO],
                          TL_PROFILE_WIRE_INFO_STRIDE, 0, total_size, ends[0]) ||
        !descriptor_range(decoder, tables[TL_PROFILE_WIRE_TABLE_FILES],
                          TL_PROFILE_WIRE_FILE_STRIDE, 0, total_size, ends[1]) ||
        !descriptor_range(decoder, tables[TL_PROFILE_WIRE_TABLE_DLLS],
                          TL_PROFILE_WIRE_DLL_STRIDE, 0, total_size, ends[2]) ||
        !descriptor_range(decoder, tables[TL_PROFILE_WIRE_TABLE_STRINGS], 0,
                          TL_PROFILE_WIRE_TABLE_FLAG_VARIABLE_RECORDS, total_size, ends[3])) {
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    if (tables[TL_PROFILE_WIRE_TABLE_INFO].count != 1U ||
        tables[TL_PROFILE_WIRE_TABLE_FILES].count > TL_PROFILE_LIMIT_MAX_FILES ||
        tables[TL_PROFILE_WIRE_TABLE_DLLS].count > TL_PROFILE_LIMIT_MAX_DLLS ||
        tables[TL_PROFILE_WIRE_TABLE_STRINGS].count > TL_PROFILE_LIMIT_MAX_STRINGS ||
        !ranges_do_not_overlap(tables, ends, decoder)) {
        error = decoder.error();
        if (error.code == TL_PROFILE_ERROR_NONE) {
            error = {TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE,
                     TL_PROFILE_ERROR_OFFSET_UNKNOWN, 0};
        }
        error_message = decoder.message().empty() ? "contagens TLPR invalidas" : decoder.message();
        return false;
    }

    std::vector<StringRecord> strings;
    const Descriptor& string_table = tables[TL_PROFILE_WIRE_TABLE_STRINGS];
    if (string_table.count != 0) {
        strings.reserve(static_cast<std::size_t>(string_table.count));
        std::uint64_t cursor = string_table.offset;
        std::uint64_t total_string_bytes = 0;
        for (std::uint64_t index = 0; index < string_table.count; ++index) {
            std::uint32_t length = 0;
            std::uint32_t reserved = 0;
            if (!decoder.read_u32(cursor, length) || !decoder.read_u32(cursor + 4U, reserved)) {
                error = decoder.error();
                error_message = decoder.message();
                return false;
            }
            if (reserved != 0 || length == 0 || length > TL_PROFILE_LIMIT_MAX_STRING_BYTES) {
                error = {TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE, cursor,
                         length};
                error_message = "registro de string TLPR invalido";
                return false;
            }
            std::uint64_t data_offset = 0;
            std::uint64_t record_end = 0;
            std::uint64_t next = 0;
            if (!add_u64(cursor, TL_PROFILE_WIRE_STRING_RECORD_HEADER_SIZE, data_offset) ||
                !add_u64(data_offset, length, record_end) || !align8(record_end, next) ||
                next > ends[TL_PROFILE_WIRE_TABLE_STRINGS] ||
                !has_range(data_offset, length, ends[TL_PROFILE_WIRE_TABLE_STRINGS])) {
                error = {TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE, cursor,
                         length};
                error_message = "registro de string TLPR excede a tabela";
                return false;
            }
            if (!decoder.zero_range(record_end, next - record_end)) {
                error = decoder.error();
                error_message = decoder.message();
                return false;
            }
            if (length > TL_PROFILE_LIMIT_MAX_STRING_BYTES - total_string_bytes) {
                error = {TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE, cursor,
                         length};
                error_message = "tabela de strings TLPR excede o limite";
                return false;
            }
            total_string_bytes += length;
            strings.push_back({data_offset, length});
            cursor = next;
        }
        if (cursor != ends[TL_PROFILE_WIRE_TABLE_STRINGS] ||
            total_string_bytes > TL_PROFILE_LIMIT_MAX_STRING_BYTES) {
            error = {TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE, cursor,
                     ends[TL_PROFILE_WIRE_TABLE_STRINGS]};
            error_message = "tabela de strings TLPR possui bytes extras";
            return false;
        }
    }

    std::uint64_t info_record = 0;
    const Descriptor& info_table = tables[TL_PROFILE_WIRE_TABLE_INFO];
    if (!decoder.fixed_record(info_table, 0, info_record)) {
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    std::uint32_t schema = 0;
    std::uint32_t backend = 0;
    std::uint32_t flags = 0;
    std::uint32_t reserved = 0;
    if (!decoder.read_u32(info_record + TL_PROFILE_WIRE_INFO_SCHEMA_OFFSET, schema) ||
        !decoder.read_u32(info_record + TL_PROFILE_WIRE_INFO_BACKEND_OFFSET, backend) ||
        !decoder.read_u32(info_record + TL_PROFILE_WIRE_INFO_FLAGS_OFFSET, flags) ||
        !decoder.read_u32(info_record + TL_PROFILE_WIRE_INFO_RESERVED_OFFSET, reserved) ||
        !decoder.zero_range(info_record + TL_PROFILE_WIRE_INFO_TAIL_RESERVED_OFFSET, 16U)) {
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    if ((schema != 1U && schema != 2U && schema != 3U) ||
        (backend != TL_PROFILE_WIRE_BACKEND_NATIVE && backend != TL_PROFILE_WIRE_BACKEND_PROTON) ||
        (flags & ~TL_PROFILE_WIRE_INFO_FLAG_BACKEND_DECLARED) != 0U || reserved != 0U) {
        error = {TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE, info_record, schema};
        error_message = "registro info TLPR invalido";
        return false;
    }

    profile = Profile{};
    profile.schema = schema;
    profile.backend.kind = backend == TL_PROFILE_WIRE_BACKEND_PROTON ? BackendKind::Proton
                                                                       : BackendKind::Native;
    profile.backend_declared = (flags & TL_PROFILE_WIRE_INFO_FLAG_BACKEND_DECLARED) != 0U;
    if (!decoder.read_ref(info_record + TL_PROFILE_WIRE_INFO_APP_ID_OFFSET, strings,
                          profile.app_id) ||
        !decoder.read_ref(info_record + TL_PROFILE_WIRE_INFO_SHA256_OFFSET, strings,
                          profile.app_sha256) ||
        !decoder.read_ref(info_record + TL_PROFILE_WIRE_INFO_VERSION_OFFSET, strings,
                          profile.app_version) ||
        !decoder.read_ref(info_record + TL_PROFILE_WIRE_INFO_MIN_VERSION_OFFSET, strings,
                          profile.backend.min_version)) {
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }

    if ((profile.schema == 1U && tables[TL_PROFILE_WIRE_TABLE_DLLS].count != 0U) ||
        (profile.schema != 3U && profile.backend_declared) ||
        (profile.backend.kind == BackendKind::Native && !profile.backend.min_version.empty())) {
        error = {TL_PROFILE_ERROR_WIRE_FORMAT, TL_PROFILE_ERROR_PHASE_WIRE, info_record, schema};
        error_message = "combinacao de schema TLPR invalida";
        return false;
    }

    const Descriptor& files = tables[TL_PROFILE_WIRE_TABLE_FILES];
    profile.files.reserve(static_cast<std::size_t>(files.count));
    for (std::uint64_t index = 0; index < files.count; ++index) {
        std::uint64_t record = 0;
        std::string source;
        std::string target;
        if (!decoder.fixed_record(files, index, record) ||
            !decoder.read_ref(record + TL_PROFILE_WIRE_FILE_SOURCE_OFFSET, strings, source) ||
            !decoder.read_ref(record + TL_PROFILE_WIRE_FILE_TARGET_OFFSET, strings, target)) {
            error = decoder.error();
            error_message = decoder.message();
            return false;
        }
        profile.files.push_back({std::filesystem::path{source}, std::move(target)});
    }
    const Descriptor& dlls = tables[TL_PROFILE_WIRE_TABLE_DLLS];
    profile.dlls.reserve(static_cast<std::size_t>(dlls.count));
    for (std::uint64_t index = 0; index < dlls.count; ++index) {
        std::uint64_t record = 0;
        std::string module;
        std::string source;
        if (!decoder.fixed_record(dlls, index, record) ||
            !decoder.read_ref(record + TL_PROFILE_WIRE_DLL_MODULE_OFFSET, strings, module) ||
            !decoder.read_ref(record + TL_PROFILE_WIRE_DLL_SOURCE_OFFSET, strings, source)) {
            error = decoder.error();
            error_message = decoder.message();
            return false;
        }
        profile.dlls.push_back({std::move(module), std::filesystem::path{source}});
    }
    if (!validate_decoded_profile(decoder, profile)) {
        error = decoder.error();
        error_message = decoder.message();
        return false;
    }
    error = {TL_PROFILE_ERROR_NONE, TL_PROFILE_ERROR_PHASE_NONE, kUnknownOffset, 0};
    error_message.clear();
    return true;
}

struct FfiCall {
    tl_profile_status_t status{};
    std::uint64_t required{};
    tl_profile_error_v1 error{};
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
        if (result.status != TL_PROFILE_STATUS_BUFFER_TOO_SMALL || message_required <= capacity) {
            return result;
        }
        if (message_required > kMaximumMessage ||
            message_required > std::numeric_limits<std::size_t>::max()) {
            result.status = TL_PROFILE_STATUS_INTERNAL;
            result.error = {TL_PROFILE_ERROR_INTERNAL, TL_PROFILE_ERROR_PHASE_INPUT,
                            TL_PROFILE_ERROR_OFFSET_UNKNOWN, message_required};
            result.message = "mensagem FFI excede o limite do adaptador";
            return result;
        }
        capacity = static_cast<std::size_t>(message_required);
    }
}

[[nodiscard]] RustProfileParseResult internal_result(const tl_profile_error_v1 error,
                                                     std::string message) {
    RustProfileParseResult result;
    result.status = TL_PROFILE_STATUS_INTERNAL;
    result.internal_failure = true;
    result.error = error;
    result.error_message = std::move(message);
    return result;
}

}  // namespace

bool decode_tlpr_v1(const std::span<const std::uint8_t> wire, Profile& profile,
                    tl_profile_error_v1& error, std::string& error_message) {
    return decode_wire_impl(wire, profile, error, error_message);
}

RustProfileParseResult parse_profile_rust(const std::span<const std::byte> input,
                                          const std::string_view expected_app_id,
                                          const std::string_view expected_app_sha256,
                                          const std::string_view expected_app_version) {
    try {
        if (input.size() > std::numeric_limits<std::uint64_t>::max()) {
            return internal_result({TL_PROFILE_ERROR_INPUT_TOO_LARGE, TL_PROFILE_ERROR_PHASE_INPUT,
                                    TL_PROFILE_ERROR_OFFSET_UNKNOWN, input.size()},
                                   "entrada excede o limite da ABI Rust");
        }
        const auto* input_bytes = reinterpret_cast<const std::uint8_t*>(input.data());
        const auto identity = tl_profile_identity_v1{
            reinterpret_cast<const std::uint8_t*>(expected_app_id.data()), expected_app_id.size(),
            reinterpret_cast<const std::uint8_t*>(expected_app_sha256.data()),
            expected_app_sha256.size(), reinterpret_cast<const std::uint8_t*>(expected_app_version.data()),
            expected_app_version.size()};
        const FfiCall sized = invoke_with_message([&](std::uint64_t* required,
                                                       tl_profile_error_v1* error_out,
                                                       char* message, std::size_t capacity,
                                                       std::uint64_t* message_required) {
            return tl_profile_parse_v1_size(input_bytes, input.size(), &identity, required,
                                            error_out, message, capacity, message_required);
        });
        if (sized.status != TL_PROFILE_STATUS_SUCCESS) {
            if (sized.status > TL_PROFILE_STATUS_UNSUPPORTED_FORMAT) {
                return internal_result(sized.error, sized.message.empty()
                                                           ? "falha interna no parser Rust"
                                                           : sized.message);
            }
            RustProfileParseResult result;
            result.status = sized.status;
            result.error = sized.error;
            result.error_message = sized.message;
            return result;
        }
        if (sized.required < TL_PROFILE_WIRE_HEADER_SIZE ||
            sized.required > TL_PROFILE_LIMIT_MAX_SERIALIZED_BYTES ||
            sized.required > std::numeric_limits<std::size_t>::max()) {
            return internal_result({TL_PROFILE_ERROR_OUTPUT_TOO_LARGE,
                                    TL_PROFILE_ERROR_PHASE_SERIALIZE,
                                    TL_PROFILE_ERROR_OFFSET_UNKNOWN, sized.required},
                                   "tamanho de saida TLPR invalido");
        }
        std::vector<std::uint8_t> wire(static_cast<std::size_t>(sized.required), 0);
        const FfiCall filled = invoke_with_message([&](std::uint64_t* required,
                                                        tl_profile_error_v1* error_out,
                                                        char* message, std::size_t capacity,
                                                        std::uint64_t* message_required) {
            return tl_profile_parse_v1_fill(input_bytes, input.size(), &identity, wire.data(),
                                            wire.size(), required, error_out, message, capacity,
                                            message_required);
        });
        if (filled.status != TL_PROFILE_STATUS_SUCCESS || filled.required != sized.required) {
            return internal_result(
                filled.error.code == TL_PROFILE_ERROR_NONE
                    ? tl_profile_error_v1{TL_PROFILE_ERROR_INTERNAL, TL_PROFILE_ERROR_PHASE_WIRE,
                                          TL_PROFILE_ERROR_OFFSET_UNKNOWN, filled.required}
                    : filled.error,
                filled.status == TL_PROFILE_STATUS_SUCCESS
                    ? "size e fill TLPR produziram tamanhos diferentes"
                    : (filled.message.empty() ? "fill TLPR falhou" : filled.message));
        }
        Profile profile;
        tl_profile_error_v1 decode_error{};
        std::string decode_message;
        if (!decode_tlpr_v1(wire, profile, decode_error, decode_message)) {
            return internal_result(decode_error,
                                   decode_message.empty() ? "wire TLPR invalido" : decode_message);
        }
        RustProfileParseResult result;
        result.profile = std::move(profile);
        result.error = {TL_PROFILE_ERROR_NONE, TL_PROFILE_ERROR_PHASE_NONE,
                        TL_PROFILE_ERROR_OFFSET_UNKNOWN, 0};
        return result;
    } catch (const std::exception& exception) {
        return internal_result({TL_PROFILE_ERROR_INTERNAL, TL_PROFILE_ERROR_PHASE_NONE,
                                TL_PROFILE_ERROR_OFFSET_UNKNOWN, 0},
                               exception.what());
    } catch (...) {
        return internal_result({TL_PROFILE_ERROR_INTERNAL, TL_PROFILE_ERROR_PHASE_NONE,
                                TL_PROFILE_ERROR_OFFSET_UNKNOWN, 0},
                               "excecao desconhecida no adaptador TLPR");
    }
}

}  // namespace tradutorlinux::compat
