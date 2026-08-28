typedef unsigned char byte_t;
typedef unsigned int dword_t;
typedef int bool_t;
typedef short int16_t;
typedef int int32_t;
typedef unsigned short uint16_t;
typedef unsigned long long uint64_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) void* GetCurrentProcess(void);

// KERNEL32
__attribute__((dllimport)) dword_t GetTickCount(void);
__attribute__((dllimport)) bool_t AllocConsole(void);
__attribute__((dllimport)) bool_t FreeConsole(void);
__attribute__((dllimport)) dword_t SetThreadExecutionState(dword_t flags);
__attribute__((dllimport)) bool_t IsDBCSLeadByte(byte_t test_char);
__attribute__((dllimport)) int32_t FoldStringW(dword_t flags, const uint16_t* src, int32_t src_len,
                                               uint16_t* dest, int32_t dest_len);

// USER32
__attribute__((dllimport)) bool_t SetUserObjectInformationW(void* obj, int32_t index, void* info, dword_t len);
__attribute__((dllimport)) dword_t WaitForInputIdle(void* proc, dword_t ms);
__attribute__((dllimport)) bool_t SetProcessDefaultLayout(dword_t layout);

// ADVAPI32
struct luid_t {
    dword_t low_part;
    int32_t high_part;
};
__attribute__((dllimport)) bool_t LookupPrivilegeValueW(const uint16_t* sys_name, const uint16_t* name, struct luid_t* pluid);
__attribute__((dllimport)) bool_t AdjustTokenPrivileges(void* tok, bool_t disable_all, void* new_state,
                                                        dword_t buf_len, void* prev_state, dword_t* ret_len);

// SHELL32
struct shfileinfo_t {
    void* hIcon;
    int32_t iIcon;
    dword_t dwAttributes;
    uint16_t szDisplayName[260];
    uint16_t szTypeName[80];
};
__attribute__((dllimport)) uint64_t SHGetFileInfoW(const uint16_t* path, dword_t attrs, struct shfileinfo_t* sfi,
                                                   dword_t cb_sfi, dword_t flags);
__attribute__((dllimport)) int32_t SHGetMalloc(void** pp_malloc);
__attribute__((dllimport)) void SHChangeNotify(int32_t event_id, dword_t flags, const void* item1, const void* item2);

// OLE32
__attribute__((dllimport)) int32_t CLSIDFromString(const uint16_t* lpsz, void* pclsid);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static void fail(const void* output, dword_t* bytes_written, dword_t code) {
    static const char message[] = "FAIL\n";
    (void)WriteFile(output, message, sizeof(message) - 1, bytes_written, (void*)0);
    ExitProcess(code);
}

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t bytes_written = 0;
    if (output == (void*)0 || output == (void*)(uint64_t)-1) {
        ExitProcess(1);
    }

    (void)GetTickCount();
    (void)AllocConsole();
    (void)FreeConsole();
    (void)SetThreadExecutionState(1);
    (void)IsDBCSLeadByte((byte_t)'A');

    static const uint16_t kFoldSrc[] = {'W', 'i', 'n', 'R', 'A', 'R', 0};
    uint16_t fold_dest[16] = {0};
    (void)FoldStringW(0, kFoldSrc, -1, fold_dest, 16);

    (void)SetUserObjectInformationW((void*)0, 0, (void*)0, 0);
    (void)WaitForInputIdle(GetCurrentProcess(), 0);
    (void)SetProcessDefaultLayout(0);

    static const uint16_t kPrivName[] = {'S', 'e', 'D', 'e', 'b', 'u', 'g', 0};
    struct luid_t luid;
    (void)LookupPrivilegeValueW((void*)0, kPrivName, &luid);
    (void)AdjustTokenPrivileges((void*)0, 0, (void*)0, 0, (void*)0, (void*)0);

    struct shfileinfo_t sfi;
    static const uint16_t kFilePath[] = {'C', ':', '\\', 't', 'e', 's', 't', 0};
    (void)SHGetFileInfoW(kFilePath, 0, &sfi, (dword_t)sizeof(sfi), 0);

    void* shell_malloc = (void*)0;
    (void)SHGetMalloc(&shell_malloc);
    (void)SHChangeNotify(0, 0, (void*)0, (void*)0);

    byte_t clsid_buf[16] = {0};
    (void)CLSIDFromString((void*)0, clsid_buf);

    static const char success[] = "winrarkernelshell\n";
    if (!WriteFile(output, success, sizeof(success) - 1, &bytes_written, (void*)0)) {
        ExitProcess(2);
    }

    ExitProcess(0);
}

