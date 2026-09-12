#pragma once

#include "kernel32_common.hpp"

#include "../../core/runtime_handle_state.hpp"
#include "../../core/runtime_memory_state.hpp"

#include "tradutorlinux/runtime/ntdll.hpp"

#include <bit>
#include <cstdint>

#include <malloc.h>

namespace tradutorlinux {

inline FileMappingSlot* find_file_mapping_slot_locked(const void* handle) noexcept {
    if (handle == nullptr) {
        return nullptr;
    }
    const auto addr = std::bit_cast<std::uintptr_t>(handle);
    const auto begin = std::bit_cast<std::uintptr_t>(g_mappings.data());
    const auto end = begin + g_mappings.size() * sizeof(FileMappingSlot);
    if (addr < begin || addr >= end || (addr - begin) % sizeof(FileMappingSlot) != 0) {
        return nullptr;
    }
    auto* slot = static_cast<FileMappingSlot*>(const_cast<void*>(handle));
    return slot->used ? slot : nullptr;
}

}  // namespace tradutorlinux
