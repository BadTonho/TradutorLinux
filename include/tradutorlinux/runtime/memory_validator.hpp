#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace tradutorlinux::runtime {

enum class GuestMemoryAccessStatus : std::uint8_t {
    Success,
    InvalidArgument,
    Unmapped,
    PermissionDenied,
    Partial,
    SystemError,
};

struct GuestMemoryAccessResult {
    GuestMemoryAccessStatus status{GuestMemoryAccessStatus::InvalidArgument};
    std::size_t transferred{0};
};

// Copia memória usando a interface do kernel para o próprio processo. Ao
// contrário de uma leitura após um snapshot de /proc/self/maps, a operação
// não desreferencia o endereço convidado no código do host e reporta cópia
// parcial quando uma página desaparece durante a operação.
[[nodiscard]] GuestMemoryAccessResult read_guest_memory(const void* source,
                                                        void* destination,
                                                        std::size_t size) noexcept;
[[nodiscard]] GuestMemoryAccessResult write_guest_memory(void* destination,
                                                         const void* source,
                                                         std::size_t size) noexcept;

// Copia strings terminadas em NUL para memória do host sem expor o ponteiro
// convidado às rotinas de conversão de texto.
[[nodiscard]] bool copy_guest_cstring(const char* source, std::size_t max_len,
                                      std::string& destination) noexcept;
[[nodiscard]] bool copy_guest_wstring(const std::uint16_t* source, std::size_t max_len,
                                      std::u16string& destination) noexcept;

// Valida se uma faixa de memória [address, address + size) está mapeada e acessível no espaço de endereço atual.
[[nodiscard]] bool validate_mapped_range(const void* address, std::size_t size, bool writable) noexcept;

// Valida se uma string C (terminada em nulo) está inteiramente contida em memória legível válida.
[[nodiscard]] bool validate_mapped_cstring(const char* str, std::size_t max_len = 65535) noexcept;

// Valida se uma string UTF-16 (terminada em nulo duplo) está inteiramente contida em memória legível válida.
[[nodiscard]] bool validate_mapped_wstring(const std::uint16_t* wstr, std::size_t max_len = 65535) noexcept;

// Invalida a cache de regiões de memória mapeadas (deve ser chamado após VirtualAlloc, VirtualFree ou mprotect).
void invalidate_memory_map_cache() noexcept;

}  // namespace tradutorlinux::runtime
