typedef unsigned long dword_t;
typedef int bool_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(void* handle, const void* buffer, dword_t bytes_to_write,
                                             dword_t* bytes_written, void* overlapped);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    static const char message[] = "Ola do Windows no Linux!\r\n";
    dword_t bytes_written = 0;
    const dword_t message_length = (dword_t)(sizeof(message) - 1U);
    const void* const standard_output = GetStdHandle((dword_t)-11);

    (void)WriteFile((void*)standard_output, message, message_length, &bytes_written, (void*)0);
    ExitProcess(0U);
}
