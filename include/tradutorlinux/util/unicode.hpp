#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace tradutorlinux::util {

// Constante sentinela para codepoints inválidos
constexpr std::uint32_t kInvalidCodepoint = 0x110000U;

// Conversão entre CP1252 (Windows ANSI padrão) e Unicode
[[nodiscard]] std::uint32_t cp1252_to_unicode(std::uint8_t byte) noexcept;
[[nodiscard]] bool unicode_to_cp1252(std::uint32_t codepoint, std::uint8_t& byte) noexcept;
[[nodiscard]] std::uint32_t cp437_to_unicode(std::uint8_t byte) noexcept;
[[nodiscard]] bool unicode_to_cp437(std::uint32_t codepoint, std::uint8_t& byte) noexcept;

// Decodificação e contagem de unidades UTF-8
[[nodiscard]] std::uint32_t decode_utf8(const char* bytes, std::size_t length, std::size_t& pos) noexcept;
[[nodiscard]] std::size_t utf8_bytes_for(std::uint32_t codepoint, char out[4]) noexcept;

// Decodificação e contagem de unidades UTF-16
[[nodiscard]] std::uint32_t decode_utf16(const std::uint16_t* units, std::size_t length, std::size_t& pos) noexcept;
[[nodiscard]] std::size_t utf16_units_for(std::uint32_t codepoint, std::uint16_t out[2]) noexcept;

// Utilitários de conversão direta de strings
[[nodiscard]] std::string wide_to_utf8(const std::uint16_t* wide_str, std::size_t max_length = 4096) noexcept;
[[nodiscard]] std::u16string utf8_to_wide(std::string_view utf8_str) noexcept;

}  // namespace tradutorlinux::util
