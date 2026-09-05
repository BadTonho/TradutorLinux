typedef unsigned long dword_t;
typedef int bool_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) int CloseHandle(const void* handle);
__attribute__((dllimport)) int CreateProcessW(const unsigned short* application_name,
                                               unsigned short* command_line,
                                               const void* process_attributes,
                                               const void* thread_attributes, int inherit_handles,
                                               dword_t creation_flags, const void* environment,
                                               const unsigned short* current_directory,
                                               void* startup_info, void* process_information);
__attribute__((dllimport)) dword_t WaitForSingleObject(const void* handle, dword_t milliseconds);
__attribute__((dllimport)) int GetExitCodeProcess(const void* process, dword_t* exit_code);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    static const unsigned short child_path[] = {
        'g', 'e', 'n', 'e', 'r', 'a', 't', 'e', 'd', '/',
        't', 'l', '_', 'p', 'r', 'o', 'c', 'e', 's', 's', '_', 'h', 'a', 'n', 'g',
        '.', 'e', 'x', 'e', 0};
    unsigned char startup_info[104] = {0};
    unsigned char process_info[24] = {0};
    if (!CreateProcessW(child_path, (unsigned short*)0, (void*)0, (void*)0, 0, 0,
                        (void*)0, (const unsigned short*)0, startup_info, process_info)) {
        ExitProcess(1U);
    }

    void* const child = *(void**)process_info;
    dword_t child_code = 0;
    if (child == (void*)0 || WaitForSingleObject(child, 0xFFFFFFFFU) != 0U ||
        !GetExitCodeProcess(child, &child_code) || child_code != 1U ||
        !CloseHandle(child)) {
        ExitProcess(2U);
    }

    static const char message[] = "process-limit-inherited\n";
    dword_t written = 0;
    if (!WriteFile(GetStdHandle((dword_t)-11), message, sizeof(message) - 1U,
                   &written, (void*)0) || written != sizeof(message) - 1U) {
        ExitProcess(3U);
    }
    ExitProcess(0U);
}
