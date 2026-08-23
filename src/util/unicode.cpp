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

constexpr std::array<std::uint32_t, 128> kCp437High{
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
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

std::uint32_t cp437_to_unicode(const std::uint8_t byte) noexcept {
    return byte < 0x80U ? static_cast<std::uint32_t>(byte)
                        : kCp437High[static_cast<std::size_t>(byte - 0x80U)];
}

bool unicode_to_cp437(const std::uint32_t codepoint, std::uint8_t& byte) noexcept {
    if (codepoint < 0x80U) {
        byte = static_cast<std::uint8_t>(codepoint);
        return true;
    }
    const auto found = std::find(kCp437High.begin(), kCp437High.end(), codepoint);
    if (found == kCp437High.end()) {
        return false;
    }
    byte = static_cast<std::uint8_t>(0x80U + static_cast<std::size_t>(found - kCp437High.begin()));
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
