typedef unsigned long dword_t;
typedef int bool_t;

__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    static const char message[] = "Proton probe\n";
    dword_t written = 0;
    void* output = GetStdHandle((dword_t)-11);
    if (output == (void*)-1 ||
        !WriteFile(output, message, sizeof(message) - 1U, &written, (void*)0) ||
        written != sizeof(message) - 1U) {
        ExitProcess(1U);
    }
    ExitProcess(0U);
}
