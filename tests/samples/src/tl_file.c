typedef unsigned long dword_t;
typedef unsigned long long size_t_guest;
typedef int bool_t;

__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) bool_t ReadFile(const void* handle, void* buffer,
                                            dword_t bytes_to_read, dword_t* bytes_read,
                                            const void* overlapped);
__attribute__((dllimport)) void* CreateFileA(const char* path, dword_t desired_access,
                                              dword_t share_mode, void* security_attributes,
                                              dword_t creation_disposition, dword_t flags,
                                              void* template_file);
__attribute__((dllimport)) bool_t CloseHandle(const void* handle);
__attribute__((dllimport)) void* VirtualAlloc(void* address, size_t_guest size,
                                               dword_t allocation_type, dword_t protection);
__attribute__((dllimport)) bool_t VirtualFree(void* address, size_t_guest size,
                                               dword_t free_type);
__attribute__((dllimport)) dword_t GetLastError(void);
__attribute__((dllimport)) void SetLastError(dword_t error);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);

void tl_entry(void) {
    static const char path[] = "tl_phase5_data.bin";
    static const char text[] = "fase5\n";
    void* memory = VirtualAlloc((void*)0, 64, 0x3000, 0x04);
    dword_t count = 0;
    if (memory == (void*)0) {
        ExitProcess(1U);
    }
    for (dword_t index = 0; index < 6; ++index) {
        ((char*)memory)[index] = text[index];
    }
    void* file = CreateFileA(path, 0x40000000, 0, (void*)0, 2, 0, (void*)0);
    if (file == (void*)0 || !WriteFile(file, memory, 6, &count, (void*)0) || count != 6) {
        ExitProcess(2U);
    }
    CloseHandle(file);
    file = CreateFileA(path, 0x80000000, 0, (void*)0, 3, 0, (void*)0);
    if (file == (void*)0 || !ReadFile(file, memory, 64, &count, (void*)0)) {
        ExitProcess(3U);
    }
    void* output = GetStdHandle((dword_t)-11);
    if (!WriteFile(output, memory, count, &count, (void*)0)) {
        ExitProcess(4U);
    }
    CloseHandle(file);
    if (!VirtualFree(memory, 0, 0x8000)) {
        ExitProcess(5U);
    }
    SetLastError(0);
    CreateFileA("/absolute-not-supported", 0x80000000, 0, (void*)0, 3, 0, (void*)0);
    if (GetLastError() != 87) {
        ExitProcess(6U);
    }
    ExitProcess(0U);
}
