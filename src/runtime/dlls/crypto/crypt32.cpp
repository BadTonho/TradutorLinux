#include "tradutorlinux/runtime/crypt32.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"

#include "../../core/runtime_state_common.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tradutorlinux {
namespace {

using Bytes = std::span<const std::uint8_t>;

constexpr std::uint32_t kX509AsnEncoding = 0x00000001U;
constexpr std::uint32_t kPkcs7AsnEncoding = 0x00010000U;
constexpr std::size_t kMaxCertificateSize = 1024U * 1024U;
constexpr std::size_t kMaxNameAttributes = 64U;
constexpr std::size_t kMaxAttributeText = 8192U;

struct DerValue {
    std::uint8_t tag{};
    Bytes value{};
};

bool read_der_value(const Bytes input, std::size_t& offset, DerValue& result) noexcept {
    if (offset >= input.size()) return false;
    result.tag = input[offset++];
    if (offset >= input.size()) return false;
    const std::uint8_t first_length = input[offset++];
    std::size_t length = first_length;
    if ((first_length & 0x80U) != 0U) {
        const std::size_t length_bytes = first_length & 0x7FU;
        if (length_bytes == 0U || length_bytes > sizeof(std::size_t) ||
            length_bytes > input.size() - offset) {
            return false;
        }
        length = 0;
        for (std::size_t index = 0; index < length_bytes; ++index) {
            if (length > (std::numeric_limits<std::size_t>::max() >> 8U)) return false;
            length = (length << 8U) | input[offset++];
        }
    }
    if (length > input.size() - offset) return false;
    result.value = input.subspan(offset, length);
    offset += length;
    return true;
}

bool read_der_tag(const Bytes input, std::size_t& offset, const std::uint8_t expected,
                  Bytes& value) noexcept {
    DerValue result{};
    if (!read_der_value(input, offset, result) || result.tag != expected) return false;
    value = result.value;
    return true;
}

bool oid_to_string(const Bytes oid, std::string& output) noexcept {
    if (oid.empty()) return false;
    output.clear();
    const std::uint8_t first = oid[0];
    output = std::to_string(std::min<std::uint8_t>(first / 40U, 2U));
    output.push_back('.');
    output += std::to_string(static_cast<unsigned int>(first >= 80U ? first - 80U : first % 40U));
    std::uint64_t component = 0;
    bool continuation = false;
    for (std::size_t index = 1; index < oid.size(); ++index) {
        const std::uint8_t byte = oid[index];
        if (component > (std::numeric_limits<std::uint64_t>::max() >> 7U)) return false;
        component = (component << 7U) | static_cast<std::uint64_t>(byte & 0x7FU);
        continuation = (byte & 0x80U) != 0U;
        if (!continuation) {
            output.push_back('.');
            output += std::to_string(component);
            component = 0;
        }
    }
    return !continuation;
}

bool bytes_to_wide(const DerValue& value, std::u16string& output) noexcept {
    if (value.value.size() > kMaxAttributeText) return false;
    output.clear();
    switch (value.tag) {
        case 0x0CU: {  // UTF8String
            output = util::utf8_to_wide(std::string_view(
                reinterpret_cast<const char*>(value.value.data()), value.value.size()));
            return true;
        }
        case 0x1EU:  // BMPString
            if ((value.value.size() & 1U) != 0U) return false;
            output.reserve(value.value.size() / 2U);
            for (std::size_t index = 0; index < value.value.size(); index += 2U) {
                output.push_back(static_cast<char16_t>((value.value[index] << 8U) |
                                                        value.value[index + 1U]));
            }
            return true;
        case 0x12U:  // NumericString
        case 0x13U:  // PrintableString
        case 0x14U:  // TeletexString
        case 0x16U:  // IA5String
            output.reserve(value.value.size());
            for (const std::uint8_t byte : value.value) {
                output.push_back(static_cast<char16_t>(util::cp1252_to_unicode(byte)));
            }
            return true;
        case 0x1CU:  // UniversalString
            if (value.value.size() % 4U != 0U) return false;
            for (std::size_t index = 0; index < value.value.size(); index += 4U) {
                const std::uint32_t codepoint =
                    (static_cast<std::uint32_t>(value.value[index]) << 24U) |
                    (static_cast<std::uint32_t>(value.value[index + 1U]) << 16U) |
                    (static_cast<std::uint32_t>(value.value[index + 2U]) << 8U) |
                    static_cast<std::uint32_t>(value.value[index + 3U]);
                if (codepoint > 0x10FFFFU ||
                    (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
                    return false;
                }
                std::uint16_t units[2]{};
                const std::size_t written = util::utf16_units_for(codepoint, units);
                output.insert(output.end(), units, units + written);
            }
            return true;
        default:
            return false;
    }
}

struct NameAttribute {
    std::string oid;
    std::u16string value;
};

bool parse_name(const Bytes name, std::vector<NameAttribute>& attributes) noexcept {
    attributes.clear();
    std::size_t offset = 0;
    while (offset < name.size()) {
        Bytes set_value{};
        if (!read_der_tag(name, offset, 0x31U, set_value)) return false;
        std::size_t set_offset = 0;
        Bytes sequence_value{};
        if (!read_der_tag(set_value, set_offset, 0x30U, sequence_value) ||
            set_offset != set_value.size()) {
            return false;
        }
        std::size_t sequence_offset = 0;
        Bytes oid_value{};
        if (!read_der_tag(sequence_value, sequence_offset, 0x06U, oid_value)) return false;
        DerValue encoded_value{};
        if (!read_der_value(sequence_value, sequence_offset, encoded_value) ||
            sequence_offset != sequence_value.size()) {
            return false;
        }
        if (attributes.size() >= kMaxNameAttributes) return false;
        NameAttribute attribute{};
        if (!oid_to_string(oid_value, attribute.oid) ||
            !bytes_to_wide(encoded_value, attribute.value)) {
            return false;
        }
        attributes.push_back(std::move(attribute));
    }
    return true;
}

bool extract_certificate_names(const Bytes encoded, Bytes& issuer, Bytes& subject) noexcept {
    std::size_t certificate_offset = 0;
    Bytes certificate_value{};
    if (!read_der_tag(encoded, certificate_offset, 0x30U, certificate_value) ||
        certificate_offset != encoded.size()) {
        return false;
    }
    std::size_t certificate_body_offset = 0;
    Bytes tbs_value{};
    if (!read_der_tag(certificate_value, certificate_body_offset, 0x30U, tbs_value)) return false;

    std::size_t tbs_offset = 0;
    if (tbs_offset < tbs_value.size() && tbs_value[tbs_offset] == 0xA0U) {
        DerValue version{};
        if (!read_der_value(tbs_value, tbs_offset, version)) return false;
    }
    Bytes ignored{};
    if (!read_der_tag(tbs_value, tbs_offset, 0x02U, ignored) ||
        !read_der_tag(tbs_value, tbs_offset, 0x30U, ignored) ||
        !read_der_tag(tbs_value, tbs_offset, 0x30U, issuer) ||
        !read_der_tag(tbs_value, tbs_offset, 0x30U, ignored) ||
        !read_der_tag(tbs_value, tbs_offset, 0x30U, subject)) {
        return false;
    }
    return true;
}

const std::u16string* find_attribute(const std::vector<NameAttribute>& attributes,
                                     const std::string_view oid) noexcept {
    const auto found = std::find_if(attributes.begin(), attributes.end(),
                                    [oid](const NameAttribute& attribute) {
                                        return attribute.oid == oid;
                                    });
    return found == attributes.end() ? nullptr : &found->value;
}

std::u16string select_name(const std::vector<NameAttribute>& attributes,
                           const std::uint32_t type, const void* const type_parameter,
                           bool& found, bool& valid) noexcept {
    found = false;
    valid = true;
    constexpr std::string_view kCommonName = "2.5.4.3";
    constexpr std::string_view kOrganizationUnit = "2.5.4.11";
    constexpr std::string_view kOrganization = "2.5.4.10";
    constexpr std::string_view kEmail = "1.2.840.113549.1.9.1";

    if (type == kCertNameEmailType) {
        if (const auto* value = find_attribute(attributes, kEmail); value != nullptr) {
            found = true;
            return *value;
        }
        return {};
    }
    if (type == kCertNameAttrType) {
        std::string oid;
        if (type_parameter == nullptr ||
            !runtime::copy_guest_cstring(static_cast<const char*>(type_parameter), 128U, oid)) {
            valid = false;
            return {};
        }
        if (const auto* value = find_attribute(attributes, oid); value != nullptr) {
            found = true;
            return *value;
        }
        return {};
    }
    if (type == kCertNameSimpleDisplayType || type == kCertNameFriendlyDisplayType) {
        constexpr std::string_view kPreferred[] = {kCommonName, kOrganizationUnit, kOrganization,
                                                   kEmail};
        for (const std::string_view oid : kPreferred) {
            if (const auto* value = find_attribute(attributes, oid); value != nullptr) {
                found = true;
                return *value;
            }
        }
    } else if (type == kCertNameDnsType) {
        if (const auto* value = find_attribute(attributes, kCommonName); value != nullptr) {
            found = true;
            return *value;
        }
        return {};
    } else if (type == kCertNameRdnType || type > kCertNameDnsType) {
        return {};
    }
    if (!attributes.empty()) {
        found = true;
        return attributes.front().value;
    }
    return {};
}

std::u16string select_name(const std::vector<NameAttribute>& attributes,
                           const std::uint32_t type, const void* const type_parameter,
                           bool& found) noexcept {
    bool valid = true;
    return select_name(attributes, type, type_parameter, found, valid);
}

bool snapshot_cert_context(const GuestCertContext* const source, GuestCertContext& context,
                           std::vector<std::uint8_t>& encoded) noexcept {
    if (source == nullptr ||
        runtime::read_guest_memory(source, &context, sizeof(context)).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        (context.encoding_type != kX509AsnEncoding &&
         context.encoding_type != (kX509AsnEncoding | kPkcs7AsnEncoding)) ||
        context.encoded == nullptr || context.encoded_size == 0U ||
        context.encoded_size > kMaxCertificateSize) {
        return false;
    }
    try {
        encoded.resize(context.encoded_size);
    } catch (...) {
        return false;
    }
    if (runtime::read_guest_memory(context.encoded, encoded.data(), encoded.size()).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        return false;
    }
    context.encoded = encoded.data();
    return true;
}

bool write_guest_wstring(std::uint16_t* const destination,
                         const std::u16string& value) noexcept {
    try {
        std::vector<std::uint16_t> output(value.begin(), value.end());
        output.push_back(0);
        return runtime::write_guest_memory(destination, output.data(),
                                            output.size() * sizeof(std::uint16_t)).status ==
               runtime::GuestMemoryAccessStatus::Success;
    } catch (...) {
        return false;
    }
}

std::u16string name_label(const std::string_view oid, const std::uint32_t string_type) {
    if (string_type == 1U) {
        return {};
    }
    if (string_type == 2U) {
        return util::utf8_to_wide(oid);
    }
    const std::pair<std::string_view, std::string_view> known_names[] = {
        {"2.5.4.3", "CN"},   {"2.5.4.4", "SN"},   {"2.5.4.5", "SERIALNUMBER"},
        {"2.5.4.6", "C"},    {"2.5.4.7", "L"},    {"2.5.4.8", "S"},
        {"2.5.4.10", "O"},   {"2.5.4.11", "OU"},  {"2.5.4.12", "T"},
        {"2.5.4.42", "G"},   {"2.5.4.43", "I"},   {"1.2.840.113549.1.9.1", "E"},
    };
    for (const auto& [known_oid, known_name] : known_names) {
        if (oid == known_oid) {
            return util::utf8_to_wide(known_name);
        }
    }
    return util::utf8_to_wide(std::string{"OID."} + std::string{oid});
}

bool name_value_needs_quotes(const std::u16string& value) noexcept {
    if (value.empty()) return true;
    const auto is_space = [](const char16_t character) noexcept {
        return character == u' ' || character == u'\t' || character == u'\r' || character == u'\n';
    };
    if (is_space(value.front()) || is_space(value.back())) return true;
    for (const char16_t character : value) {
        if (character == u',' || character == u'+' || character == u'=' ||
            character == u'"' || character == u'\\' || character == u'\n' ||
            character == u'<' || character == u'>' || character == u'#' ||
            character == u';') {
            return true;
        }
    }
    return false;
}

void append_name_value(std::u16string& output, const std::u16string& value,
                       const bool no_quoting) {
    if (no_quoting || !name_value_needs_quotes(value)) {
        output += value;
        return;
    }
    output.push_back(u'"');
    for (const char16_t character : value) {
        output.push_back(character);
        if (character == u'"') output.push_back(u'"');
    }
    output.push_back(u'"');
}

std::u16string format_name(const std::vector<NameAttribute>& attributes,
                           const std::uint32_t string_type) {
    constexpr std::uint32_t kSemicolonFlag = 0x40000000U;
    constexpr std::uint32_t kCrlfFlag = 0x08000000U;
    constexpr std::uint32_t kNoPlusFlag = 0x20000000U;
    constexpr std::uint32_t kNoQuotingFlag = 0x10000000U;
    constexpr std::uint32_t kReverseFlag = 0x02000000U;
    const std::uint32_t base_type = string_type & 0xFFU;
    const bool reverse = (string_type & kReverseFlag) != 0U;
    const bool no_quoting = (string_type & kNoQuotingFlag) != 0U;
    const bool semicolon = (string_type & kSemicolonFlag) != 0U;
    const bool crlf = (string_type & kCrlfFlag) != 0U;
    (void)kNoPlusFlag;

    std::u16string output;
    for (std::size_t index = 0; index < attributes.size(); ++index) {
        const std::size_t attribute_index = reverse ? attributes.size() - 1U - index : index;
        if (index != 0U) {
            if (crlf) {
                output += u"\r\n";
            } else if (semicolon) {
                output += u"; ";
            } else {
                output += u", ";
            }
        }
        const NameAttribute& attribute = attributes[attribute_index];
        const std::u16string label = name_label(attribute.oid, base_type);
        if (!label.empty()) {
            output += label;
            output.push_back(u'=');
        }
        append_name_value(output, attribute.value, no_quoting);
    }
    return output;
}

struct Sha1Context {
    std::uint64_t count{0};
    std::array<std::uint32_t, 5> state{0x67452301U, 0xEFCDAB89U, 0x98BADCFEU, 0x10325476U, 0xC3D2E1F0U};
    std::array<std::uint8_t, 64> buffer{};
};

inline std::uint32_t rol(const std::uint32_t value, const std::size_t bits) noexcept {
    return (value << bits) | (value >> (32U - bits));
}

void sha1_transform(std::array<std::uint32_t, 5>& state, const std::uint8_t buffer[64]) noexcept {
    std::array<std::uint32_t, 80> block{};
    for (std::size_t i = 0; i < 16; ++i) {
        block[i] = (static_cast<std::uint32_t>(buffer[i * 4U]) << 24U) |
                   (static_cast<std::uint32_t>(buffer[i * 4U + 1U]) << 16U) |
                   (static_cast<std::uint32_t>(buffer[i * 4U + 2U]) << 8U) |
                   static_cast<std::uint32_t>(buffer[i * 4U + 3U]);
    }
    for (std::size_t i = 16; i < 80; ++i) {
        block[i] = rol(block[i - 3U] ^ block[i - 8U] ^ block[i - 14U] ^ block[i - 16U], 1U);
    }
    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];

    for (std::size_t i = 0; i < 80; ++i) {
        std::uint32_t f = 0;
        std::uint32_t k = 0;
        if (i < 20U) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999U;
        } else if (i < 40U) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1U;
        } else if (i < 60U) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCU;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6U;
        }
        const std::uint32_t temp = rol(a, 5U) + f + e + k + block[i];
        e = d;
        d = c;
        c = rol(b, 30U);
        b = a;
        a = temp;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void sha1_update(Sha1Context& ctx, const std::uint8_t* const data, const std::size_t len) noexcept {
    std::size_t buffer_len = static_cast<std::size_t>((ctx.count >> 3U) & 63U);
    ctx.count += static_cast<std::uint64_t>(len) << 3U;
    const std::size_t part_len = 64U - buffer_len;
    std::size_t i = 0;
    if (len >= part_len) {
        std::copy_n(data, part_len, &ctx.buffer[buffer_len]);
        sha1_transform(ctx.state, ctx.buffer.data());
        for (i = part_len; i + 63U < len; i += 64U) {
            sha1_transform(ctx.state, &data[i]);
        }
        buffer_len = 0;
    }
    if (i < len) {
        std::copy_n(&data[i], len - i, &ctx.buffer[buffer_len]);
    }
}

void sha1_final(Sha1Context& ctx, std::array<std::uint8_t, 20>& digest) noexcept {
    std::array<std::uint8_t, 8> final_count{};
    for (std::size_t i = 0; i < 8; ++i) {
        final_count[i] = static_cast<std::uint8_t>((ctx.count >> ((7U - i) * 8U)) & 0xFFU);
    }
    const std::uint8_t pad = 0x80U;
    sha1_update(ctx, &pad, 1U);
    while ((ctx.count & (63U << 3U)) != (56U << 3U)) {
        const std::uint8_t zero = 0;
        sha1_update(ctx, &zero, 1U);
    }
    sha1_update(ctx, final_count.data(), 8U);
    for (std::size_t i = 0; i < 20; ++i) {
        digest[i] = static_cast<std::uint8_t>((ctx.state[i / 4U] >> ((3U - (i % 4U)) * 8U)) & 0xFFU);
    }
}

std::array<std::uint8_t, 20> compute_sha1(const Bytes data) noexcept {
    Sha1Context ctx{};
    sha1_update(ctx, data.data(), data.size());
    std::array<std::uint8_t, 20> digest{};
    sha1_final(ctx, digest);
    return digest;
}

struct TrackedContext {
    GuestCertContext guest_context{};
    std::vector<std::uint8_t> encoded_storage;
    std::atomic<std::uint32_t> refcount{1};
};

struct TrackedStore {
    std::uint32_t encoding_type{0};
    std::uint32_t flags{0};
    std::vector<const GuestCertContext*> certs;
};

std::mutex g_crypto_mutex;
std::vector<std::unique_ptr<TrackedContext>> g_tracked_contexts;
std::vector<std::unique_ptr<TrackedStore>> g_tracked_stores;

}  // namespace

extern "C" {

TL_CRYPT32_MSABI std::uint32_t tl_CertGetNameStringW(
    const GuestCertContext* const cert_context, const std::uint32_t type,
    const std::uint32_t flags, const void* const type_parameter,
    std::uint16_t* const name_string, const std::uint32_t name_string_capacity) noexcept {
    constexpr std::uint32_t kSupportedFlags = kCertNameIssuerFlag;
    GuestCertContext context{};
    std::vector<std::uint8_t> encoded_storage;
    if (!snapshot_cert_context(cert_context, context, encoded_storage) ||
        (type < kCertNameEmailType || type > kCertNameDnsType || type == kCertNameRdnType) ||
        (type == kCertNameAttrType && type_parameter == nullptr) ||
        (flags & ~kSupportedFlags) != 0U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }

    Bytes issuer{};
    Bytes subject{};
    const Bytes encoded(context.encoded, context.encoded_size);
    if (!extract_certificate_names(encoded, issuer, subject)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::vector<NameAttribute> attributes;
    if (!parse_name((flags & kCertNameIssuerFlag) != 0U ? issuer : subject, attributes)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    bool found = false;
    bool selection_valid = true;
    const std::u16string selected =
        select_name(attributes, type, type_parameter, found, selection_valid);
    if (!selection_valid) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::size_t required = selected.size() + 1U;
    if (required > std::numeric_limits<std::uint32_t>::max()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    if (name_string == nullptr || name_string_capacity == 0U) {
        set_last_error(abi::kErrorSuccess);
        return static_cast<std::uint32_t>(required);
    }
    if (name_string_capacity < required) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    if (!write_guest_wstring(name_string, selected)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(required);
}

TL_CRYPT32_MSABI std::uint32_t tl_CertNameToStrW(
    const std::uint32_t encoding_type, const GuestDataBlob* const name,
    const std::uint32_t string_type, std::uint16_t* const string,
    const std::uint32_t string_capacity) noexcept {
    constexpr std::uint32_t kSupportedEncoding = kX509AsnEncoding;
    constexpr std::uint32_t kSupportedStringTypes = 1U | 2U | 3U;
    constexpr std::uint32_t kSemicolonFlag = 0x40000000U;
    constexpr std::uint32_t kCrlfFlag = 0x08000000U;
    constexpr std::uint32_t kNoPlusFlag = 0x20000000U;
    constexpr std::uint32_t kNoQuotingFlag = 0x10000000U;
    constexpr std::uint32_t kReverseFlag = 0x02000000U;
    constexpr std::uint32_t kSupportedFlags =
        kSemicolonFlag | kCrlfFlag | kNoPlusFlag | kNoQuotingFlag | kReverseFlag;

    if ((encoding_type & 0xFFFFU) != kSupportedEncoding || name == nullptr ||
        !runtime::validate_mapped_range(name, sizeof(*name), false) ||
        (string_type & 0xFFU) == 0U ||
        (string_type & 0xFFU) > kSupportedStringTypes ||
        (string_type & ~(0xFFU | kSupportedFlags)) != 0U || name->data == nullptr ||
        name->size == 0U || name->size > kMaxCertificateSize ||
        !runtime::validate_mapped_range(name->data, name->size, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (string != nullptr && string_capacity != 0U &&
        (static_cast<std::size_t>(string_capacity) >
             std::numeric_limits<std::size_t>::max() / sizeof(*string) ||
         !runtime::validate_mapped_range(
             string, static_cast<std::size_t>(string_capacity) * sizeof(*string), true))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }

    Bytes encoded_name{name->data, name->size};
    if (!encoded_name.empty() && encoded_name.front() == 0x30U) {
        std::size_t offset = 0;
        Bytes sequence{};
        if (!read_der_tag(encoded_name, offset, 0x30U, sequence) ||
            offset != encoded_name.size()) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        encoded_name = sequence;
    }
    std::vector<NameAttribute> attributes;
    if (!parse_name(encoded_name, attributes)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::u16string formatted = format_name(attributes, string_type);
    if (formatted.size() == std::numeric_limits<std::size_t>::max()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    const std::size_t required = formatted.size() + 1U;
    if (required > std::numeric_limits<std::uint32_t>::max()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    if (string == nullptr || string_capacity == 0U) {
        set_last_error(abi::kErrorSuccess);
        return static_cast<std::uint32_t>(required);
    }
    if (string_capacity < required) {
        string[0] = 0;
        set_last_error(abi::kErrorInsufficientBuffer);
        return static_cast<std::uint32_t>(required);
    }
    std::copy(formatted.begin(), formatted.end(), string);
    string[formatted.size()] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(required);
}

TL_CRYPT32_MSABI const GuestCertContext* tl_CertDuplicateCertificateContext(
    const GuestCertContext* const cert_context) noexcept {
    if (cert_context == nullptr) {
        return nullptr;
    }
    if (!runtime::validate_mapped_range(cert_context, sizeof(*cert_context), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(g_crypto_mutex);
    for (const auto& tracked : g_tracked_contexts) {
        if (&tracked->guest_context == cert_context) {
            tracked->refcount++;
            return &tracked->guest_context;
        }
    }

    if (cert_context->encoded == nullptr || cert_context->encoded_size == 0U ||
        cert_context->encoded_size > kMaxCertificateSize ||
        !runtime::validate_mapped_range(cert_context->encoded, cert_context->encoded_size, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }

    auto tracked = std::make_unique<TrackedContext>();
    tracked->encoded_storage.assign(cert_context->encoded,
                                   cert_context->encoded + cert_context->encoded_size);
    tracked->guest_context.encoding_type = cert_context->encoding_type;
    tracked->guest_context.encoded = tracked->encoded_storage.data();
    tracked->guest_context.encoded_size = cert_context->encoded_size;
    tracked->guest_context.cert_info = cert_context->cert_info;
    tracked->guest_context.cert_store = cert_context->cert_store;
    tracked->refcount = 1U;

    const GuestCertContext* result = &tracked->guest_context;
    g_tracked_contexts.push_back(std::move(tracked));
    return result;
}

TL_CRYPT32_MSABI std::uint32_t tl_CertFreeCertificateContext(
    const GuestCertContext* const cert_context) noexcept {
    if (cert_context == nullptr) {
        return 1U;
    }

    std::lock_guard<std::mutex> lock(g_crypto_mutex);
    for (auto it = g_tracked_contexts.begin(); it != g_tracked_contexts.end(); ++it) {
        if (&(*it)->guest_context == cert_context) {
            if (--(*it)->refcount == 0U) {
                g_tracked_contexts.erase(it);
            }
            return 1U;
        }
    }

    if (!runtime::validate_mapped_range(cert_context, sizeof(*cert_context), false)) {
        return 0U;
    }
    return 1U;
}

TL_CRYPT32_MSABI void* tl_CertOpenStore(
    const char* const store_provider, const std::uint32_t encoding_type,
    void* const crypt_prov, const std::uint32_t flags, const void* const para) noexcept {
    (void)crypt_prov;
    if (store_provider == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }

    const std::uintptr_t provider = reinterpret_cast<std::uintptr_t>(store_provider);
    if (provider != kCertStoreProvMemory && provider != kCertStoreProvSystemA &&
        provider != kCertStoreProvSystemW) {
        if (provider < 4096U || !runtime::validate_mapped_cstring(store_provider)) {
            set_last_error(abi::kErrorInvalidParameter);
            return nullptr;
        }
        set_last_error(abi::kErrorNotSupported);
        return nullptr;
    }
    if (provider == kCertStoreProvSystemA) {
        if (para == nullptr || !runtime::validate_mapped_cstring(static_cast<const char*>(para))) {
            set_last_error(abi::kErrorInvalidParameter);
            return nullptr;
        }
    } else if (provider == kCertStoreProvSystemW) {
        if (para == nullptr ||
            !runtime::validate_mapped_wstring(static_cast<const std::uint16_t*>(para))) {
            set_last_error(abi::kErrorInvalidParameter);
            return nullptr;
        }
    }

    std::lock_guard<std::mutex> lock(g_crypto_mutex);
    auto store = std::make_unique<TrackedStore>();
    store->encoding_type = encoding_type;
    store->flags = flags;
    void* const handle = store.get();
    g_tracked_stores.push_back(std::move(store));
    set_last_error(abi::kErrorSuccess);
    return handle;
}

TL_CRYPT32_MSABI std::uint32_t tl_CertCloseStore(
    void* const cert_store, const std::uint32_t flags) noexcept {
    (void)flags;
    if (cert_store == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0U;
    }

    std::lock_guard<std::mutex> lock(g_crypto_mutex);
    for (auto it = g_tracked_stores.begin(); it != g_tracked_stores.end(); ++it) {
        if (it->get() == cert_store) {
            for (const auto* const cert : (*it)->certs) {
                for (auto ctx_it = g_tracked_contexts.begin(); ctx_it != g_tracked_contexts.end(); ++ctx_it) {
                    if (&(*ctx_it)->guest_context == cert) {
                        if (--(*ctx_it)->refcount == 0U) {
                            g_tracked_contexts.erase(ctx_it);
                        }
                        break;
                    }
                }
            }
            g_tracked_stores.erase(it);
            set_last_error(abi::kErrorSuccess);
            return 1U;
        }
    }

    set_last_error(abi::kErrorInvalidHandle);
    return 0U;
}

TL_CRYPT32_MSABI const GuestCertContext* tl_CertEnumCertificatesInStore(
    void* const cert_store, const GuestCertContext* const prev_cert_context) noexcept {
    if (cert_store == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(g_crypto_mutex);
    TrackedStore* target_store = nullptr;
    for (const auto& store : g_tracked_stores) {
        if (store.get() == cert_store) {
            target_store = store.get();
            break;
        }
    }
    if (target_store == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }

    std::size_t next_index = 0;
    if (prev_cert_context != nullptr) {
        bool found = false;
        for (std::size_t i = 0; i < target_store->certs.size(); ++i) {
            if (target_store->certs[i] == prev_cert_context) {
                next_index = i + 1U;
                found = true;
                break;
            }
        }
        for (auto it = g_tracked_contexts.begin(); it != g_tracked_contexts.end(); ++it) {
            if (&(*it)->guest_context == prev_cert_context) {
                if (--(*it)->refcount == 0U) {
                    g_tracked_contexts.erase(it);
                }
                break;
            }
        }
        if (!found) {
            set_last_error(kCryptENotFound);
            return nullptr;
        }
    }

    if (next_index < target_store->certs.size()) {
        const auto* const cert = target_store->certs[next_index];
        for (const auto& tracked : g_tracked_contexts) {
            if (&tracked->guest_context == cert) {
                tracked->refcount++;
                set_last_error(abi::kErrorSuccess);
                return &tracked->guest_context;
            }
        }
    }

    set_last_error(kCryptENotFound);
    return nullptr;
}

TL_CRYPT32_MSABI const GuestCertContext* tl_CertFindCertificateInStore(
    void* const cert_store, const std::uint32_t encoding_type, const std::uint32_t find_flags,
    const std::uint32_t find_type, const void* const find_para,
    const GuestCertContext* const prev_cert_context) noexcept {
    (void)encoding_type;
    (void)find_flags;
    if (cert_store == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(g_crypto_mutex);
    TrackedStore* target_store = nullptr;
    for (const auto& store : g_tracked_stores) {
        if (store.get() == cert_store) {
            target_store = store.get();
            break;
        }
    }
    if (target_store == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }

    std::size_t start_index = 0;
    if (prev_cert_context != nullptr) {
        for (std::size_t i = 0; i < target_store->certs.size(); ++i) {
            if (target_store->certs[i] == prev_cert_context) {
                start_index = i + 1U;
                break;
            }
        }
        for (auto it = g_tracked_contexts.begin(); it != g_tracked_contexts.end(); ++it) {
            if (&(*it)->guest_context == prev_cert_context) {
                if (--(*it)->refcount == 0U) {
                    g_tracked_contexts.erase(it);
                }
                break;
            }
        }
    }

    for (std::size_t i = start_index; i < target_store->certs.size(); ++i) {
        const auto* const candidate = target_store->certs[i];
        bool matches = false;
        if (find_type == kCertFindAny) {
            matches = true;
        } else if (find_type == kCertFindSha1Hash && find_para != nullptr &&
                   runtime::validate_mapped_range(find_para, sizeof(GuestDataBlob), false)) {
            const auto* const blob = static_cast<const GuestDataBlob*>(find_para);
            if (blob->size == 20U && blob->data != nullptr &&
                runtime::validate_mapped_range(blob->data, 20U, false)) {
                const auto hash = compute_sha1(Bytes(candidate->encoded, candidate->encoded_size));
                matches = (std::memcmp(hash.data(), blob->data, 20U) == 0);
            }
        } else if (find_type == kCertFindSubjectStrW && find_para != nullptr &&
                   runtime::validate_mapped_wstring(static_cast<const std::uint16_t*>(find_para))) {
            Bytes issuer{};
            Bytes subject{};
            if (extract_certificate_names(Bytes(candidate->encoded, candidate->encoded_size),
                                          issuer, subject)) {
                std::vector<NameAttribute> attrs;
                if (parse_name(subject, attrs)) {
                    bool found = false;
                    const std::u16string name =
                        select_name(attrs, kCertNameSimpleDisplayType, nullptr, found);
                    if (found) {
                        const std::u16string target(static_cast<const char16_t*>(
                            static_cast<const void*>(find_para)));
                        matches = (name.find(target) != std::u16string::npos);
                    }
                }
            }
        }

        if (matches) {
            for (const auto& tracked : g_tracked_contexts) {
                if (&tracked->guest_context == candidate) {
                    tracked->refcount++;
                    set_last_error(abi::kErrorSuccess);
                    return &tracked->guest_context;
                }
            }
        }
    }

    set_last_error(kCryptENotFound);
    return nullptr;
}

TL_CRYPT32_MSABI std::uint32_t tl_CertGetCertificateContextProperty(
    const GuestCertContext* const cert_context, const std::uint32_t prop_id, void* const data,
    std::uint32_t* const data_size) noexcept {
    if (cert_context == nullptr || data_size == nullptr ||
        !runtime::validate_mapped_range(cert_context, sizeof(*cert_context), false) ||
        !runtime::validate_mapped_range(data_size, sizeof(*data_size), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0U;
    }
    if (cert_context->encoded == nullptr || cert_context->encoded_size == 0U ||
        cert_context->encoded_size > kMaxCertificateSize ||
        !runtime::validate_mapped_range(cert_context->encoded, cert_context->encoded_size, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0U;
    }

    if (prop_id == kCertSha1HashPropId) {
        const auto hash = compute_sha1(Bytes(cert_context->encoded, cert_context->encoded_size));
        if (data == nullptr) {
            *data_size = 20U;
            set_last_error(abi::kErrorSuccess);
            return 1U;
        }
        if (*data_size < 20U) {
            *data_size = 20U;
            set_last_error(kErrorMoreData);
            return 0U;
        }
        if (!runtime::validate_mapped_range(data, 20U, true)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0U;
        }
        std::copy(hash.begin(), hash.end(), static_cast<std::uint8_t*>(data));
        *data_size = 20U;
        set_last_error(abi::kErrorSuccess);
        return 1U;
    }

    if (prop_id == kCertFriendlyNamePropId) {
        Bytes issuer{};
        Bytes subject{};
        if (!extract_certificate_names(Bytes(cert_context->encoded, cert_context->encoded_size),
                                      issuer, subject)) {
            set_last_error(kCryptENotFound);
            return 0U;
        }
        std::vector<NameAttribute> attributes;
        if (!parse_name(subject, attributes)) {
            set_last_error(kCryptENotFound);
            return 0U;
        }
        bool found = false;
        const std::u16string selected = select_name(attributes, kCertNameSimpleDisplayType, nullptr, found);
        if (!found) {
            set_last_error(kCryptENotFound);
            return 0U;
        }
        const std::size_t required = (selected.size() + 1U) * sizeof(std::uint16_t);
        if (data == nullptr) {
            *data_size = static_cast<std::uint32_t>(required);
            set_last_error(abi::kErrorSuccess);
            return 1U;
        }
        if (static_cast<std::size_t>(*data_size) < required) {
            *data_size = static_cast<std::uint32_t>(required);
            set_last_error(kErrorMoreData);
            return 0U;
        }
        if (!runtime::validate_mapped_range(data, required, true)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0U;
        }
        std::copy(selected.begin(), selected.end(), static_cast<std::uint16_t*>(data));
        static_cast<std::uint16_t*>(data)[selected.size()] = 0;
        *data_size = static_cast<std::uint32_t>(required);
        set_last_error(abi::kErrorSuccess);
        return 1U;
    }

    set_last_error(kCryptENotFound);
    return 0U;
}

TL_CRYPT32_MSABI void* tl_CertOpenSystemStoreA(
    void* const crypt_prov, const char* const system_store_name) noexcept {
    return tl_CertOpenStore(reinterpret_cast<const char*>(kCertStoreProvSystemA), 0U, crypt_prov,
                            0U, system_store_name);
}

TL_CRYPT32_MSABI void* tl_CertOpenSystemStoreW(
    void* const crypt_prov, const std::uint16_t* const system_store_name) noexcept {
    return tl_CertOpenStore(reinterpret_cast<const char*>(kCertStoreProvSystemW), 0U, crypt_prov,
                            0U, system_store_name);
}

TL_CRYPT32_MSABI void* tl_CertGetEnhancedKeyUsage(void* const cert_context, const std::uint32_t flags,
                                                  void* const usage, std::uint32_t* const usage_size) noexcept {
    (void)cert_context;
    (void)flags;
    if (usage_size == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    constexpr std::uint32_t req_size = 32;
    if (usage == nullptr || *usage_size < req_size) {
        *usage_size = req_size;
        set_last_error(abi::kErrorSuccess);
        return nullptr;
    }
    std::memset(usage, 0, req_size);
    *usage_size = req_size;
    set_last_error(abi::kErrorSuccess);
    return usage;
}

TL_CRYPT32_MSABI int tl_CertGetIntendedKeyUsage(const std::uint32_t cert_encoding_type, void* const cert_info,
                                                std::uint8_t* const key_usage, const std::uint32_t byte_count) noexcept {
    (void)cert_encoding_type;
    (void)cert_info;
    if (key_usage != nullptr && byte_count > 0) {
        std::memset(key_usage, 0xFF, byte_count);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_CRYPT32_MSABI int tl_CryptMsgClose(void* const hCryptMsg) noexcept {
    if (hCryptMsg == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    set_last_error(abi::kErrorInvalidHandle);
    return 0;
}

TL_CRYPT32_MSABI int tl_CryptMsgGetParam(void* const hCryptMsg, const std::uint32_t dwParamType, const std::uint32_t dwIndex, void* const pvData, std::uint32_t* const pcbData) noexcept {
    (void)hCryptMsg;
    (void)dwParamType;
    (void)dwIndex;
    (void)pvData;
    if (pcbData != nullptr) {
        *pcbData = 0;
    }
    if (hCryptMsg == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (pcbData == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorInvalidHandle);
    return 0;
}

TL_CRYPT32_MSABI int tl_CryptQueryObject(const std::uint32_t dwObjectType, const void* const pvObject, const std::uint32_t dwExpectedContentTypeFlags, const std::uint32_t dwExpectedFormatTypeFlags, const std::uint32_t dwFlags, std::uint32_t* const pdwMsgAndCertEncodingType, std::uint32_t* const pdwContentType, std::uint32_t* const pdwFormatType, void** const phCertStore, void** const phMsg, const void** const ppvContext) noexcept {
    (void)dwObjectType;
    (void)pvObject;
    (void)dwExpectedContentTypeFlags;
    (void)dwExpectedFormatTypeFlags;
    (void)dwFlags;
    const auto valid_output = [](const void* const pointer, const std::size_t size,
                                 const bool writable) noexcept {
        return pointer == nullptr || runtime::validate_mapped_range(pointer, size, writable);
    };
    if (!valid_output(pdwMsgAndCertEncodingType, sizeof(*pdwMsgAndCertEncodingType), true) ||
        !valid_output(pdwContentType, sizeof(*pdwContentType), true) ||
        !valid_output(pdwFormatType, sizeof(*pdwFormatType), true) ||
        !valid_output(phCertStore, sizeof(*phCertStore), true) ||
        !valid_output(phMsg, sizeof(*phMsg), true) ||
        !valid_output(ppvContext, sizeof(*ppvContext), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (pdwMsgAndCertEncodingType != nullptr) *pdwMsgAndCertEncodingType = 0;
    if (pdwContentType != nullptr) *pdwContentType = 0;
    if (pdwFormatType != nullptr) *pdwFormatType = 0;
    if (phCertStore != nullptr) *phCertStore = nullptr;
    if (phMsg != nullptr) *phMsg = nullptr;
    if (ppvContext != nullptr) *ppvContext = nullptr;
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

}  // extern "C"
}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_crypt32_module() {
    static const ExportedFunction kCrypt32Exports[] = {
        {"CertGetNameStringW", 1, reinterpret_cast<std::uintptr_t>(&tl_CertGetNameStringW), ExportSupport::Full},
        {"CertDuplicateCertificateContext", 2,
         reinterpret_cast<std::uintptr_t>(&tl_CertDuplicateCertificateContext), ExportSupport::Full},
        {"CertFreeCertificateContext", 3,
         reinterpret_cast<std::uintptr_t>(&tl_CertFreeCertificateContext), ExportSupport::Full},
        {"CertOpenStore", 4, reinterpret_cast<std::uintptr_t>(&tl_CertOpenStore), ExportSupport::Full},
        {"CertCloseStore", 5, reinterpret_cast<std::uintptr_t>(&tl_CertCloseStore), ExportSupport::Full},
        {"CertEnumCertificatesInStore", 6,
         reinterpret_cast<std::uintptr_t>(&tl_CertEnumCertificatesInStore), ExportSupport::Full},
        {"CertFindCertificateInStore", 7,
         reinterpret_cast<std::uintptr_t>(&tl_CertFindCertificateInStore), ExportSupport::Full},
        {"CertGetCertificateContextProperty", 8,
         reinterpret_cast<std::uintptr_t>(&tl_CertGetCertificateContextProperty), ExportSupport::Full},
        {"CertOpenSystemStoreA", 9, reinterpret_cast<std::uintptr_t>(&tl_CertOpenSystemStoreA), ExportSupport::Full},
        {"CertOpenSystemStoreW", 10, reinterpret_cast<std::uintptr_t>(&tl_CertOpenSystemStoreW), ExportSupport::Full},
        {"CertGetEnhancedKeyUsage", 11, reinterpret_cast<std::uintptr_t>(&tl_CertGetEnhancedKeyUsage), ExportSupport::Full},
        {"CertGetIntendedKeyUsage", 12, reinterpret_cast<std::uintptr_t>(&tl_CertGetIntendedKeyUsage), ExportSupport::Full},
        {"CryptMsgClose", 13, reinterpret_cast<std::uintptr_t>(&tl_CryptMsgClose), ExportSupport::Full},
        {"CryptMsgGetParam", 14, reinterpret_cast<std::uintptr_t>(&tl_CryptMsgGetParam), ExportSupport::Full},
        {"CryptQueryObject", 15, reinterpret_cast<std::uintptr_t>(&tl_CryptQueryObject), ExportSupport::Full},
        {"CertNameToStrW", 16, reinterpret_cast<std::uintptr_t>(&tl_CertNameToStrW), ExportSupport::Full},
    };
    static const InternalModule kCrypt32Module{"CRYPT32.dll", kCrypt32Exports};
    register_module(kCrypt32Module);
}

}  // namespace tradutorlinux::loader
