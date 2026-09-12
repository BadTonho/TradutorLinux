#pragma once

#include "runtime_state_common.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace tradutorlinux {

using AllocationSlot = runtime::GuestContext::ContextAllocationSlot;
using AllocationRegion = runtime::GuestContext::ContextAllocationRegion;
using GlobalMemorySlot = runtime::GuestContext::ContextGlobalMemorySlot;
using DibSlot = runtime::GuestContext::ContextDibSlot;

#define g_allocations (::tradutorlinux::runtime::guest_context().allocations)
#define g_allocations_mutex (::tradutorlinux::runtime::guest_context().allocations_mutex)
#define g_mappings (::tradutorlinux::runtime::guest_context().mappings)
#define g_mapping_mutex (::tradutorlinux::runtime::guest_context().mapping_mutex)
#define g_global_memory (::tradutorlinux::runtime::guest_context().global_memory)
#define g_global_memory_mutex (::tradutorlinux::runtime::guest_context().global_memory_mutex)
#define g_local_free_blocks (::tradutorlinux::runtime::guest_context().local_free_blocks)
#define g_local_free_mutex (::tradutorlinux::runtime::guest_context().local_free_mutex)
#define g_dibs (::tradutorlinux::runtime::guest_context().dibs)
#define g_dib_mutex (::tradutorlinux::runtime::guest_context().dib_mutex)

[[nodiscard]] bool register_local_free_block(void* address) noexcept;
[[nodiscard]] bool take_local_free_block(void* address) noexcept;
[[nodiscard]] bool register_wts_allocation(void* address) noexcept;
[[nodiscard]] bool take_wts_allocation(void* address) noexcept;
void bump_guest_allocation_generation() noexcept;

std::uint32_t decode_multibyte(std::uint32_t code_page, const std::uint8_t* bytes,
                               std::size_t length, std::size_t& pos,
                               bool use_glyph_chars = false) noexcept;

}  // namespace tradutorlinux
