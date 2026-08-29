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
            return PROT_READ | PROT_WRITE | PROT_EXEC;
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
    // Contrato Fase 5: apenas MEM_COMMIT|MEM_RESERVE. Wine aceita separado,
    // mas mantemos strict e documentamos. Aceita qualquer combinação que inclua
    // ao menos COMMIT|RESERVE para compatibilidade com alvos que usam 0x3000.
    constexpr std::uint32_t kAllowedAlloc = kMemCommit | kMemReserve | 0x00100000U /* MEM_TOP_DOWN */ | 0x00080000U /* MEM_RESET */ | 0x00020000U /* MEM_WRITE_WATCH */;
    if ((AllocationType & kAllowedAlloc) == 0) {
        trace_nt("NtAllocateVirtualMemory", "allocation_type não suportado", NtStatus::InvalidParameter);
        return NtStatus::InvalidParameter;
    }
    if (!is_valid_protect(Protect)) {
        trace_nt("NtAllocateVirtualMemory", "protect inválido", NtStatus::InvalidParameter);
        return NtStatus::InvalidParameter;
    }

    const std::size_t aligned = align_to_page(*RegionSize);
    const int prot = prot_to_host(Protect);
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
        auto it = std::find_if(g_allocations.begin(), g_allocations.end(),
                               [](const AllocationSlot& s) { return s.address == nullptr; });
        if (it != g_allocations.end()) {
            it->address = result;
            it->size = aligned;
        } else {
            // Tabela cheia — ainda retorna memória, mas sem tracking para VirtualQuery.
            // Wine nunca perde tracking; aqui logamos exaustão como Wine faria em
            // server/mapping.c com categoria "exhaustion".
            const std::array<diagnostics::TraceField, 2> fields{
                diagnostics::TraceField{"category", "exhaustion"},
                diagnostics::TraceField{"detail", "g_allocations cheia em NtAllocateVirtualMemory"},
            };
            diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime,
                                     diagnostics::TraceLevel::Error, "resource-exhaustion", fields);
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
                                   return !s.view && s.address == BaseAddress;
                               });
        if (it == g_allocations.end()) {
            trace_nt("NtFreeVirtualMemory", "endereço não alocado por NtAllocateVirtualMemory", NtStatus::MemoryNotAllocated);
            return NtStatus::MemoryNotAllocated;
        }
        freed_size = it->size != 0 ? it->size : *RegionSize;
        ::munmap(BaseAddress, freed_size);
        *it = {};
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

    // Tenta descobrir proteção antiga via /proc/self/maps (como Wine faz ao
    // manter VAD tree). Simplificado: lê maps.
    std::uint32_t old = kPageReadWrite;
    {
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
                Info->BaseAddress = slot.address;
                Info->AllocationBase = slot.address;
                Info->AllocationProtect = kPageReadWrite;
                Info->RegionSize = slot.size - (addr - base);
                Info->State = kMemCommit;
                Info->Protect = kPageReadWrite;
                Info->Type = 0x20000U;  // MEM_PRIVATE
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
