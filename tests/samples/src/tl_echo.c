typedef unsigned long dword_t;
typedef int bool_t;

__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t ReadFile(void* handle, void* buffer, dword_t bytes_to_read,
                                            dword_t* bytes_read, void* overlapped);
__attribute__((dllimport)) bool_t WriteFile(void* handle, const void* buffer, dword_t bytes_to_write,
                                             dword_t* bytes_written, void* overlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    char buffer[64];
    dword_t bytes_read = 0;
    dword_t bytes_written = 0;
    void* input = GetStdHandle((dword_t)-10);
    void* output = GetStdHandle((dword_t)-11);
    if (!ReadFile(input, buffer, sizeof(buffer), &bytes_read, (void*)0)) {
        ExitProcess(1U);
    }
    if (!WriteFile(output, buffer, bytes_read, &bytes_written, (void*)0) ||
        bytes_written != bytes_read) {
        ExitProcess(2U);
    }
    ExitProcess(0U);
}
