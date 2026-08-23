typedef unsigned long dword_t;
typedef unsigned short wchar_t_guest;
typedef int bool_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) void* CreateFileW(const wchar_t_guest* path, dword_t desired_access,
                                              dword_t share_mode, const void* security_attributes,
                                              dword_t creation_disposition, dword_t flags,
                                              const void* template_file);
__attribute__((dllimport)) int CloseHandle(const void* handle);
__attribute__((dllimport)) dword_t GetEnvironmentVariableW(const wchar_t_guest* name,
                                                            wchar_t_guest* buffer, dword_t size);
__attribute__((dllimport)) dword_t GetCurrentDirectoryW(dword_t size, wchar_t_guest* buffer);
__attribute__((dllimport)) dword_t GetModuleFileNameW(const void* module, wchar_t_guest* buffer,
                                                       dword_t size);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static void fail(void* output, dword_t* written, dword_t code) {
    static const char message[] = "INSTALL-APP-FAIL\n";
    (void)WriteFile(output, message, sizeof(message) - 1, written, (void*)0);
    ExitProcess(code);
}

static int equals_wide(const wchar_t_guest* left, const wchar_t_guest* right) {
    while (*left != 0 && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t written = 0;
    static const wchar_t_guest local_app_data[] = {
        'L','O','C','A','L','A','P','P','D','A','T','A',0};
    static const wchar_t_guest state_path[] = {
        'C',':','\\','u','s','e','r','s','\\','g','u','e','s','t','\\','A','p','p','D','a','t','a','\\',
        'L','o','c','a','l','\\','t','l','-','i','n','s','t','a','l','l','-','s','t','a','t','e','.','t','x','t',0};
    static const wchar_t_guest expected_local_app_data[] = {
        'C',':','\\','u','s','e','r','s','\\','g','u','e','s','t','\\','A','p','p','D','a','t','a','\\','L','o','c','a','l',0};
    static const wchar_t_guest expected_current_directory[] = {
        'C',':','\\','P','r','o','g','r','a','m',' ','F','i','l','e','s','\\','T','L',' ','I','n','s','t','a','l','l',' ','F','i','x','t','u','r','e',0};
    wchar_t_guest environment[128] = {0};
    wchar_t_guest current_directory[260] = {0};
    wchar_t_guest module_name[260] = {0};
    if (GetEnvironmentVariableW(local_app_data, environment, 128) == 0 ||
        !equals_wide(environment, expected_local_app_data) ||
        GetCurrentDirectoryW(260, current_directory) == 0 ||
        !equals_wide(current_directory, expected_current_directory) ||
        GetModuleFileNameW((void*)0, module_name, 260) == 0 || module_name[0] != 'C') {
        fail(output, &written, 1U);
    }
    void* state = CreateFileW(state_path, 0x40000000U, 0, (void*)0, 2, 0, (void*)0);
    static const char payload[] = "installed\n";
    if (state == (void*)0 || !WriteFile(state, payload, sizeof(payload) - 1, &written, (void*)0) ||
        written != sizeof(payload) - 1 || !CloseHandle(state)) {
        fail(output, &written, 2U);
    }
    static const char message[] = "installed-app\n";
    if (!WriteFile(output, message, sizeof(message) - 1, &written, (void*)0)) {
        ExitProcess(3U);
    }
    ExitProcess(0U);
}
