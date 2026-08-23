typedef unsigned long dword_t;
typedef int bool_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    static const char message[] = "installer-failed\n";
    dword_t written = 0;
    (void)WriteFile(GetStdHandle((dword_t)-11), message, sizeof(message) - 1, &written, (void*)0);
    ExitProcess(9U);
}
