typedef unsigned long dword_t;
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
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    static const char path[] =
        "C:\\Program Files\\Compat Fixture\\injected.dat";
    static const char replacement[] = "guest modification\n";
    char payload[128] = {0};
    dword_t count = 0;

    void* file = CreateFileA(path, 0x80000000, 0, (void*)0, 3, 0, (void*)0);
    if (file == (void*)-1 || !ReadFile(file, payload, sizeof(payload), &count, (void*)0)) {
        ExitProcess(1U);
    }
    CloseHandle(file);

    void* output = GetStdHandle((dword_t)-11);
    if (!WriteFile(output, payload, count, &count, (void*)0)) {
        ExitProcess(2U);
    }

    file = CreateFileA(path, 0x40000000, 0, (void*)0, 3, 0, (void*)0);
    if (file == (void*)-1 ||
        !WriteFile(file, replacement, sizeof(replacement) - 1U, &count, (void*)0) ||
        count != sizeof(replacement) - 1U) {
        ExitProcess(3U);
    }
    CloseHandle(file);
    ExitProcess(0U);
}
