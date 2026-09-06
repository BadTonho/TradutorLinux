#include "tradutorlinux/runtime/ntdll.hpp"

#include "runtime_context.hpp"
#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/runtime/error_map.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unistd.h>
#include <sys/mman.h>

namespace tradutorlinux::ntdll {
namespace {

constexpr std::uint32_t kMemCommit = 0x1000U;
constexpr std::uint32_t kMemReserve = 0x2000U;
constexpr std::uint32_t kMemRelease = 0x8000U;
constexpr std::uint32_t kMemFree = 0x10000U;

constexpr std::uint32_t kPageNoAccess = 0x01U;
constexpr std::uint32_t kPageReadOnly = 0x02U;
constexpr std::uint32_t kPageReadWrite = 0x04U;
constexpr std::uint32_t kPageWriteCopy = 0x08U;
constexpr std::uint32_t kPageExecute = 0x10U;
constexpr std::uint32_t kPageExecuteRead = 0x20U;
constexpr std::uint32_t kPageExecuteReadWrite = 0x40U;
constexpr std::uint32_t kPageExecuteWriteCopy = 0x80U;

[[nodiscard]] bool is_valid_protect(const std::uint32_t prot) noexcept {
    switch (prot) {
        case kPageNoAccess:
        case kPageReadOnly:
        case kPageReadWrite:
        case kPageWriteCopy:
        case kPageExecute:
        case kPageExecuteRead:
        case kPageExecuteReadWrite:
        case kPageExecuteWriteCopy:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] int prot_to_host(const std::uint32_t prot) noexcept {
    switch (prot) {
        case kPageNoAccess:
            return PROT_NONE;
        case kPageReadOnly:
            return PROT_READ;
        case kPageReadWrite:
        case kPageWriteCopy:
            return PROT_READ | PROT_WRITE;
        case kPageExecute:
            return PROT_EXEC;
        case kPageExecuteRead:
            return PROT_READ | PROT_EXEC;
        case kPageExecuteReadWrite:
        case kPageExecuteWriteCopy:
            // O runtime aplica W^X. A capacidade Win32 é aceita para fins de
            // compatibilidade, mas a página permanece não executável enquanto
            // estiver gravável.
            return PROT_READ | PROT_WRITE;
        default:
            return PROT_READ | PROT_WRITE;
    }
}

[[nodiscard]] std::uint32_t host_to_win_protect(const std::string_view perms) noexcept {
    const bool r = perms.size() > 0 && perms[0] == 'r';
    const bool w = perms.size() > 1 && perms[1] == 'w';
    const bool x = perms.size() > 2 && perms[2] == 'x';
    if (!r && !w && !x) return kPageNoAccess;
    if (r && !w && !x) return kPageReadOnly;
    if (r && w && !x) return kPageReadWrite;
    if (r && !w && x) return kPageExecuteRead;
    if (r && w && x) return kPageExecuteReadWrite;
    if (!r && x) return kPageExecute;
    return kPageReadWrite;
}

std::size_t page_size() noexcept {
    static const std::size_t kPage = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    return kPage > 0 ? kPage : 4096U;
}

std::size_t align_to_page(std::size_t v) noexcept {
    const std::size_t ps = page_size();
    return (v + ps - 1) & ~(ps - 1);
}

void* align_down(void* p) noexcept {
    const std::size_t ps = page_size();
    const auto addr = reinterpret_cast<std::uintptr_t>(p);
    return reinterpret_cast<void*>(addr & ~(ps - 1));
}

void trace_nt(const char* api, const char* detail, NtStatus st) noexcept {
    const std::array<diagnostics::TraceField, 3> fields{
        diagnostics::TraceField{"api", api},
        diagnostics::TraceField{"detail", detail},
        diagnostics::TraceField{"status", std::to_string(static_cast<std::uint32_t>(st))},
    };
    diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime,
                             diagnostics::TraceLevel::Info, "ntdll", fields);
}

[[nodiscard]] bool contains_range(const AllocationSlot& slot,
                                   const void* address,
                                   const std::size_t size) noexcept {
    if (slot.address == nullptr || slot.size == 0 || address == nullptr) {
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(slot.address);
    const auto target = reinterpret_cast<std::uintptr_t>(address);
    if (target < base || target - base > slot.size) {
        return false;
    }
    return size <= slot.size - (target - base);
}

[[nodiscard]] AllocationSlot* find_private_allocation_locked(const void* address,
                                                               const std::size_t size) noexcept {
    for (AllocationSlot& slot : runtime::guest_context().allocations) {
        if (!slot.view && contains_range(slot, address, size)) {
            return &slot;
        }
    }
    return nullptr;
}

[[nodiscard]] bool guest_memory_limit_exceeded_locked(const std::size_t requested) noexcept {
    const std::size_t limit = runtime::guest_context().guest_virtual_memory_limit_bytes;
    if (limit == 0) return false;
    std::size_t used = 0;
    for (const AllocationSlot& slot : runtime::guest_context().allocations) {
        if (slot.view || slot.address == nullptr || slot.state == kMemFree) continue;
        if (slot.size > std::numeric_limits<std::size_t>::max() - used) return true;
        used += slot.size;
    }
    return used > limit || requested > limit - used;
}

[[nodiscard]] bool guest_memory_limit_exceeded(const std::size_t requested) noexcept {
    std::lock_guard<std::mutex> lock(g_allocations_mutex);
    return guest_memory_limit_exceeded_locked(requested);
}

[[nodiscard]] AllocationRegion* find_allocation_region_locked(AllocationSlot& slot,
                                                                const std::uintptr_t address) noexcept {
    const auto base = reinterpret_cast<std::uintptr_t>(slot.address);
    if (address < base || address - base >= slot.size) {
        return nullptr;
    }
    const std::size_t offset = static_cast<std::size_t>(address - base);
    for (AllocationRegion& region : slot.regions) {
        if (offset >= region.offset && offset - region.offset < region.size) {
            return &region;
        }
    }
    return nullptr;
}

[[nodiscard]] bool build_allocation_protection(const AllocationSlot& slot,
                                                const std::size_t offset,
                                                const std::size_t size,
                                                const std::uint32_t protection,
                                                std::vector<AllocationRegion>& updated) noexcept {
    try {
        if (offset > slot.size || size == 0 || size > slot.size - offset) {
            return false;
        }
        const std::size_t end = offset + size;
        updated.clear();
        updated.reserve(slot.regions.size() + 2U);
        bool changed = false;
        for (const AllocationRegion& region : slot.regions) {
            if (region.offset > slot.size || region.size > slot.size - region.offset) {
                return false;
            }
            const std::size_t region_end = region.offset + region.size;
            if (region_end <= offset || region.offset >= end) {
                updated.push_back(region);
                continue;
            }
            changed = true;
            if (region.offset < offset) {
                updated.push_back(AllocationRegion{region.offset, offset - region.offset,
                                                   region.state, region.protect});
            }
            const std::size_t changed_start = std::max(region.offset, offset);
            const std::size_t changed_end = std::min(region_end, end);
            updated.push_back(AllocationRegion{changed_start, changed_end - changed_start,
                                               region.state, protection});
            if (changed_end < region_end) {
                updated.push_back(AllocationRegion{changed_end, region_end - changed_end,
                                                   region.state, region.protect});
            }
        }
        return changed && !updated.empty();
    } catch (...) {
        return false;
    }
}

}  // namespace

std::uint32_t NtStatusToDosError(NtStatus status) noexcept {
    switch (status) {
        case NtStatus::Success:
            return 0;  // ERROR_SUCCESS
        case NtStatus::NoMemory:
            return 8;  // ERROR_NOT_ENOUGH_MEMORY
        case NtStatus::MemoryNotAllocated:
        case NtStatus::ConflictingAddresses:
        case NtStatus::InvalidParameter:
            return 87;  // ERROR_INVALID_PARAMETER
        case NtStatus::AccessDenied:
            return 5;  // ERROR_ACCESS_DENIED
        default:
            return 87;
    }
}

NtStatus NtAllocateVirtualMemory(void** BaseAddress,
                                 std::size_t* RegionSize,
                                 std::uint32_t AllocationType,
                                 std::uint32_t Protect) noexcept {
    if (BaseAddress == nullptr || RegionSize == nullptr) {
        return NtStatus::InvalidParameter;
    }
    if (*RegionSize == 0) {
        return NtStatus::InvalidParameter;
    }
    constexpr std::uint32_t kAllowedAlloc = kMemCommit | kMemReserve | 0x00100000U /* MEM_TOP_DOWN */ | 0x00080000U /* MEM_RESET */ | 0x00020000U /* MEM_WRITE_WATCH */;
    if ((AllocationType & ~kAllowedAlloc) != 0 ||
        (AllocationType & (kMemCommit | kMemReserve)) == 0) {
        trace_nt("NtAllocateVirtualMemory", "allocation_type não suportado", NtStatus::InvalidParameter);
        return NtStatus::InvalidParameter;
    }
    if (!is_valid_protect(Protect)) {
        trace_nt("NtAllocateVirtualMemory", "protect inválido", NtStatus::InvalidParameter);
        return NtStatus::InvalidParameter;
    }

    const std::size_t aligned = align_to_page(*RegionSize);
    const bool reserve = (AllocationType & kMemReserve) != 0;
    const bool commit = (AllocationType & kMemCommit) != 0;

    if (guest_memory_limit_exceeded(aligned)) {
        trace_nt("NtAllocateVirtualMemory", "limite de memória do convidado", NtStatus::NoMemory);
        return NtStatus::NoMemory;
    }

    // MEM_COMMIT sobre uma reserva existente apenas muda o estado e a
    // proteção da região já registrada. Mantemos o contrato inteiro da região
    // para não permitir uma VirtualQuery incoerente com a reserva.
    if (commit && !reserve && *BaseAddress != nullptr) {
        std::lock_guard<std::mutex> lock(g_allocations_mutex);
        AllocationSlot* const slot = find_private_allocation_locked(*BaseAddress, aligned);
        if (slot == nullptr || slot->address != *BaseAddress || slot->state != kMemReserve ||
            slot->size != aligned) {
            trace_nt("NtAllocateVirtualMemory", "commit sem reserva compatível", NtStatus::InvalidParameter);
            return NtStatus::InvalidParameter;
        }
        if (::mprotect(slot->address, slot->size, prot_to_host(Protect)) != 0) {
            const int error = errno;
            trace_nt("NtAllocateVirtualMemory", "mprotect de commit falhou", NtStatus::NoMemory);
            return error == ENOMEM ? NtStatus::NoMemory : NtStatus::InvalidParameter;
        }
        slot->state = kMemCommit;
        slot->protect = Protect;
        if (slot->regions.size() == 1U) {
            slot->regions.front().state = kMemCommit;
            slot->regions.front().protect = Protect;
        }
        *RegionSize = aligned;
        runtime::invalidate_memory_map_cache();
        return NtStatus::Success;
    }

    const int prot = commit ? prot_to_host(Protect) : PROT_NONE;
    void* result = ::mmap(*BaseAddress, aligned, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (result == MAP_FAILED) {
        result = ::mmap(nullptr, aligned, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    if (result == MAP_FAILED) {
        trace_nt("NtAllocateVirtualMemory", "mmap falhou", NtStatus::NoMemory);
        return NtStatus::NoMemory;
    }

    {
        std::lock_guard<std::mutex> lock(g_allocations_mutex);
        if (guest_memory_limit_exceeded_locked(aligned)) {
            ::munmap(result, aligned);
            trace_nt("NtAllocateVirtualMemory", "limite de memória do convidado", NtStatus::NoMemory);
            return NtStatus::NoMemory;
        }
        for (AllocationSlot& slot : g_allocations) {
            if (slot.view || slot.state != kMemFree || slot.address == nullptr) continue;
            const auto free_base = reinterpret_cast<std::uintptr_t>(slot.address);
            const auto result_base = reinterpret_cast<std::uintptr_t>(result);
            const bool result_starts_in_free = result_base >= free_base &&
                                               result_base - free_base < slot.size;
            const bool free_starts_in_result = free_base >= result_base &&
                                               free_base - result_base < aligned;
            if (result_starts_in_free || free_starts_in_result) {
                slot = {};
            }
        }
        auto it = std::find_if(g_allocations.begin(), g_allocations.end(),
                               [](const AllocationSlot& s) {
                                   return s.address == nullptr ||
                                          (!s.view && s.state == kMemFree);
                               });
        if (it == g_allocations.end()) {
            ::munmap(result, aligned);
            trace_nt("NtAllocateVirtualMemory", "tabela de alocações cheia", NtStatus::NoMemory);
            return NtStatus::NoMemory;
        }
        it->address = result;
        it->size = aligned;
        it->view = false;
        it->allocation_protect = Protect;
        it->state = commit ? kMemCommit : kMemReserve;
        it->protect = commit ? Protect : 0;
        try {
            it->regions.clear();
            it->regions.push_back(AllocationRegion{0, aligned, it->state, it->protect});
        } catch (...) {
            it->address = nullptr;
            it->size = 0;
            ::munmap(result, aligned);
            trace_nt("NtAllocateVirtualMemory", "tabela de regiões sem memória", NtStatus::NoMemory);
            return NtStatus::NoMemory;
        }
    }
    runtime::invalidate_memory_map_cache();
    *BaseAddress = result;
    *RegionSize = aligned;
    return NtStatus::Success;
}

NtStatus NtFreeVirtualMemory(void* BaseAddress,
                             std::size_t* RegionSize,
                             std::uint32_t FreeType) noexcept {
    if (BaseAddress == nullptr || RegionSize == nullptr) {
        return NtStatus::InvalidParameter;
    }
    if (FreeType != kMemRelease) {
        trace_nt("NtFreeVirtualMemory", "free_type deve ser MEM_RELEASE", NtStatus::InvalidParameter);
        return NtStatus::InvalidParameter;
    }
    if (*RegionSize != 0) {
        trace_nt("NtFreeVirtualMemory", "RegionSize deve ser 0 com MEM_RELEASE", NtStatus::InvalidParameter);
        return NtStatus::InvalidParameter;
    }

    std::size_t freed_size = 0;
    {
        std::lock_guard<std::mutex> lock(g_allocations_mutex);
        auto it = std::find_if(g_allocations.begin(), g_allocations.end(),
                               [BaseAddress](const AllocationSlot& s) {
                                   return !s.view && s.address == BaseAddress &&
                                          s.state != kMemFree;
                               });
        if (it == g_allocations.end()) {
            trace_nt("NtFreeVirtualMemory", "endereço não alocado por NtAllocateVirtualMemory", NtStatus::MemoryNotAllocated);
            return NtStatus::MemoryNotAllocated;
        }
        freed_size = it->size != 0 ? it->size : *RegionSize;
        ::munmap(BaseAddress, freed_size);
        it->view = false;
        it->allocation_protect = 0;
        it->state = kMemFree;
        it->protect = 0;
        it->regions.clear();
        it->regions.push_back(AllocationRegion{0, freed_size, kMemFree, 0});
    }
    runtime::invalidate_memory_map_cache();
    // Wine zera *BaseAddress no sucesso; mantemos gesto para caller.
    *RegionSize = 0;
    (void)freed_size;
    return NtStatus::Success;
}

NtStatus NtProtectVirtualMemory(void* BaseAddress,
                                std::size_t* RegionSize,
                                std::uint32_t NewProtect,
                                std::uint32_t* OldProtect) noexcept {
    if (BaseAddress == nullptr || RegionSize == nullptr || OldProtect == nullptr) {
        return NtStatus::InvalidParameter;
    }
    if (*RegionSize == 0) {
        return NtStatus::InvalidParameter;
    }
    if (!is_valid_protect(NewProtect)) {
        return NtStatus::InvalidParameter;
    }

    void* aligned_base = align_down(BaseAddress);
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(BaseAddress);
    const std::uintptr_t aligned_start = reinterpret_cast<std::uintptr_t>(aligned_base);
    const std::size_t head = start - aligned_start;
    const std::size_t aligned_size = align_to_page(*RegionSize + head);

    // A tabela de alocações é a fonte de verdade para regiões criadas pelo
    // runtime. Reservas não podem ser protegidas antes de serem commitadas.
    std::uint32_t old = kPageReadWrite;
    std::vector<AllocationRegion> updated_regions;
    {
        std::lock_guard<std::mutex> lock(g_allocations_mutex);
        AllocationSlot* slot = find_private_allocation_locked(aligned_base, aligned_size);
        if (slot == nullptr) {
            for (AllocationSlot& candidate : runtime::guest_context().allocations) {
                if (!candidate.view && contains_range(candidate, BaseAddress, 1U)) {
                    return NtStatus::InvalidParameter;
                }
            }
        }
        if (slot != nullptr) {
            if (!contains_range(*slot, aligned_base, aligned_size)) {
                return NtStatus::InvalidParameter;
            }
            AllocationRegion* const region = find_allocation_region_locked(*slot, aligned_start);
            if (region == nullptr || region->state != kMemCommit) {
                return NtStatus::InvalidParameter;
            }
            const std::size_t tracked_offset = static_cast<std::size_t>(
                aligned_start - reinterpret_cast<std::uintptr_t>(slot->address));
            old = region->protect;
            if (!build_allocation_protection(*slot, tracked_offset, aligned_size, NewProtect,
                                              updated_regions)) {
                return NtStatus::NoMemory;
            }
            const int tracked_prot = prot_to_host(NewProtect);
            if (::mprotect(aligned_base, aligned_size, tracked_prot) != 0) {
                if (errno == ENOMEM) return NtStatus::NoMemory;
                if (errno == EACCES) return NtStatus::AccessDenied;
                return NtStatus::InvalidParameter;
            }
            slot->regions = std::move(updated_regions);
            slot->state = slot->regions.size() == 1U ? slot->regions.front().state : 0U;
            slot->protect = slot->regions.size() == 1U ? slot->regions.front().protect : 0U;
            *OldProtect = old;
            *RegionSize = aligned_size;
            runtime::invalidate_memory_map_cache();
            return NtStatus::Success;
        }
    }

    // Para regiões externas, tenta descobrir a proteção antiga via
    // /proc/self/maps (como Wine faz ao manter sua árvore VAD).
    std::ifstream maps{"/proc/self/maps"};
    std::string line;
    while (maps && std::getline(maps, line)) {
        const std::size_t dash = line.find('-');
        const std::size_t sp = line.find(' ', dash == std::string::npos ? 0 : dash);
        if (dash == std::string::npos || sp == std::string::npos) continue;
        std::uintptr_t s = 0, e = 0;
        try {
            s = std::stoull(line.substr(0, dash), nullptr, 16);
            e = std::stoull(line.substr(dash + 1, sp - dash - 1), nullptr, 16);
        } catch (...) { continue; }
        if (aligned_start >= s && aligned_start < e) {
            const std::size_t perm_off = sp + 1;
            if (line.size() >= perm_off + 3) {
                old = host_to_win_protect(line.substr(perm_off, 3));
            }
            break;
        }
    }

    const int prot = prot_to_host(NewProtect);
    if (::mprotect(aligned_base, aligned_size, prot) != 0) {
        if (errno == ENOMEM) return NtStatus::NoMemory;
        if (errno == EACCES) return NtStatus::AccessDenied;
        return NtStatus::InvalidParameter;
    }
    *OldProtect = old;
    *RegionSize = aligned_size;
    runtime::invalidate_memory_map_cache();
    return NtStatus::Success;
}

NtStatus NtQueryVirtualMemory(const void* Address, NtMemoryInformation* Info) noexcept {
    if (Address == nullptr || Info == nullptr) {
        return NtStatus::InvalidParameter;
    }
    // Primeiro verifica tabela privada de NtAllocateVirtualMemory (Wine: VAD)
    {
        std::lock_guard<std::mutex> lock(g_allocations_mutex);
        for (const auto& slot : g_allocations) {
            if (slot.address == nullptr) continue;
            const auto base = reinterpret_cast<std::uintptr_t>(slot.address);
            const auto end = base + slot.size;
            const auto addr = reinterpret_cast<std::uintptr_t>(Address);
            if (addr >= base && addr < end) {
                AllocationRegion fallback_region{0, slot.size, slot.state, slot.protect};
                const AllocationRegion* region = &fallback_region;
                const std::size_t offset = static_cast<std::size_t>(addr - base);
                for (const AllocationRegion& candidate : slot.regions) {
                    if (offset >= candidate.offset &&
                        offset - candidate.offset < candidate.size) {
                        region = &candidate;
                        break;
                    }
                }
                Info->BaseAddress = reinterpret_cast<void*>(base + region->offset);
                const bool free = region->state == kMemFree;
                Info->AllocationBase = free ? nullptr : slot.address;
                Info->AllocationProtect = free ? 0 : slot.allocation_protect;
                Info->RegionSize = region->size - (offset - region->offset);
                Info->State = region->state;
                Info->Protect = region->state == kMemCommit ? region->protect : 0;
                Info->Type = free ? 0 : (slot.view ? 0x40000U : 0x20000U);  // MEM_MAPPED/MEM_PRIVATE
                return NtStatus::Success;
            }
        }
    }
    // Fallback: parse /proc/self/maps como Wine faz após esgotar VAD.
    std::ifstream maps{"/proc/self/maps"};
    if (!maps) return NtStatus::InvalidParameter;
    std::string line;
    const auto target = reinterpret_cast<std::uintptr_t>(Address);
    while (std::getline(maps, line)) {
        const std::size_t dash = line.find('-');
        const std::size_t sp = line.find(' ', dash == std::string::npos ? 0 : dash);
        if (dash == std::string::npos || sp == std::string::npos) continue;
        std::uintptr_t s = 0, e = 0;
        try {
            s = std::stoull(line.substr(0, dash), nullptr, 16);
            e = std::stoull(line.substr(dash + 1, sp - dash - 1), nullptr, 16);
        } catch (...) { continue; }
        if (target >= s && target < e) {
            const std::size_t perm_off = sp + 1;
            std::string_view perms;
            if (line.size() >= perm_off + 3) perms = std::string_view{line}.substr(perm_off, 3);
            const std::uint32_t prot = host_to_win_protect(perms);
            Info->BaseAddress = reinterpret_cast<void*>(s);
            Info->AllocationBase = reinterpret_cast<void*>(s);
            Info->AllocationProtect = prot;
            Info->RegionSize = e - target;
            Info->State = kMemCommit;
            Info->Protect = prot;
            // Heurística Wine: se mapeado de arquivo vs anon → MEM_IMAGE vs MEM_PRIVATE
            Info->Type = line.find('/') != std::string::npos ? 0x1000000U : 0x20000U;
            return NtStatus::Success;
        }
    }
    return NtStatus::InvalidParameter;
}

}  // namespace tradutorlinux::ntdll
