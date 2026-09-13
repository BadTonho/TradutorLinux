#include "kernel32_memory_internal.hpp"

#include "tradutorlinux/util/unicode.hpp"

#include <charconv>
#include <cstring>
#include <sys/mman.h>

namespace tradutorlinux {

namespace {

[[nodiscard]] GlobalMemorySlot* find_global_memory_slot_locked(const void* memory,
                                                                const bool global_only) noexcept {
    if (memory == nullptr) {
        return nullptr;
    }
    for (auto& slot : g_global_memory) {
        if (slot.address == memory && (!global_only || slot.global)) {
            return slot.used ? &slot : nullptr;
        }
    }
    return nullptr;
}

[[nodiscard]] std::uintptr_t process_pointer_cookie() noexcept {
    std::uintptr_t cookie = g_pointer_cookie.load(std::memory_order_acquire);
    if (cookie != 0) {
        return cookie;
    }
    constexpr std::uintptr_t kGoldenRatio = 0x9E3779B97F4A7C15ULL;
    const std::uintptr_t pid = static_cast<std::uintptr_t>(::getpid());
    const std::uintptr_t tid = static_cast<std::uintptr_t>(::syscall(SYS_gettid));
    std::uintptr_t candidate = (pid << 32U) ^ tid ^ kGoldenRatio;
    if (candidate == 0) {
        candidate = kGoldenRatio;
    }
    std::uintptr_t expected = 0;
    if (!g_pointer_cookie.compare_exchange_strong(expected, candidate,
                                                  std::memory_order_acq_rel)) {
        candidate = expected;
    }
    return candidate;
}

struct [[maybe_unused]] MapsRegion {
    std::uintptr_t start{};
    std::uintptr_t end{};
    char permissions[5]{};
    bool has_path{false};
};

[[maybe_unused]] bool find_maps_region(const void* address, MapsRegion& result) noexcept {
    const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(address);
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        const std::size_t dash = line.find('-');
        const std::size_t separator = line.find(' ');
        if (dash == std::string::npos || separator == std::string::npos || dash > separator) {
            continue;
        }
        std::uintptr_t start = 0;
        std::uintptr_t end = 0;
        const auto start_result = std::from_chars(line.data(), line.data() + dash, start, 16);
        const auto end_result = std::from_chars(line.data() + dash + 1, line.data() + separator, end, 16);
        if (start_result.ec != std::errc{} || end_result.ec != std::errc{} ||
            target < start || target >= end) {
            continue;
        }
        const std::size_t permissions_offset = separator + 1U;
        if (line.size() < permissions_offset + 4U) {
            return false;
        }
        result.start = start;
        result.end = end;
        std::memcpy(result.permissions, line.data() + permissions_offset, 4U);
        result.permissions[4] = '\0';
        result.has_path = line.find('/', permissions_offset + 4U) != std::string::npos;
        return true;
    }
    return false;
}

[[maybe_unused]] std::uint32_t win32_protection(const char permissions[4]) noexcept {
    const bool readable = permissions[0] == 'r';
    const bool writable = permissions[1] == 'w';
    const bool executable = permissions[2] == 'x';
    if (!readable && !writable && !executable) {
        return abi::kPageNoAccess;
    }
    if (executable) {
        return writable ? abi::kPageExecuteReadWrite : abi::kPageExecuteRead;
    }
    return writable ? abi::kPageReadWrite : abi::kPageReadOnly;
}

[[maybe_unused]] int host_protection(const std::uint32_t protection) noexcept {
    switch (protection & 0xFFU) {
        case abi::kPageNoAccess:
            return PROT_NONE;
        case abi::kPageReadOnly:
            return PROT_READ;
        case abi::kPageReadWrite:
        case abi::kPageWriteCopy:
            return PROT_READ | PROT_WRITE;
        case abi::kPageExecute:
            return PROT_EXEC;
        case abi::kPageExecuteRead:
            return PROT_READ | PROT_EXEC;
        case abi::kPageExecuteReadWrite:
        case abi::kPageExecuteWriteCopy:
            return PROT_READ | PROT_WRITE;
        default:
            return -1;
    }
}

} // namespace

extern "C" {

TL_MSABI void* tl_EncodePointer(void* const pointer) noexcept {
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(pointer);
    const std::uintptr_t encoded = std::rotl(value ^ process_pointer_cookie(), 17);
    set_last_error(abi::kErrorSuccess);
    trace_process_console("process-context", "encode-pointer", "opaque");
    return reinterpret_cast<void*>(encoded);
}

TL_MSABI void* tl_DecodePointer(void* const pointer) noexcept {
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(pointer);
    const std::uintptr_t decoded = std::rotr(value, 17) ^ process_pointer_cookie();
    set_last_error(abi::kErrorSuccess);
    trace_process_console("process-context", "decode-pointer", "opaque");
    return reinterpret_cast<void*>(decoded);
}

TL_MSABI void* tl_VirtualAlloc(void* const address, const std::size_t size,
                               const std::uint32_t allocation_type,
                               const std::uint32_t protect) noexcept {
    void* base = address;
    std::size_t region = size;
    const ntdll::NtStatus st = ntdll::NtAllocateVirtualMemory(&base, &region, allocation_type, protect);
    if (st != ntdll::NtStatus::Success) {
        set_last_error(ntdll::NtStatusToDosError(st));
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return base;
}

TL_MSABI int tl_VirtualFree(void* const address, const std::size_t size,
                            const std::uint32_t free_type) noexcept {
    std::size_t region = size;
    const ntdll::NtStatus st = ntdll::NtFreeVirtualMemory(address, &region, free_type);
    if (st != ntdll::NtStatus::Success) {
        set_last_error(ntdll::NtStatusToDosError(st));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_GetProcessHeap() noexcept {
    static char g_process_heap_token = 0;
    return &g_process_heap_token;
}

TL_MSABI void* tl_HeapAlloc(void* heap, std::uint32_t flags, std::uintptr_t size) noexcept {
    (void)heap;
    void* memory = nullptr;
    if ((flags & 0x0008) != 0) {
        memory = std::calloc(1, size);
    } else {
        memory = std::malloc(size);
    }
    if (memory != nullptr) {
        bump_guest_allocation_generation();
    }
    return memory;
}

TL_MSABI int tl_HeapFree(void* heap, std::uint32_t flags, void* memory) noexcept {
    (void)heap;
    (void)flags;
    std::free(memory);
    bump_guest_allocation_generation();
    return 1;
}

TL_MSABI void* tl_HeapReAlloc(void* heap, std::uint32_t flags, void* memory,
                              std::uintptr_t new_size) noexcept {
    (void)heap;
    (void)flags;
    void* const result = std::realloc(memory, new_size);
    if (result != nullptr) {
        bump_guest_allocation_generation();
    }
    return result;
}

TL_MSABI void* tl_GlobalAlloc(const std::uint32_t flags, const std::size_t bytes) noexcept {
    constexpr std::uint32_t kAllowedFlags =
        abi::kGmemMoveable | abi::kGmemZeroinit | 0x0080U | 0x0100U | 0x0010U | 0x0020U | 0x2000U | 0x1000U;
    if ((flags & ~kAllowedFlags) != 0U) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::size_t allocation_size = bytes == 0 ? 1 : bytes;
    void* const memory = (flags & abi::kGmemZeroinit) != 0
                             ? std::calloc(1, allocation_size)
                             : std::malloc(allocation_size);
    if (memory == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    {
        std::lock_guard lock(g_global_memory_mutex);
        auto it = std::find_if(g_global_memory.begin(), g_global_memory.end(),
                               [](const GlobalMemorySlot& slot) { return !slot.used; });
        if (it == g_global_memory.end()) {
            std::free(memory);
            set_last_error(abi::kErrorNotEnoughMemory);
            return nullptr;
        }
        *it = GlobalMemorySlot{true, memory, allocation_size, flags, 0, true};
    }
    bump_guest_allocation_generation();
    set_last_error(abi::kErrorSuccess);
    return memory;
}

TL_MSABI void* tl_GlobalLock(void* const memory) noexcept {
    std::lock_guard lock(g_global_memory_mutex);
    GlobalMemorySlot* const slot = find_global_memory_slot_locked(memory, true);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }
    ++slot->lock_count;
    set_last_error(abi::kErrorSuccess);
    return slot->address;
}

TL_MSABI int tl_GlobalUnlock(void* const memory) noexcept {
    std::lock_guard lock(g_global_memory_mutex);
    GlobalMemorySlot* const slot = find_global_memory_slot_locked(memory, true);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (slot->lock_count == 0) {
        set_last_error(abi::kErrorNotLocked);
        return 0;
    }
    --slot->lock_count;
    set_last_error(abi::kErrorSuccess);
    return slot->lock_count == 0 ? 0 : 1;
}

TL_MSABI void* tl_GlobalFree(void* const memory) noexcept {
    if (memory == nullptr) {
        set_last_error(abi::kErrorSuccess);
        return nullptr;
    }
    std::lock_guard lock(g_global_memory_mutex);
    GlobalMemorySlot* const slot = find_global_memory_slot_locked(memory, true);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return memory;
    }
    std::free(slot->address);
    *slot = GlobalMemorySlot{};
    bump_guest_allocation_generation();
    set_last_error(abi::kErrorSuccess);
    return nullptr;
}

TL_MSABI void* tl_LocalAlloc(const std::uint32_t flags, const std::size_t bytes) noexcept {
    constexpr std::uint32_t kAllowedFlags =
        abi::kGmemMoveable | abi::kGmemZeroinit | 0x0080U | 0x0100U | 0x0010U | 0x0020U | 0x2000U | 0x1000U;
    if ((flags & ~kAllowedFlags) != 0U) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::size_t allocation_size = bytes == 0 ? 1 : bytes;
    void* const memory = (flags & abi::kGmemZeroinit) != 0
                             ? std::calloc(1, allocation_size)
                             : std::malloc(allocation_size);
    if (memory == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    {
        std::lock_guard lock(g_global_memory_mutex);
        auto it = std::find_if(g_global_memory.begin(), g_global_memory.end(),
                               [](const GlobalMemorySlot& slot) { return !slot.used; });
        if (it == g_global_memory.end()) {
            std::free(memory);
            set_last_error(abi::kErrorNotEnoughMemory);
            return nullptr;
        }
        *it = GlobalMemorySlot{true, memory, allocation_size, flags, 0, false};
    }
    bump_guest_allocation_generation();
    set_last_error(abi::kErrorSuccess);
    return memory;
}

TL_MSABI void* tl_LocalFree(void* memory) noexcept {
    if (memory == nullptr) {
        set_last_error(abi::kErrorSuccess);
        return nullptr;
    }
    {
        std::lock_guard lock(g_global_memory_mutex);
        if (GlobalMemorySlot* const slot = find_global_memory_slot_locked(memory, false);
            slot != nullptr) {
            if (slot->global) {
                set_last_error(abi::kErrorInvalidHandle);
                return memory;
            }
            std::free(slot->address);
            *slot = GlobalMemorySlot{};
            bump_guest_allocation_generation();
            set_last_error(abi::kErrorSuccess);
            return nullptr;
        }
    }
    if (take_local_free_block(memory)) {
        std::free(memory);
        bump_guest_allocation_generation();
        set_last_error(abi::kErrorSuccess);
        return nullptr;
    }
    set_last_error(abi::kErrorInvalidHandle);
    return memory;
}

TL_MSABI int tl_GlobalMemoryStatusEx(void* buffer) noexcept {
    abi::GuestMemoryStatusEx ms{};
    if (!read_guest_value(buffer, ms) || ms.length < sizeof(abi::GuestMemoryStatusEx)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    long pages = sysconf(_SC_PHYS_PAGES);
    long avail_pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages <= 0) pages = 1048576;
    if (avail_pages <= 0) avail_pages = 524288;
    if (page_size <= 0) page_size = 4096;

    const std::uint64_t total_phys = static_cast<std::uint64_t>(pages) *
                                     static_cast<std::uint64_t>(page_size);
    const std::uint64_t avail_phys = static_cast<std::uint64_t>(avail_pages) *
                                     static_cast<std::uint64_t>(page_size);
    const std::uint64_t used_phys = total_phys > avail_phys ? total_phys - avail_phys : 0;
    const std::uint32_t load = total_phys > 0 ? static_cast<std::uint32_t>((used_phys * 100ULL) / total_phys) : 0;

    ms.memory_load = load;
    ms.total_phys = total_phys;
    ms.avail_phys = avail_phys;
    ms.total_page_file = total_phys * 2;
    ms.avail_page_file = avail_phys * 2;
    ms.total_virtual = 0x7FFFFFFF0000ULL;
    ms.avail_virtual = 0x700000000000ULL;
    ms.avail_extended_virtual = 0;
    if (!write_guest_value(buffer, ms)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreateFileMappingA(const void* file, const void* file_mapping_attributes,
                                     const std::uint32_t protect, const std::uint32_t maximum_size_high,
                                     const std::uint32_t maximum_size_low, const char* name) noexcept {
    (void)file_mapping_attributes;
    int fd = -1;
    FileSlotGuard file_guard(file);
    if (file != nullptr && file != reinterpret_cast<const void*>(~static_cast<std::uintptr_t>(0))) {
        fd = file_guard.get() != nullptr ? file_guard.get()->fd
                                         : (file_guard.is_file_handle() ? -1 : handle_fd(file));
        if (fd < 0) {
            set_last_error(abi::kErrorInvalidHandle);
            return nullptr;
        }
    }
    std::uint64_t max_size = (static_cast<std::uint64_t>(maximum_size_high) << 32) | maximum_size_low;
    if (max_size == 0 && fd >= 0) {
        struct stat st{};
        if (fstat(fd, &st) == 0) {
            max_size = static_cast<std::uint64_t>(st.st_size);
        }
    }
    std::lock_guard<std::mutex> lock(g_mapping_mutex);
    auto it = std::find_if(g_mappings.begin(), g_mappings.end(), [](const FileMappingSlot& s) { return !s.used; });
    if (it == g_mappings.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    it->used = true;
    it->header = {runtime::HandleObjectType::FileMapping, 1};
    it->fd = fd >= 0 ? ::dup(fd) : -1;
    it->size = max_size;
    it->protect = protect;
    if (name != nullptr) {
        std::string guest_name;
        if (!runtime::copy_guest_cstring(name, 4096, guest_name)) {
            if (it->fd >= 0) {
                ::close(it->fd);
            }
            *it = FileMappingSlot{};
            set_last_error(abi::kErrorInvalidParameter);
            return nullptr;
        }
        it->name = std::move(guest_name);
    }
    set_last_error(abi::kErrorSuccess);
    return &*it;
}

TL_MSABI void* tl_CreateFileMappingW(const void* file, const void* file_mapping_attributes,
                                     const std::uint32_t protect, const std::uint32_t maximum_size_high,
                                     const std::uint32_t maximum_size_low, const std::uint16_t* name) noexcept {
    std::string utf8_name;
    if (name != nullptr) {
        std::u16string guest_name;
        if (!runtime::copy_guest_wstring(name, 4096, guest_name)) {
            set_last_error(abi::kErrorInvalidParameter);
            return nullptr;
        }
        utf8_name = util::wide_to_utf8(
            reinterpret_cast<const std::uint16_t*>(guest_name.data()), guest_name.size());
    }
    return tl_CreateFileMappingA(file, file_mapping_attributes, protect, maximum_size_high, maximum_size_low,
                                 utf8_name.empty() ? nullptr : utf8_name.c_str());
}

TL_MSABI void* tl_MapViewOfFile(const void* file_mapping_object, const std::uint32_t desired_access,
                                const std::uint32_t file_offset_high, const std::uint32_t file_offset_low,
                                const std::size_t number_of_bytes_to_map) noexcept {
    int mapping_fd = -1;
    std::uint64_t mapping_size = 0;
    {
        std::lock_guard<std::mutex> lock(g_mapping_mutex);
        const FileMappingSlot* slot = find_file_mapping_slot_locked(file_mapping_object);
        if (slot == nullptr) {
            set_last_error(abi::kErrorInvalidHandle);
            return nullptr;
        }
        mapping_fd = slot->fd;
        mapping_size = slot->size;
    }
    int prot = PROT_READ;
    if ((desired_access & 0x0002) != 0 || (desired_access & 0xF0000) != 0) {
        prot |= PROT_WRITE;
    }
    int flags = (mapping_fd >= 0) ? MAP_SHARED : (MAP_PRIVATE | MAP_ANONYMOUS);
    const std::uint64_t offset_value = (static_cast<std::uint64_t>(file_offset_high) << 32U) |
                                       file_offset_low;
    const off_t offset = static_cast<off_t>(offset_value);
    const std::size_t size = number_of_bytes_to_map > 0 ? number_of_bytes_to_map :
                             (mapping_size > offset_value ? static_cast<std::size_t>(mapping_size - offset_value)
                                                          : 4096U);
    void* result = mmap(nullptr, size, prot, flags, mapping_fd >= 0 ? mapping_fd : -1,
                        mapping_fd >= 0 ? offset : 0);
    if (result == MAP_FAILED) {
        set_last_error(errno_to_win32(errno));
        return nullptr;
    }
    bool recorded = false;
    {
        std::lock_guard<std::mutex> lock(g_allocations_mutex);
        auto it = std::find_if(g_allocations.begin(), g_allocations.end(), [](const AllocationSlot& s) { return s.address == nullptr; });
        if (it != g_allocations.end()) {
            it->address = result;
            it->size = size;
            it->view = true;
            it->allocation_protect = (prot & PROT_WRITE) != 0 ? abi::kPageReadWrite : abi::kPageReadOnly;
            it->state = abi::kMemCommit;
            it->protect = it->allocation_protect;
            recorded = true;
        }
    }
    if (!recorded) {
        // Sem rastreio não há como UnmapViewOfFile desfazer em segurança.
        munmap(result, size);
        bump_guest_allocation_generation();
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    bump_guest_allocation_generation();
    set_last_error(abi::kErrorSuccess);
    return result;
}

TL_MSABI int tl_UnmapViewOfFile(const void* base_address) noexcept {
    if (base_address == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::size_t size = 0;
    {
        std::lock_guard<std::mutex> lock(g_allocations_mutex);
        auto it = std::find_if(g_allocations.begin(), g_allocations.end(), [base_address](const AllocationSlot& s) {
            return s.view && s.address == base_address && s.size > 0;
        });
        if (it != g_allocations.end()) {
            size = it->size;
            *it = {};
        }
    }
    if (size == 0) {
        // Endereço desconhecido: nunca munmap memória que o runtime não criou.
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (munmap(const_cast<void*>(base_address), size) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    bump_guest_allocation_generation();
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_FlushViewOfFile(const void* base_address, const std::size_t number_of_bytes_to_flush) noexcept {
    if (base_address == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::size_t size = number_of_bytes_to_flush > 0 ? number_of_bytes_to_flush : 4096;
    const auto address = reinterpret_cast<std::uintptr_t>(base_address);
    if (address % 4096 != 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (msync(const_cast<void*>(base_address), size, MS_SYNC) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_HeapCreate(const std::uint32_t options, const std::size_t initial_size, const std::size_t maximum_size) noexcept {
    (void)options;
    (void)initial_size;
    (void)maximum_size;
    static char g_custom_heap_token = 0;
    return &g_custom_heap_token;
}

TL_MSABI int tl_HeapDestroy(void* heap) noexcept {
    (void)heap;
    return 1;
}

TL_MSABI int tl_HeapValidate(void* heap, const std::uint32_t flags, const void* memory) noexcept {
    (void)heap;
    (void)flags;
    (void)memory;
    return 1;
}

TL_MSABI std::size_t tl_HeapSize(void* heap, const std::uint32_t flags, const void* memory) noexcept {
    (void)heap;
    (void)flags;
    if (memory == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<std::size_t>(-1);
    }
    set_last_error(abi::kErrorSuccess);
    return malloc_usable_size(const_cast<void*>(memory));
}

TL_MSABI std::size_t tl_HeapCompact(void* heap, const std::uint32_t flags) noexcept {
    (void)heap;
    (void)flags;
    return 0;
}

TL_MSABI std::size_t tl_VirtualQueryEx(const void* const process_handle, const void* const address,
                                       void* const buffer, const std::size_t length) noexcept {
    (void)process_handle;
    if (buffer == nullptr || length < sizeof(abi::GuestMemoryBasicInformation)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    return tl_VirtualQuery(address, buffer, length);
}

TL_MSABI std::size_t tl_GetLargePageMinimum(void) noexcept {
    return 2097152U; // 2MB
}

TL_MSABI void* tl_OpenFileMappingW(const std::uint32_t desired_access, const int inherit_handle,
                                   const std::uint16_t* const name) noexcept {
    (void)desired_access;
    (void)inherit_handle;
    (void)name;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x2000);
}

TL_MSABI int tl_GetPhysicallyInstalledSystemMemory(std::uint64_t* const total_memory_in_kilobytes) noexcept {
    if (total_memory_in_kilobytes != nullptr &&
        !write_guest_value(total_memory_in_kilobytes, 16ULL * 1024ULL * 1024ULL)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::size_t tl_GlobalSize(void* const mem) noexcept {
    if (mem == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    return 4096;
}

TL_MSABI void tl_GlobalMemoryStatus(void* const buffer) noexcept {
    const std::array<std::uint32_t, 8> values{
        32,          // dwLength
        25,          // dwMemoryLoad (25%)
        0x7FFFFFFF,  // dwTotalPhys (2GB)
        0x60000000,  // dwAvailPhys (1.5GB)
        0x7FFFFFFF,  // dwTotalPageFile
        0x60000000,  // dwAvailPageFile
        0x7FFE0000,  // dwTotalVirtual
        0x70000000,  // dwAvailVirtual
    };
    if (buffer != nullptr) {
        static_cast<void>(runtime::write_guest_memory(buffer, values.data(), sizeof(values)));
    }
}

TL_MSABI int tl_VirtualProtect(void* address, std::uintptr_t size,
                               std::uint32_t new_protection,
                               std::uint32_t* old_protection) noexcept {
    if (old_protection == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::size_t region = static_cast<std::size_t>(size);
    std::uint32_t old = 0;
    const ntdll::NtStatus st = ntdll::NtProtectVirtualMemory(address, &region, new_protection, &old);
    if (st != ntdll::NtStatus::Success) {
        const std::uint32_t err = ntdll::NtStatusToDosError(st);
        // Wine mapeia InvalidParameter/AccessDenied para ERROR_INVALID_ADDRESS quando fora da VAD.
        if (st == ntdll::NtStatus::InvalidParameter || st == ntdll::NtStatus::AccessDenied) {
            // Tenta distinguir: se não achou região, retorna InvalidAddress como antes.
            set_last_error(abi::kErrorInvalidAddress);
        } else {
            set_last_error(err);
        }
        return 0;
    }
    if (!write_guest_value(old_protection, old)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uintptr_t tl_VirtualQuery(const void* address, void* memory_information,
                                        std::uintptr_t length) noexcept {
    if (memory_information == nullptr || length < sizeof(abi::GuestMemoryBasicInformation)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    ntdll::NtMemoryInformation info{};
    const ntdll::NtStatus st = ntdll::NtQueryVirtualMemory(address, &info);
    if (st != ntdll::NtStatus::Success) {
        set_last_error(abi::kErrorInvalidAddress);
        return 0;
    }
    abi::GuestMemoryBasicInformation out{};
    out.base_address = info.BaseAddress;
    out.allocation_base = info.AllocationBase;
    out.allocation_protect = info.AllocationProtect;
    out.region_size = info.RegionSize;
    out.state = info.State;
    out.protect = info.Protect;
    out.type = info.Type;
    if (!write_guest_value(memory_information, out)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return sizeof(out);
}

}  // extern "C"
}  // namespace tradutorlinux
