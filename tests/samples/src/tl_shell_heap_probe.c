typedef unsigned long dword_t;
typedef unsigned short wchar16_t;
typedef int bool_t;

typedef struct startup_info_w_t {
    dword_t cb;
    dword_t padding;
    wchar16_t* reserved;
    wchar16_t* desktop;
    wchar16_t* title;
    dword_t x;
    dword_t y;
    dword_t x_size;
    dword_t y_size;
    dword_t x_count_chars;
    dword_t y_count_chars;
    dword_t fill_attribute;
    dword_t flags;
    unsigned short show_window;
    unsigned short reserved2_size;
    unsigned char* reserved2;
    void* std_input;
    void* std_output;
    void* std_error;
} startup_info_w_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(void* handle, const void* buffer,
                                             dword_t bytes_to_write,
                                             dword_t* bytes_written, void* overlapped);
__attribute__((dllimport)) void GetStartupInfoW(startup_info_w_t* startup_info);
__attribute__((dllimport)) wchar16_t* GetEnvironmentStringsW(void);
__attribute__((dllimport)) bool_t FreeEnvironmentStringsW(wchar16_t* block);
__attribute__((dllimport)) void* GetProcessHeap(void);
__attribute__((dllimport)) void* HeapAlloc(void* heap, dword_t flags, unsigned long long size);
__attribute__((dllimport)) bool_t HeapFree(void* heap, dword_t flags, void* memory);
__attribute__((dllimport)) long SHGetFolderPathW(void* hwnd, int csidl, void* token,
                                                  dword_t flags, wchar16_t* path);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static void fail(void) {
    ExitProcess(10U);
}

void tl_entry(void) {
    startup_info_w_t startup = {0};
    GetStartupInfoW(&startup);
    if (startup.cb == 0U) fail();

    wchar16_t* const environment = GetEnvironmentStringsW();
    if (environment == (wchar16_t*)0 || !FreeEnvironmentStringsW(environment)) fail();

    GetStartupInfoW(&startup);
    if (startup.cb == 0U) fail();

    void* const heap = GetProcessHeap();
    unsigned char* const first = (unsigned char*)HeapAlloc(heap, 0U, 64U);
    unsigned char* const second = (unsigned char*)HeapAlloc(heap, 0U, 4096U);
    if (first == (unsigned char*)0 || second == (unsigned char*)0) fail();
    for (unsigned long long index = 0; index < 64U; ++index) first[index] = (unsigned char)index;
    for (unsigned long long index = 0; index < 4096U; ++index) second[index] = (unsigned char)(index ^ 0x5AU);

    wchar16_t path[260] = {0};
    if (SHGetFolderPathW((void*)0, 0x1A, (void*)0, 0U, path) != 0 || path[0] == 0) fail();

    if (!HeapFree(heap, 0U, first) || !HeapFree(heap, 0U, second)) fail();

    wchar16_t second_path[260] = {0};
    if (SHGetFolderPathW((void*)0, 0x1A, (void*)0, 0U, second_path) != 0 ||
        second_path[0] == 0) {
        fail();
    }

    static const char message[] = "shell-heap-probe\n";
    dword_t written = 0;
    void* const output = GetStdHandle((dword_t)-11);
    if (!WriteFile(output, message, (dword_t)(sizeof(message) - 1U), &written, (void*)0) ||
        written != (dword_t)(sizeof(message) - 1U)) {
        fail();
    }
    ExitProcess(0U);
}
