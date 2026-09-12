#pragma once

#include <cstddef>
#include <span>

namespace tradutorlinux::runtime {

// Calcula a quantidade de unidades UTF-16, incluindo o terminador de cada
// entrada e o terminador final, sem permitir overflow da soma ou da alocação.
[[nodiscard]] bool checked_environment_block_units(
    std::span<const std::size_t> entry_lengths, std::size_t& units) noexcept;

}  // namespace tradutorlinux::runtime
