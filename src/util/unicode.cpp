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

constexpr std::array<std::uint32_t, 128> kCp1250High{
    0x20AC, 0x0081, 0x201A, 0x0083, 0x201E, 0x2026, 0x2020, 0x2021,
    0x0088, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x0098, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
    0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7,
    0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
    0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
    0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
    0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
    0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
    0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
    0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
    0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
    0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
    0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
    0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9
};

constexpr std::array<std::uint32_t, 128> kCp1251High{
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x0098, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
    0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
    0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
    0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F
};

struct CpEntry {
    std::uint16_t codepoint{};
    std::uint8_t byte{};
};

template <std::size_t N>
constexpr auto make_cp_reverse(const std::array<std::uint32_t, N>& high_table) {
    std::array<CpEntry, N> table{};
    for (std::size_t i = 0; i < N; ++i) {
        table[i] = {static_cast<std::uint16_t>(high_table[i]), static_cast<std::uint8_t>(0x80U + i)};
    }
    std::sort(table.begin(), table.end(), [](const CpEntry& a, const CpEntry& b) {
        return a.codepoint < b.codepoint;
    });
    return table;
}

constexpr auto kCp437Reverse = make_cp_reverse(kCp437High);
constexpr auto kCp1250Reverse = make_cp_reverse(kCp1250High);
constexpr auto kCp1251Reverse = make_cp_reverse(kCp1251High);

}  // namespace

std::uint32_t cp1252_to_unicode(const std::uint8_t byte) noexcept {
    if (byte >= 0x80U && byte <= 0x9FU) {
        return kCp1252Control[static_cast<std::size_t>(byte - 0x80U)];
    }
    return static_cast<std::uint32_t>(byte);
}

bool unicode_to_cp1252(const std::uint32_t codepoint, std::uint8_t& byte) noexcept {
    if (codepoint <= 0x7FU || (codepoint >= 0xA0U && codepoint <= 0xFFU)) {
        byte = static_cast<std::uint8_t>(codepoint);
        return true;
    }
    switch (codepoint) {
        case 0x20AC: byte = 0x80; return true;
        case 0x0081: byte = 0x81; return true;
        case 0x201A: byte = 0x82; return true;
        case 0x0192: byte = 0x83; return true;
        case 0x201E: byte = 0x84; return true;
        case 0x2026: byte = 0x85; return true;
        case 0x2020: byte = 0x86; return true;
        case 0x2021: byte = 0x87; return true;
        case 0x02C6: byte = 0x88; return true;
        case 0x2030: byte = 0x89; return true;
        case 0x0160: byte = 0x8A; return true;
        case 0x2039: byte = 0x8B; return true;
        case 0x0152: byte = 0x8C; return true;
        case 0x008D: byte = 0x8D; return true;
        case 0x017D: byte = 0x8E; return true;
        case 0x008F: byte = 0x8F; return true;
        case 0x0090: byte = 0x90; return true;
        case 0x2018: byte = 0x91; return true;
        case 0x2019: byte = 0x92; return true;
        case 0x201C: byte = 0x93; return true;
        case 0x201D: byte = 0x94; return true;
        case 0x2022: byte = 0x95; return true;
        case 0x2013: byte = 0x96; return true;
        case 0x2014: byte = 0x97; return true;
        case 0x02DC: byte = 0x98; return true;
        case 0x2122: byte = 0x99; return true;
        case 0x0161: byte = 0x9A; return true;
        case 0x203A: byte = 0x9B; return true;
        case 0x0153: byte = 0x9C; return true;
        case 0x009D: byte = 0x9D; return true;
        case 0x017E: byte = 0x9E; return true;
        case 0x0178: byte = 0x9F; return true;
        default: break;
    }
    return false;
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
    if (codepoint > 0xFFFFU) {
        return false;
    }
    const auto cp16 = static_cast<std::uint16_t>(codepoint);
    auto it = std::lower_bound(kCp437Reverse.begin(), kCp437Reverse.end(), cp16,
                               [](const CpEntry& entry, const std::uint16_t val) noexcept {
                                   return entry.codepoint < val;
                               });
    if (it != kCp437Reverse.end() && it->codepoint == cp16) {
        byte = it->byte;
        return true;
    }
    return false;
}

std::uint32_t cp1250_to_unicode(const std::uint8_t byte) noexcept {
    return byte < 0x80U ? static_cast<std::uint32_t>(byte)
                        : kCp1250High[static_cast<std::size_t>(byte - 0x80U)];
}

bool unicode_to_cp1250(const std::uint32_t codepoint, std::uint8_t& byte) noexcept {
    if (codepoint < 0x80U) {
        byte = static_cast<std::uint8_t>(codepoint);
        return true;
    }
    if (codepoint > 0xFFFFU) {
        return false;
    }
    const auto cp16 = static_cast<std::uint16_t>(codepoint);
    auto it = std::lower_bound(kCp1250Reverse.begin(), kCp1250Reverse.end(), cp16,
                               [](const CpEntry& entry, const std::uint16_t val) noexcept {
                                   return entry.codepoint < val;
                               });
    if (it != kCp1250Reverse.end() && it->codepoint == cp16) {
        byte = it->byte;
        return true;
    }
    return false;
}

std::uint32_t cp1251_to_unicode(const std::uint8_t byte) noexcept {
    return byte < 0x80U ? static_cast<std::uint32_t>(byte)
                        : kCp1251High[static_cast<std::size_t>(byte - 0x80U)];
}

bool unicode_to_cp1251(const std::uint32_t codepoint, std::uint8_t& byte) noexcept {
    if (codepoint < 0x80U) {
        byte = static_cast<std::uint8_t>(codepoint);
        return true;
    }
    if (codepoint > 0xFFFFU) {
        return false;
    }
    const auto cp16 = static_cast<std::uint16_t>(codepoint);
    auto it = std::lower_bound(kCp1251Reverse.begin(), kCp1251Reverse.end(), cp16,
                               [](const CpEntry& entry, const std::uint16_t val) noexcept {
                                   return entry.codepoint < val;
                               });
    if (it != kCp1251Reverse.end() && it->codepoint == cp16) {
        byte = it->byte;
        return true;
    }
    return false;
}

std::uint32_t cp28591_to_unicode(const std::uint8_t byte) noexcept {
    return static_cast<std::uint32_t>(byte);
}

bool unicode_to_cp28591(const std::uint32_t codepoint, std::uint8_t& byte) noexcept {
    if (codepoint <= 0xFFU) {
        byte = static_cast<std::uint8_t>(codepoint);
        return true;
    }
    return false;
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
