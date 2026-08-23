typedef unsigned long dword_t;
typedef unsigned short wchar_t_guest;
typedef int bool_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) int CreateDirectoryW(const wchar_t_guest* path,
                                                const void* security_attributes);
__attribute__((dllimport)) void* CreateFileW(const wchar_t_guest* path, dword_t desired_access,
                                              dword_t share_mode, const void* security_attributes,
                                              dword_t creation_disposition, dword_t flags,
                                              const void* template_file);
__attribute__((dllimport)) dword_t GetFileAttributesW(const wchar_t_guest* path);
__attribute__((dllimport)) int CopyFileW(const wchar_t_guest* from, const wchar_t_guest* to,
                                         int fail_if_exists);
__attribute__((dllimport)) int CreateProcessW(const wchar_t_guest* application_name,
                                              wchar_t_guest* command_line,
                                              const void* process_attributes,
                                              const void* thread_attributes, int inherit_handles,
                                              dword_t creation_flags, const void* environment,
                                              const wchar_t_guest* current_directory,
                                              void* startup_info, void* process_information);
__attribute__((dllimport)) dword_t WaitForSingleObject(const void* handle, dword_t milliseconds);
__attribute__((dllimport)) int GetExitCodeProcess(const void* process, dword_t* exit_code);
__attribute__((dllimport)) int CloseHandle(const void* handle);
__attribute__((dllimport)) dword_t GetModuleFileNameW(const void* module, wchar_t_guest* buffer,
                                                       dword_t size);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static void fail(void* output, dword_t* written, dword_t code) {
    static const char message[] = "INSTALL-SETUP-FAIL\n";
    (void)WriteFile(output, message, sizeof(message) - 1, written, (void*)0);
    ExitProcess(code);
}

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t written = 0;
    static const wchar_t_guest target_dir[] = {
        'C',':','\\','P','r','o','g','r','a','m',' ','F','i','l','e','s','\\','T','L',' ',
        'I','n','s','t','a','l','l',' ','F','i','x','t','u','r','e',0};
    static const wchar_t_guest source[] = {
        'C',':','\\','w','i','n','d','o','w','s','\\','t','e','m','p','\\','t','l','_','i','n',
        's','t','a','l','l','_','a','p','p','.','e','x','e',0};
    static const wchar_t_guest target[] = {
        'C',':','\\','P','r','o','g','r','a','m',' ','F','i','l','e','s','\\','T','L',' ',
        'I','n','s','t','a','l','l',' ','F','i','x','t','u','r','e','\\','t','l','_','i','n',
        's','t','a','l','l','_','a','p','p','.','e','x','e',0};
    unsigned char startup_info[104] = {0};
    unsigned char process_info[24] = {0};
    wchar_t_guest module_name[260] = {0};
    if (GetModuleFileNameW((void*)0, module_name, 260) == 0 || module_name[0] != 'Z') {
        fail(output, &written, 6U);
    }
    if (!CreateDirectoryW(target_dir, (void*)0) &&
        (GetFileAttributesW(target_dir) == 0xFFFFFFFFU ||
         (GetFileAttributesW(target_dir) & 0x10U) == 0U)) {
        fail(output, &written, 1U);
    }
    if (!CopyFileW(source, target, 0)) {
        fail(output, &written, 2U);
    }
    if (!CreateProcessW(target, (wchar_t_guest*)0, (void*)0, (void*)0, 0, 0, (void*)0,
                        target_dir, startup_info, process_info)) {
        fail(output, &written, 3U);
    }
    void* child = *(void**)process_info;
    dword_t child_code = 1;
    if (child == (void*)0 || WaitForSingleObject(child, 5000) != 0U ||
        !GetExitCodeProcess(child, &child_code) || child_code != 0U || !CloseHandle(child)) {
        fail(output, &written, 4U);
    }
    static const char message[] = "installer\n";
    if (!WriteFile(output, message, sizeof(message) - 1, &written, (void*)0)) {
        ExitProcess(5U);
    }
    ExitProcess(0U);
}
