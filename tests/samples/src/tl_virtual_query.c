typedef unsigned long dword_t;
typedef unsigned long long size_t_guest;
typedef int bool_t;

typedef struct memory_basic_information_t {
    void* base_address;
    void* allocation_base;
    dword_t allocation_protect;
    dword_t padding1;
    size_t_guest region_size;
    dword_t state;
    dword_t protect;
    dword_t type;
    dword_t padding2;
} memory_basic_information_t;

__attribute__((dllimport)) void* VirtualAlloc(void* address, size_t_guest size,
                                               dword_t allocation_type, dword_t protection);
__attribute__((dllimport)) bool_t VirtualFree(void* address, size_t_guest size,
                                              dword_t free_type);
__attribute__((dllimport)) size_t_guest VirtualQuery(const void* address,
                                                      memory_basic_information_t* information,
                                                      size_t_guest length);
__attribute__((dllimport)) bool_t VirtualProtect(void* address, size_t_guest size,
                                                 dword_t new_protection,
                                                 dword_t* old_protection);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static bool_t query_matches(const void* address, const void* expected_region_base,
                            const void* expected_allocation_base, dword_t state,
                            dword_t protection, size_t_guest region_size,
                            memory_basic_information_t* information) {
    if (VirtualQuery(address, information, sizeof(*information)) != sizeof(*information)) {
        return 0;
    }
    return information->base_address == expected_region_base &&
           information->allocation_base == expected_allocation_base &&
           information->allocation_protect == 0x04U &&
           information->region_size == region_size &&
           information->state == state &&
           information->protect == protection &&
           information->type == 0x20000U;
}

static bool_t query_is_free(const void* address, size_t_guest region_size,
                            memory_basic_information_t* information) {
    if (VirtualQuery(address, information, sizeof(*information)) != sizeof(*information)) {
        return 0;
    }
    return information->base_address == address &&
           information->allocation_base == (void*)0 &&
           information->allocation_protect == 0U &&
           information->region_size == region_size &&
           information->state == 0x10000U &&
           information->protect == 0U &&
           information->type == 0U;
}

void tl_entry(void) {
    memory_basic_information_t information = {0};
    dword_t old_protection = 0;
    void* memory = VirtualAlloc((void*)0, 0x2000ULL, 0x2000U, 0x04U);
    if (memory == (void*)0 ||
        !query_matches(memory, memory, memory, 0x2000U, 0U, 0x2000ULL, &information)) {
        ExitProcess(1U);
    }

    if (VirtualAlloc(memory, 0x2000ULL, 0x1000U, 0x04U) != memory ||
        !query_matches(memory, memory, memory, 0x1000U, 0x04U, 0x2000ULL, &information)) {
        ExitProcess(2U);
    }

    if (!VirtualProtect(memory, 0x1000ULL, 0x02U, &old_protection) ||
        old_protection != 0x04U ||
        !query_matches(memory, memory, memory, 0x1000U, 0x02U, 0x1000ULL, &information) ||
        !query_matches((char*)memory + 0x1000, (char*)memory + 0x1000, memory,
                       0x1000U, 0x04U, 0x1000ULL, &information)) {
        ExitProcess(3U);
    }

    if (!VirtualFree(memory, 0, 0x8000U)) {
        ExitProcess(4U);
    }
    if (!query_is_free(memory, 0x2000ULL, &information)) {
        ExitProcess(5U);
    }

    static const char message[] = "virtual-query\n";
    dword_t written = 0;
    if (!WriteFile(GetStdHandle((dword_t)-11), message, sizeof(message) - 1U,
                   &written, (void*)0) || written != sizeof(message) - 1U) {
        ExitProcess(6U);
    }
    ExitProcess(0U);
}
