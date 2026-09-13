#include "tradutorlinux/runtime/memory_validator.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include <cerrno>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

namespace tradutorlinux::runtime {
namespace {

struct MemoryRegion {
    std::uintptr_t start{0};
    std::uintptr_t end{0};
    bool readable{false};
    bool writable{false};
};

std::shared_mutex g_map_mutex;
std::vector<MemoryRegion> g_map_regions;
std::atomic<std::size_t> g_allocation_generation{1};
std::atomic<std::size_t> g_cached_generation{0};

void rebuild_cache_locked() noexcept {
    g_map_regions.clear();
    std::ifstream maps{"/proc/self/maps"};
    if (!maps) {
        return;
    }

    std::string line;
    while (std::getline(maps, line)) {
        const std::size_t dash = line.find('-');
        const std::size_t space = line.find(' ', dash == std::string::npos ? 0 : dash);
        if (dash == std::string::npos || space == std::string::npos || dash == 0) {
            continue;
        }

        std::uintptr_t region_start = 0;
        std::uintptr_t region_end = 0;
        if (std::from_chars(line.data(), line.data() + dash, region_start, 16).ec != std::errc{} ||
            std::from_chars(line.data() + dash + 1, line.data() + space, region_end, 16).ec != std::errc{} ||
            region_start > region_end) {
            continue;
        }

        const std::size_t permissions_offset = space + 1;
        if (line.size() < permissions_offset + 2) {
            continue;
        }

        const bool readable = line[permissions_offset] == 'r';
        const bool writable = line[permissions_offset + 1] == 'w';

        g_map_regions.push_back(MemoryRegion{region_start, region_end, readable, writable});
    }
    g_cached_generation.store(g_allocation_generation.load(std::memory_order_relaxed),
                              std::memory_order_release);
}

void ensure_cache_valid() noexcept {
    if (g_cached_generation.load(std::memory_order_acquire) ==
            g_allocation_generation.load(std::memory_order_acquire) &&
        !g_map_regions.empty()) {
        return;
    }
    std::unique_lock<std::shared_mutex> write_lock(g_map_mutex);
    if (g_cached_generation.load(std::memory_order_relaxed) !=
            g_allocation_generation.load(std::memory_order_relaxed) ||
        g_map_regions.empty()) {
        rebuild_cache_locked();
    }
}

// Busca binaria O(log N) para encontrar a regiao que cobre address
const MemoryRegion* find_region_covering(const std::vector<MemoryRegion>& regions,
                                         const std::uintptr_t address) noexcept {
    if (regions.empty()) {
        return nullptr;
    }
    auto it = std::upper_bound(regions.begin(), regions.end(), address,
                               [](const std::uintptr_t addr, const MemoryRegion& reg) noexcept {
                                   return addr < reg.start;
                               });
    if (it == regions.begin()) {
        return nullptr;
    }
    --it;
    if (address >= it->start && address < it->end) {
        return &(*it);
    }
    return nullptr;
}

}  // namespace

namespace {

GuestMemoryAccessResult copy_guest_memory_via_proc_mem(const bool write_to_guest,
                                                       void* const guest_address,
                                                       const void* const host_address,
                                                       const std::size_t size) noexcept {
    // O_RDWR is required by some kernels for a writable /proc/self/mem
    // descriptor, even though the operation itself is pwrite(2).
    const int flags = write_to_guest ? O_RDWR : O_RDONLY;
    const int descriptor = ::open("/proc/thread-self/mem", flags | O_CLOEXEC);
    if (descriptor < 0) {
        if (errno == EACCES || errno == EPERM) {
            return {GuestMemoryAccessStatus::PermissionDenied, 0};
        }
        return {GuestMemoryAccessStatus::SystemError, 0};
    }

    const off_t offset = static_cast<off_t>(reinterpret_cast<std::uintptr_t>(guest_address));
    errno = 0;
    const ssize_t result = write_to_guest
                               ? ::pwrite(descriptor, host_address, size, offset)
                               : ::pread(descriptor, const_cast<void*>(host_address), size, offset);
    const int saved_errno = errno;
    static_cast<void>(::close(descriptor));

    if (result == static_cast<ssize_t>(size)) {
        return {GuestMemoryAccessStatus::Success, size};
    }
    if (result > 0) {
        return {GuestMemoryAccessStatus::Partial, static_cast<std::size_t>(result)};
    }
    switch (saved_errno) {
        case EACCES:
        case EPERM:
            return {GuestMemoryAccessStatus::PermissionDenied, 0};
        case EFAULT:
        case EIO:
            return {GuestMemoryAccessStatus::Unmapped, 0};
        default:
            return {GuestMemoryAccessStatus::SystemError, 0};
    }
}

GuestMemoryAccessResult copy_guest_memory(const bool write_to_guest,
                                          void* const guest_address,
                                          const void* const host_address,
                                          const std::size_t size) noexcept {
    if (guest_address == nullptr || host_address == nullptr || size == 0) {
        return {GuestMemoryAccessStatus::InvalidArgument, 0};
    }

    const std::uintptr_t guest_start = reinterpret_cast<std::uintptr_t>(guest_address);
    if (size > std::numeric_limits<std::uintptr_t>::max() - guest_start) {
        return {GuestMemoryAccessStatus::InvalidArgument, 0};
    }
    if (size > static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()) ||
        guest_start > static_cast<std::uintptr_t>(std::numeric_limits<off_t>::max())) {
        return {GuestMemoryAccessStatus::InvalidArgument, 0};
    }

    iovec local{const_cast<void*>(host_address), size};
    iovec remote{guest_address, size};
    errno = 0;
    const long result = write_to_guest
                            ? ::syscall(SYS_process_vm_writev, ::getpid(), &local, 1,
                                        &remote, 1, 0)
                            : ::syscall(SYS_process_vm_readv, ::getpid(), &local, 1,
                                        &remote, 1, 0);
    if (result == static_cast<long>(size)) {
        return {GuestMemoryAccessStatus::Success, size};
    }
    if (result > 0) {
        return {GuestMemoryAccessStatus::Partial, static_cast<std::size_t>(result)};
    }
    switch (errno) {
        case EFAULT:
            return {GuestMemoryAccessStatus::Unmapped, 0};
        case EACCES:
        case EPERM:
        case ENOSYS:
            if (write_to_guest) {
                // The fallback may use FOLL_FORCE and must not turn a stale
                // /proc/self/maps snapshot into a writable guest page.
                invalidate_memory_map_cache();
                if (!validate_mapped_range(guest_address, size, true)) {
                    return {GuestMemoryAccessStatus::PermissionDenied, 0};
                }
            }
            return copy_guest_memory_via_proc_mem(write_to_guest, guest_address, host_address, size);
        default:
            return {GuestMemoryAccessStatus::SystemError, 0};
    }
}

}  // namespace

GuestMemoryAccessResult read_guest_memory(const void* const source,
                                          void* const destination,
                                          const std::size_t size) noexcept {
    return copy_guest_memory(false, const_cast<void*>(source), destination, size);
}

GuestMemoryAccessResult write_guest_memory(void* const destination,
                                           const void* const source,
                                           const std::size_t size) noexcept {
    return copy_guest_memory(true, destination, source, size);
}

void invalidate_memory_map_cache() noexcept {
    g_allocation_generation.fetch_add(1, std::memory_order_relaxed);
}

bool validate_mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    if (address == nullptr || size == 0) {
        return false;
    }

    const std::uintptr_t target_start = reinterpret_cast<std::uintptr_t>(address);
    if (size > std::numeric_limits<std::uintptr_t>::max() - target_start) {
        return false;
    }
    const std::uintptr_t target_end = target_start + size;

    ensure_cache_valid();

    std::shared_lock<std::shared_mutex> lock(g_map_mutex);
    std::uintptr_t cur = target_start;

    while (cur < target_end) {
        const MemoryRegion* region = find_region_covering(g_map_regions, cur);
        if (region == nullptr) {
            return false;
        }
        if (writable ? !region->writable : !region->readable) {
            return false;
        }
        cur = std::min(target_end, region->end);
    }

    return true;
}

bool validate_mapped_cstring(const char* str, const std::size_t max_len) noexcept {
    if (str == nullptr) {
        return false;
    }

    ensure_cache_valid();

    std::shared_lock<std::shared_mutex> lock(g_map_mutex);
    std::uintptr_t cur = reinterpret_cast<std::uintptr_t>(str);
    std::size_t checked = 0;

    while (checked < max_len) {
        const MemoryRegion* region = find_region_covering(g_map_regions, cur);
        if (region == nullptr || !region->readable) {
            return false;
        }
        const std::size_t bytes_in_region = static_cast<std::size_t>(region->end - cur);
        const std::size_t scan_count = std::min(max_len - checked, bytes_in_region);
        std::array<char, 4096> scratch{};
        const std::size_t chunk_count = std::min(scan_count, scratch.size());
        const GuestMemoryAccessResult access =
            read_guest_memory(reinterpret_cast<const void*>(cur), scratch.data(), chunk_count);
        if (access.status != GuestMemoryAccessStatus::Success) {
            return false;
        }
        const void* const null_found = std::memchr(scratch.data(), '\0', chunk_count);
        if (null_found != nullptr) {
            return true;
        }
        checked += chunk_count;
        cur += chunk_count;
    }

    return false;
}

bool validate_mapped_wstring(const std::uint16_t* wstr, const std::size_t max_len) noexcept {
    if (wstr == nullptr || reinterpret_cast<std::uintptr_t>(wstr) % alignof(std::uint16_t) != 0) {
        return false;
    }

    ensure_cache_valid();

    std::shared_lock<std::shared_mutex> lock(g_map_mutex);
    std::uintptr_t cur = reinterpret_cast<std::uintptr_t>(wstr);
    std::size_t checked = 0;

    while (checked < max_len) {
        const MemoryRegion* region = find_region_covering(g_map_regions, cur);
        if (region == nullptr || !region->readable) {
            return false;
        }
        const std::size_t bytes_in_region = static_cast<std::size_t>(region->end - cur);
        const std::size_t elements_in_region = bytes_in_region / sizeof(std::uint16_t);
        if (elements_in_region == 0) {
            return false;
        }
        const std::size_t scan_count = std::min(max_len - checked, elements_in_region);
        std::array<std::uint16_t, 2048> scratch{};
        const std::size_t chunk_count = std::min(scan_count, scratch.size());
        const GuestMemoryAccessResult access = read_guest_memory(
            reinterpret_cast<const void*>(cur), scratch.data(), chunk_count * sizeof(std::uint16_t));
        if (access.status != GuestMemoryAccessStatus::Success) {
            return false;
        }
        for (std::size_t i = 0; i < chunk_count; ++i) {
            if (scratch[i] == 0) {
                return true;
            }
        }
        checked += chunk_count;
        cur += chunk_count * sizeof(std::uint16_t);
    }

    return false;
}

}  // namespace tradutorlinux::runtime
