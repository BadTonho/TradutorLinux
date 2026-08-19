#include "tradutorlinux/util/unicode.hpp"

#include <algorithm>
#include <array>

namespace tradutorlinux::util {
namespace {

constexpr std::array<std::uint32_t, 32> kCp1252Control{
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

}  // namespace

std::uint32_t cp1252_to_unicode(const std::uint8_t byte) noexcept {
    if (byte >= 0x80U && byte <= 0x9FU) {
        return kCp1252Control[static_cast<std::size_t>(byte - 0x80U)];
    }
    return static_cast<std::uint32_t>(byte);
}

bool unicode_to_cp1252(const std::uint32_t codepoint, std::uint8_t& byte) noexcept {
    if (codepoint <= 0xFFU) {
        byte = static_cast<std::uint8_t>(codepoint);
        return true;
    }
    const auto found = std::find(kCp1252Control.begin(), kCp1252Control.end(), codepoint);
    if (found == kCp1252Control.end()) {
        return false;
    }
    byte = static_cast<std::uint8_t>(0x80U + static_cast<std::size_t>(found - kCp1252Control.begin()));
    return true;
}

std::uint32_t decode_utf8(const char* const bytes, const std::size_t length,
                          std::size_t& pos) noexcept {
    if (pos >= length) {
        return kInvalidCodepoint;
    }
    const auto lead = static_cast<std::uint8_t>(bytes[pos]);
    if (lead < 0x80U) {
        pos += 1;
        return static_cast<std::uint32_t>(lead);
    }
    std::size_t needed = 0;
    std::uint32_t value = 0;
    if ((lead & 0xE0U) == 0xC0U) {
        needed = 2;
        value = lead & 0x1FU;
    } else if ((lead & 0xF0U) == 0xE0U) {
        needed = 3;
        value = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
        needed = 4;
        value = lead & 0x07U;
    } else {
        pos += 1;
        return kInvalidCodepoint;
    }
    if (pos + needed > length) {
        pos = length;
        return kInvalidCodepoint;
    }
    for (std::size_t i = 1; i < needed; ++i) {
        const auto continuation = static_cast<std::uint8_t>(bytes[pos + i]);
        if ((continuation & 0xC0U) != 0x80U) {
            pos += 1;
            return kInvalidCodepoint;
        }
        value = (value << 6U) | static_cast<std::uint32_t>(continuation & 0x3FU);
    }
    pos += needed;
    const bool overlong = (needed == 2 && value < 0x80U) ||
                          (needed == 3 && value < 0x800U) ||
                          (needed == 4 && value < 0x10000U);
    if (overlong || value > 0x10FFFFU || (value >= 0xD800U && value <= 0xDFFFU)) {
        return kInvalidCodepoint;
    }
    return value;
}

std::size_t utf8_bytes_for(const std::uint32_t codepoint, char out[4]) noexcept {
    if (codepoint < 0x80U) {
        out[0] = static_cast<char>(codepoint);
        return 1;
    }
    if (codepoint < 0x800U) {
        out[0] = static_cast<char>(0xC0U | (codepoint >> 6U));
        out[1] = static_cast<char>(0x80U | (codepoint & 0x3FU));
        return 2;
    }
    if (codepoint < 0x10000U) {
        out[0] = static_cast<char>(0xE0U | (codepoint >> 12U));
        out[1] = static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
        out[2] = static_cast<char>(0x80U | (codepoint & 0x3FU));
        return 3;
    }
    out[0] = static_cast<char>(0xF0U | (codepoint >> 18U));
    out[1] = static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU));
    out[2] = static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
    out[3] = static_cast<char>(0x80U | (codepoint & 0x3FU));
    return 4;
}

std::uint32_t decode_utf16(const std::uint16_t* const units, const std::size_t length,
                           std::size_t& pos) noexcept {
    const std::uint16_t first = units[pos];
    if (first >= 0xD800U && first <= 0xDBFFU) {
        if (pos + 1 < length && units[pos + 1] >= 0xDC00U && units[pos + 1] <= 0xDFFFU) {
            const std::uint32_t value =
                (static_cast<std::uint32_t>(first - 0xD800U) << 10U) |
                static_cast<std::uint32_t>(units[pos + 1] - 0xDC00U);
            pos += 2;
            return value + 0x10000U;
        }
        pos += 1;
        return kInvalidCodepoint;
    }
    if (first >= 0xDC00U && first <= 0xDFFFU) {
        pos += 1;
        return kInvalidCodepoint;
    }
    pos += 1;
    return static_cast<std::uint32_t>(first);
}

std::size_t utf16_units_for(const std::uint32_t codepoint, std::uint16_t out[2]) noexcept {
    if (codepoint < 0x10000U) {
        out[0] = static_cast<std::uint16_t>(codepoint);
        return 1;
    }
    const std::uint32_t value = codepoint - 0x10000U;
    out[0] = static_cast<std::uint16_t>(0xD800U | (value >> 10U));
    out[1] = static_cast<std::uint16_t>(0xDC00U | (value & 0x3FFU));
    return 2;
}

std::string wide_to_utf8(const std::uint16_t* wide_str, std::size_t max_length) noexcept {
    if (wide_str == nullptr || wide_str[0] == 0) {
        return {};
    }
    std::size_t length = 0;
    while (length < max_length && wide_str[length] != 0) {
        ++length;
    }
    std::string result;
    result.reserve(length * 3 / 2);
    std::size_t pos = 0;
    while (pos < length) {
        const std::uint32_t cp = decode_utf16(wide_str, length, pos);
        if (cp == kInvalidCodepoint) {
            result.push_back('?');
            continue;
        }
        char buf[4]{};
        const std::size_t written = utf8_bytes_for(cp, buf);
        result.append(buf, written);
    }
    return result;
}

std::u16string utf8_to_wide(std::string_view utf8_str) noexcept {
    std::u16string result;
    result.reserve(utf8_str.size());
    std::size_t pos = 0;
    while (pos < utf8_str.size()) {
        const std::uint32_t cp = decode_utf8(utf8_str.data(), utf8_str.size(), pos);
        if (cp == kInvalidCodepoint) {
            result.push_back(u'?');
            continue;
        }
        std::uint16_t buf[2]{};
        const std::size_t written = utf16_units_for(cp, buf);
        for (std::size_t i = 0; i < written; ++i) {
            result.push_back(buf[i]);
        }
    }
    return result;
}

}  // namespace tradutorlinux::util
