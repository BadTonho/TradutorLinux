#include "tradutorlinux/runtime/crypt32.hpp"

#include "runtime_context.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
                           bool& found) noexcept {
    found = false;
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
        if (type_parameter == nullptr || !runtime::validate_mapped_cstring(
                                             static_cast<const char*>(type_parameter), 128U)) {
            return {};
        }
        const std::string oid(static_cast<const char*>(type_parameter));
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

}  // namespace

extern "C" {

TL_CRYPT32_MSABI std::uint32_t tl_CertGetNameStringW(
    const GuestCertContext* const cert_context, const std::uint32_t type,
    const std::uint32_t flags, const void* const type_parameter,
    std::uint16_t* const name_string, const std::uint32_t name_string_capacity) noexcept {
    constexpr std::uint32_t kSupportedFlags = kCertNameIssuerFlag;
    if (cert_context == nullptr ||
        !runtime::validate_mapped_range(cert_context, sizeof(*cert_context), false) ||
        (type < kCertNameEmailType || type > kCertNameDnsType || type == kCertNameRdnType) ||
        (type == kCertNameAttrType && type_parameter == nullptr) ||
        (flags & ~kSupportedFlags) != 0U ||
        (cert_context->encoding_type != kX509AsnEncoding &&
         cert_context->encoding_type != (kX509AsnEncoding | kPkcs7AsnEncoding)) ||
        cert_context->encoded == nullptr || cert_context->encoded_size == 0U ||
        cert_context->encoded_size > kMaxCertificateSize ||
        !runtime::validate_mapped_range(cert_context->encoded, cert_context->encoded_size, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (name_string != nullptr && name_string_capacity != 0U &&
        (static_cast<std::size_t>(name_string_capacity) >
             std::numeric_limits<std::size_t>::max() / sizeof(*name_string) ||
         !runtime::validate_mapped_range(
             name_string, static_cast<std::size_t>(name_string_capacity) * sizeof(*name_string),
             true))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }

    Bytes issuer{};
    Bytes subject{};
    const Bytes encoded(cert_context->encoded, cert_context->encoded_size);
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
    const std::u16string selected = select_name(attributes, type, type_parameter, found);
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
    std::copy(selected.begin(), selected.end(), name_string);
    name_string[selected.size()] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(required);
}

}  // extern "C"

}  // namespace tradutorlinux
