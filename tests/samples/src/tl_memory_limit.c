typedef unsigned long dword_t;
typedef unsigned long long size_t_guest;
typedef int bool_t;

__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) void* VirtualAlloc(void* address, size_t_guest size,
                                               dword_t allocation_type, dword_t protection);
__attribute__((dllimport)) bool_t VirtualFree(void* address, size_t_guest size,
                                               dword_t free_type);
__attribute__((dllimport)) dword_t GetLastError(void);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    // The runtime applies the configured RLIMIT_AS after the loader has
    // prepared the image. A 1 GiB request must fail under the 128 MiB test
    // limit without taking down the host.
    void* const memory = VirtualAlloc((void*)0, 0x40000000ULL, 0x3000U, 0x04U);
    if (memory != (void*)0) {
        VirtualFree(memory, 0, 0x8000U);
        ExitProcess(1U);
    }
    if (GetLastError() != 8U) {
        ExitProcess(2U);
    }
    static const char message[] = "memory-limit\n";
    dword_t written = 0;
    if (!WriteFile(GetStdHandle((dword_t)-11), message, sizeof(message) - 1U,
                   &written, (void*)0) || written != sizeof(message) - 1U) {
        ExitProcess(3U);
    }
    ExitProcess(0U);
}
