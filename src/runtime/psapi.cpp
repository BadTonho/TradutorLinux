#include "tradutorlinux/runtime/psapi.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <unistd.h>

#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/util/unicode.hpp"

namespace tradutorlinux {

namespace {

struct GuestProcessMemoryCounters {
    std::uint32_t cb;
    std::uint32_t page_fault_count;
    std::size_t peak_working_set_size;
    std::size_t working_set_size;
    std::size_t quota_peak_paged_pool_usage;
    std::size_t quota_paged_pool_usage;
    std::size_t quota_peak_non_paged_pool_usage;
    std::size_t quota_non_paged_pool_usage;
    std::size_t pagefile_usage;
    std::size_t peak_pagefile_usage;
};

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

}  // namespace

extern "C" {

TL_PSAPI_MSABI int tl_EnumProcesses(std::uint32_t* process_ids, const std::uint32_t size,
                                    std::uint32_t* bytes_returned) noexcept {
    if (process_ids == nullptr || size < sizeof(std::uint32_t) || !mapped_range(process_ids, size, true) ||
        bytes_returned == nullptr || !mapped_range(bytes_returned, sizeof(std::uint32_t), true)) {
        return 0;
    }
    process_ids[0] = static_cast<std::uint32_t>(::getpid());
    *bytes_returned = sizeof(std::uint32_t);
    return 1;
}

TL_PSAPI_MSABI int tl_EnumProcessModules(const void* process, void** modules,
                                        const std::uint32_t size, std::uint32_t* needed) noexcept {
    (void)process;
    if (needed == nullptr || !mapped_range(needed, sizeof(std::uint32_t), true)) {
        return 0;
    }
    *needed = sizeof(void*);
    if (modules == nullptr || size < sizeof(void*) || !mapped_range(modules, size, true)) {
        return 1;
    }
    static char g_main_module_token = 0;
    modules[0] = &g_main_module_token;
    return 1;
}

TL_PSAPI_MSABI int tl_EnumProcessModulesEx(const void* process, void** modules,
                                          const std::uint32_t size, std::uint32_t* needed,
                                          const std::uint32_t filter_flag) noexcept {
    (void)filter_flag;
    return tl_EnumProcessModules(process, modules, size, needed);
}

TL_PSAPI_MSABI std::uint32_t tl_GetModuleBaseNameA(const void* process, void* module,
                                                   char* base_name, const std::uint32_t size) noexcept {
    (void)process;
    (void)module;
    if (base_name == nullptr || size == 0 || !mapped_range(base_name, size, true)) {
        return 0;
    }
    char full[4096]{};
    tl_GetModuleFileNameA(nullptr, full, sizeof(full));
    const char* p = full;
    for (const char* cur = full; *cur != '\0'; ++cur) {
        if (*cur == '\\' || *cur == '/') {
            p = cur + 1;
        }
    }
    std::strncpy(base_name, p, size - 1);
    base_name[size - 1] = '\0';
    return static_cast<std::uint32_t>(std::strlen(base_name));
}

TL_PSAPI_MSABI std::uint32_t tl_GetModuleBaseNameW(const void* process, void* module,
                                                   std::uint16_t* base_name, const std::uint32_t size) noexcept {
    (void)process;
    (void)module;
    if (base_name == nullptr || size == 0 || !mapped_range(base_name, size * sizeof(std::uint16_t), true)) {
        return 0;
    }
    char buf[256]{};
    tl_GetModuleBaseNameA(process, module, buf, sizeof(buf));
    const std::u16string u16 = util::utf8_to_wide(buf);
    const std::size_t len = std::min<std::size_t>(u16.size(), size - 1);
    std::copy(u16.begin(), u16.begin() + static_cast<std::ptrdiff_t>(len), base_name);
    base_name[len] = 0;
    return static_cast<std::uint32_t>(len);
}

TL_PSAPI_MSABI std::uint32_t tl_GetModuleFileNameExA(const void* process, void* module,
                                                     char* filename, const std::uint32_t size) noexcept {
    (void)process;
    (void)module;
    return tl_GetModuleFileNameA(nullptr, filename, size);
}

TL_PSAPI_MSABI std::uint32_t tl_GetModuleFileNameExW(const void* process, void* module,
                                                     std::uint16_t* filename, const std::uint32_t size) noexcept {
    (void)process;
    (void)module;
    char buf[4096]{};
    tl_GetModuleFileNameA(nullptr, buf, sizeof(buf));
    const std::u16string u16 = util::utf8_to_wide(buf);
    const std::size_t len = std::min<std::size_t>(u16.size(), size - 1);
    std::copy(u16.begin(), u16.begin() + static_cast<std::ptrdiff_t>(len), filename);
    filename[len] = 0;
    return static_cast<std::uint32_t>(len);
}

TL_PSAPI_MSABI int tl_GetProcessMemoryInfo(const void* process, void* counters,
                                          const std::uint32_t size) noexcept {
    (void)process;
    if (counters == nullptr || size < sizeof(GuestProcessMemoryCounters) ||
        !mapped_range(counters, sizeof(GuestProcessMemoryCounters), true)) {
        return 0;
    }
    auto* mem = static_cast<GuestProcessMemoryCounters*>(counters);
    std::memset(mem, 0, sizeof(*mem));
    mem->cb = sizeof(GuestProcessMemoryCounters);
    long pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages <= 0) pages = 65536;
    if (page_size <= 0) page_size = 4096;
    mem->working_set_size = 32ULL * 1024ULL * 1024ULL; // ~32MB default working set
    mem->peak_working_set_size = mem->working_set_size;
    mem->pagefile_usage = mem->working_set_size;
    mem->peak_pagefile_usage = mem->working_set_size;
    return 1;
}

}  // extern "C"

}  // namespace tradutorlinux
