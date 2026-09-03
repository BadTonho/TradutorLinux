#include "tradutorlinux/runtime/memory_validator.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstring>
#include <fstream>
#include <limits>
#include <shared_mutex>
#include <string>
#include <vector>

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
        const char* const ptr = reinterpret_cast<const char*>(cur);
        const void* const null_found = std::memchr(ptr, '\0', scan_count);
        if (null_found != nullptr) {
            return true;
        }
        checked += scan_count;
        cur += scan_count;
    }

    return false;
}

bool validate_mapped_wstring(const std::uint16_t* wstr, const std::size_t max_len) noexcept {
    if (wstr == nullptr) {
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
        const auto* const ptr = reinterpret_cast<const std::uint16_t*>(cur);
        for (std::size_t i = 0; i < scan_count; ++i) {
            if (ptr[i] == 0) {
                return true;
            }
        }
        checked += scan_count;
        cur += scan_count * sizeof(std::uint16_t);
    }

    return false;
}

}  // namespace tradutorlinux::runtime
