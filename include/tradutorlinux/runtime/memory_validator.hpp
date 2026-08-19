#pragma once

#include <cstddef>
#include <cstdint>

namespace tradutorlinux::runtime {

// Valida se uma faixa de memória [address, address + size) está mapeada e acessível no espaço de endereço atual.
[[nodiscard]] bool validate_mapped_range(const void* address, std::size_t size, bool writable) noexcept;

// Valida se uma string C (terminada em nulo) está inteiramente contida em memória legível válida.
[[nodiscard]] bool validate_mapped_cstring(const char* str, std::size_t max_len = 65535) noexcept;

// Valida se uma string UTF-16 (terminada em nulo duplo) está inteiramente contida em memória legível válida.
[[nodiscard]] bool validate_mapped_wstring(const std::uint16_t* wstr, std::size_t max_len = 65535) noexcept;

// Invalida a cache de regiões de memória mapeadas (deve ser chamado após VirtualAlloc, VirtualFree ou mprotect).
void invalidate_memory_map_cache() noexcept;

}  // namespace tradutorlinux::runtime
