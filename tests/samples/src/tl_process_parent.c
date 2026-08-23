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
__attribute__((dllimport)) int TerminateProcess(const void* process, dword_t exit_code);
__attribute__((dllimport)) int SetEnvironmentVariableW(const unsigned short* name,
                                                        const unsigned short* value);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static void fail(void* output, dword_t* written, dword_t code) {
    static const char message[] = "FAIL\n";
    (void)WriteFile(output, message, sizeof(message) - 1, written, (void*)0);
    ExitProcess(code);
}

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t written = 0;
    static const unsigned short child_path[] = {
        'g', 'e', 'n', 'e', 'r', 'a', 't', 'e', 'd', '/',
        't', 'l', '_', 'p', 'r', 'o', 'c', 'e', 's', 's', '_', 'c', 'h', 'i', 'l', 'd',
        '.', 'e', 'x', 'e', 0};
    static const unsigned short hang_path[] = {
        'g', 'e', 'n', 'e', 'r', 'a', 't', 'e', 'd', '/',
        't', 'l', '_', 'p', 'r', 'o', 'c', 'e', 's', 's', '_', 'h', 'a', 'n', 'g',
        '.', 'e', 'x', 'e', 0};
    unsigned char startup_info[104] = {0};
    unsigned char process_info[24] = {0};
    static const unsigned short environment_name[] = {
        'T','L','_','P','R','O','C','E','S','S','_','E','N','V',0};
    static const unsigned short environment_value[] = {'c','h','i','l','d',0};

    if (!SetEnvironmentVariableW(environment_name, environment_value)) {
        fail(output, &written, 6U);
    }

    if (!CreateProcessW(child_path, (unsigned short*)0, (void*)0, (void*)0, 0, 0, (void*)0,
                        (const unsigned short*)0, startup_info, process_info)) {
        fail(output, &written, 1U);
    }
    void* child_process = *(void**)process_info;
    dword_t child_code = 0;
    if (child_process == (void*)0 || WaitForSingleObject(child_process, 5000) != 0U ||
        !GetExitCodeProcess(child_process, &child_code) || child_code != 7U ||
        !CloseHandle(child_process)) {
        fail(output, &written, 2U);
    }

    if (!CreateProcessW(hang_path, (unsigned short*)0, (void*)0, (void*)0, 0, 0, (void*)0,
                        (const unsigned short*)0, startup_info, process_info)) {
        fail(output, &written, 3U);
    }
    void* hanging_process = *(void**)process_info;
    if (hanging_process == (void*)0 || !TerminateProcess(hanging_process, 9U) ||
        WaitForSingleObject(hanging_process, 5000) != 0U ||
        !GetExitCodeProcess(hanging_process, &child_code) || child_code != 9U ||
        !CloseHandle(hanging_process)) {
        fail(output, &written, 4U);
    }

    static const char message[] = "parent\n";
    if (!WriteFile(output, message, sizeof(message) - 1, &written, (void*)0)) {
        ExitProcess(5U);
    }
    ExitProcess(0U);
}
