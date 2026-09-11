#include "tradutorlinux/pe/pe_analyzer.hpp"
#include "tradutorlinux/pe/pe_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace tradutorlinux::pe {

MitigationInfo inspect_pe_mitigations(
    const std::span<const std::byte> file_bytes,
    [[maybe_unused]] const PeInfo& info) {
    MitigationInfo result{};
    if (file_bytes.size() < 0x40) {
        return result;
    }

    // Validação do cabeçalho DOS ('MZ')
    if (std::to_integer<unsigned char>(file_bytes[0]) != 'M' ||
        std::to_integer<unsigned char>(file_bytes[1]) != 'Z') {
        return result;
    }

    // Offset do PE em e_lfanew (0x3C, 4 bytes, little-endian)
    std::uint32_t pe_offset = 0;
    for (int i = 0; i < 4; ++i) {
        pe_offset |= static_cast<std::uint32_t>(
            std::to_integer<unsigned char>(file_bytes[0x3C + static_cast<std::size_t>(i)])) << (i * 8);
    }

    // OptionalHeader64 DllCharacteristics fica no offset 70 dentro do Optional Header.
    // Estrutura: PE_SIGNATURE (4 bytes) + COFF_HEADER (20 bytes) + OPTIONAL_HEADER.
    constexpr std::size_t kSignatureSize = 4;
    constexpr std::size_t kCoffHeaderSize = 20;
    constexpr std::size_t kDllCharOptOffset = 70;
    const std::size_t dll_char_file_offset =
        static_cast<std::size_t>(pe_offset) + kSignatureSize + kCoffHeaderSize + kDllCharOptOffset;

    if (file_bytes.size() < dll_char_file_offset + 2) {
        return result;
    }

    // Validação da assinatura PE ('PE\0\0')
    if (std::to_integer<unsigned char>(file_bytes[pe_offset]) != 'P' ||
        std::to_integer<unsigned char>(file_bytes[pe_offset + 1]) != 'E' ||
        std::to_integer<unsigned char>(file_bytes[pe_offset + 2]) != 0 ||
        std::to_integer<unsigned char>(file_bytes[pe_offset + 3]) != 0) {
        return result;
    }

    // Leitura segura do campo DllCharacteristics (uint16_t little-endian)
    const std::uint16_t chars = static_cast<std::uint16_t>(
        std::to_integer<unsigned char>(file_bytes[dll_char_file_offset]) |
        (static_cast<std::uint16_t>(std::to_integer<unsigned char>(file_bytes[dll_char_file_offset + 1])) << 8));

    result.has_mitigations = true;
    result.raw_characteristics = chars;
    result.aslr = (chars & kDllCharDynamicBase) != 0;
    result.high_entropy_va = (chars & kDllCharHighEntropyVa) != 0;
    result.dep = (chars & kDllCharNxCompat) != 0;
    result.cfg = (chars & kDllCharGuardCf) != 0;
    result.no_seh = (chars & kDllCharNoSeh) != 0;
    result.app_container = (chars & kDllCharAppContainer) != 0;
    result.force_integrity = (chars & kDllCharForceIntegrity) != 0;

    return result;
}

}  // namespace tradutorlinux::pe

