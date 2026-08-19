#include "tradutorlinux/runtime/memory_validator.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
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

std::mutex g_map_mutex;
std::vector<MemoryRegion> g_map_regions;
std::atomic<std::size_t> g_allocation_generation{1};
std::size_t g_cached_generation{0};

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
    g_cached_generation = g_allocation_generation.load(std::memory_order_relaxed);
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

    std::lock_guard<std::mutex> lock(g_map_mutex);
    if (g_cached_generation != g_allocation_generation.load(std::memory_order_relaxed) || g_map_regions.empty()) {
        rebuild_cache_locked();
    }

    for (const auto& region : g_map_regions) {
        if (target_start >= region.start && target_end <= region.end) {
            return writable ? region.writable : region.readable;
        }
    }

    return false;
}

bool validate_mapped_cstring(const char* str, const std::size_t max_len) noexcept {
    if (str == nullptr) {
        return false;
    }

    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(str);
    std::lock_guard<std::mutex> lock(g_map_mutex);
    if (g_cached_generation != g_allocation_generation.load(std::memory_order_relaxed) || g_map_regions.empty()) {
        rebuild_cache_locked();
    }

    for (std::size_t index = 0; index < max_len; ++index) {
        const std::uintptr_t curr_addr = start + index;
        bool mapped = false;
        for (const auto& region : g_map_regions) {
            if (curr_addr >= region.start && curr_addr < region.end) {
                if (!region.readable) {
                    return false;
                }
                mapped = true;
                // Otimização: se o resto da string cabe na região atual, buscar o nulo diretamente na memória
                const std::size_t remaining_in_region = static_cast<std::size_t>(region.end - curr_addr);
                const char* current_ptr = reinterpret_cast<const char*>(curr_addr);
                const char* null_ptr = static_cast<const char*>(std::memchr(current_ptr, '\0', std::min(max_len - index, remaining_in_region)));
                if (null_ptr != nullptr) {
                    return true;
                }
                index += remaining_in_region - 1;
                break;
            }
        }
        if (!mapped) {
            return false;
        }
    }

    return false;
}

bool validate_mapped_wstring(const std::uint16_t* wstr, const std::size_t max_len) noexcept {
    if (wstr == nullptr) {
        return false;
    }

    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(wstr);
    std::lock_guard<std::mutex> lock(g_map_mutex);
    if (g_cached_generation != g_allocation_generation.load(std::memory_order_relaxed) || g_map_regions.empty()) {
        rebuild_cache_locked();
    }

    for (std::size_t index = 0; index < max_len; ++index) {
        const std::uintptr_t curr_addr = start + index * sizeof(std::uint16_t);
        bool mapped = false;
        for (const auto& region : g_map_regions) {
            if (curr_addr >= region.start && curr_addr + sizeof(std::uint16_t) <= region.end) {
                if (!region.readable) {
                    return false;
                }
                mapped = true;
                if (wstr[index] == 0) {
                    return true;
                }
                break;
            }
        }
        if (!mapped) {
            return false;
        }
    }

    return false;
}

}  // namespace tradutorlinux::runtime
