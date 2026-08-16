#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

#include <unistd.h>

namespace tradutorlinux::util {

// Formata um inteiro sem sinal como "0x" + dígitos hexadecimais em minúsculas.
[[nodiscard]] inline std::string format_hex(const std::uint64_t value) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string result = "0x";
    bool started = false;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const unsigned int digit = static_cast<unsigned int>((value >> shift) & 0xFULL);
        if (digit != 0 || started) {
            result.push_back(kDigits[digit]);
            started = true;
        }
    }
    if (!started) {
        result.push_back('0');
    }
    return result;
}

// Formata um inteiro com sinal em hexadecimal, preservando o sinal (ex.: delta
// de relocação negativo aparece como "-0x1000" e não como 0xfffffffffffff000).
[[nodiscard]] inline std::string format_signed_hex(const std::int64_t value) {
    if (value < 0) {
        const std::uint64_t magnitude =
            value == INT64_MIN ? 0x8000000000000000ULL
                               : static_cast<std::uint64_t>(-value);
        return "-" + format_hex(magnitude);
    }
    return format_hex(static_cast<std::uint64_t>(value));
}

// Comparação ASCII case-insensitive, independente de locale.
[[nodiscard]] inline bool ascii_iequals(const std::string_view a, const std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t index = 0; index < a.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(a[index])) !=
            std::tolower(static_cast<unsigned char>(b[index]))) {
            return false;
        }
    }
    return true;
}

// Tamanho da página do host, com fallback de 4 KiB quando sysconf falha.
[[nodiscard]] inline std::size_t host_page_size() noexcept {
    const long value = ::sysconf(_SC_PAGESIZE);
    return value > 0 ? static_cast<std::size_t>(value) : 0x1000;
}

}  // namespace tradutorlinux::util