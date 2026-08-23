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
__attribute__((dllimport)) dword_t GetFileAttributesW(const wchar_t_guest* path);
__attribute__((dllimport)) int CopyFileW(const wchar_t_guest* from, const wchar_t_guest* to,
                                         int fail_if_exists);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static void fail(void* output, dword_t* written, dword_t code) {
    static const char message[] = "INSTALL-MULTI-FAIL\n";
    (void)WriteFile(output, message, sizeof(message) - 1, written, (void*)0);
    ExitProcess(code);
}

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t written = 0;
    static const wchar_t_guest target_dir[] = {
        'C',':','\\','P','r','o','g','r','a','m',' ','F','i','l','e','s','\\','T','L',' ','M','u','l','t','i',0};
    static const wchar_t_guest source[] = {
        'C',':','\\','w','i','n','d','o','w','s','\\','t','e','m','p','\\','t','l','_','i','n','s','t','a','l','l',
        '_','a','p','p','.','e','x','e',0};
    static const wchar_t_guest first[] = {
        'C',':','\\','P','r','o','g','r','a','m',' ','F','i','l','e','s','\\','T','L',' ','M','u','l','t','i','\\',
        'f','i','r','s','t','.','e','x','e',0};
    static const wchar_t_guest second[] = {
        'C',':','\\','P','r','o','g','r','a','m',' ','F','i','l','e','s','\\','T','L',' ','M','u','l','t','i','\\',
        's','e','c','o','n','d','.','e','x','e',0};
    if (!CreateDirectoryW(target_dir, (void*)0) &&
        (GetFileAttributesW(target_dir) == 0xFFFFFFFFU ||
         (GetFileAttributesW(target_dir) & 0x10U) == 0U)) {
        fail(output, &written, 1U);
    }
    if (!CopyFileW(source, first, 0) || !CopyFileW(source, second, 0)) {
        fail(output, &written, 2U);
    }
    static const char message[] = "installer-multi\n";
    (void)WriteFile(output, message, sizeof(message) - 1, &written, (void*)0);
    ExitProcess(0U);
}
