#pragma once

#include "tradutorlinux/loader/image_mapper.hpp"
#include "tradutorlinux/loader/import_resolver.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace tradutorlinux::diagnostics {

// Contexto PE derivado do endereço de falta reportado pelo kernel (si_addr).
// A conversão é pura e determinística: endereço virtual do convidado → RVA →
// seção que contém o RVA → slot da IAT mais próximo abaixo dele.
struct GuestCrashContext {
    // Verdadeiro somente quando o endereço cai dentro da imagem mapeada;
    // faltas fora dela (ex.: desreferência de nulo) não têm RVA/section.
    bool valid{};
    std::uint64_t rva{};
    // Nome da região/seção que contém o RVA; vazio quando o RVA cai em um
    // intervalo entre regiões mapeadas.
    std::string_view section;
    // "DLL!símbolo" da importação resolvida cujo slot da IAT está mais próximo
    // abaixo (≤) do RVA da falta; vazio quando não há candidata.
    std::string nearest_import;
};

[[nodiscard]] GuestCrashContext describe_guest_crash(const loader::MappedImage& image,
                                                     const loader::ResolveResult& imports,
                                                     std::uint64_t fault_address);

// Verifica se o endereço de falta recai na guard page da pilha do processo.
[[nodiscard]] constexpr bool is_stack_overflow_fault(const std::uint64_t fault_address,
                                                     const void* const stack_base,
                                                     const std::size_t guard_size) noexcept {
    if (stack_base == nullptr || guard_size == 0) {
        return false;
    }
    const auto base = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(stack_base));
    return fault_address >= base && fault_address < (base + static_cast<std::uint64_t>(guard_size));
}

}  // namespace tradutorlinux::diagnostics
