typedef unsigned long dword_t;
typedef unsigned long long size_t_guest;
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

typedef struct cpinfo_t {
    dword_t max_char_size;
    unsigned char default_char[2];
    unsigned char lead_byte[12];
} cpinfo_t;

typedef struct slist_header_t {
    unsigned long long alignment;
    unsigned long long region;
} __attribute__((aligned(16))) slist_header_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) dword_t GetFileType(const void* handle);
__attribute__((dllimport)) void GetStartupInfoW(startup_info_w_t* startup_info);
__attribute__((dllimport)) dword_t GetACP(void);
__attribute__((dllimport)) dword_t GetOEMCP(void);
__attribute__((dllimport)) bool_t GetCPInfo(dword_t code_page, cpinfo_t* info);
__attribute__((dllimport)) bool_t IsValidCodePage(dword_t code_page);
__attribute__((dllimport)) bool_t GetStringTypeW(dword_t info_type,
                                                  const wchar16_t* source,
                                                  int source_count,
                                                  unsigned short* char_type);
__attribute__((dllimport)) void InitializeSListHead(slist_header_t* list_head);
__attribute__((dllimport)) dword_t FlsAlloc(void* callback);
__attribute__((dllimport)) void* FlsGetValue(dword_t index);
__attribute__((dllimport)) bool_t FlsSetValue(dword_t index, void* value);
__attribute__((dllimport)) bool_t FlsFree(dword_t index);
__attribute__((dllimport)) wchar16_t* GetEnvironmentStringsW(void);
__attribute__((dllimport)) bool_t FreeEnvironmentStringsW(wchar16_t* block);
__attribute__((dllimport)) void* GetProcessHeap(void);
__attribute__((dllimport)) void* HeapAlloc(void* heap, dword_t flags, size_t_guest size);
__attribute__((dllimport)) void* HeapReAlloc(void* heap, dword_t flags, void* memory,
                                             size_t_guest size);
__attribute__((dllimport)) size_t_guest HeapSize(void* heap, dword_t flags,
                                                  const void* memory);
__attribute__((dllimport)) bool_t HeapFree(void* heap, dword_t flags, void* memory);
__attribute__((dllimport)) wchar16_t* lstrcpyW(wchar16_t* destination,
                                               const wchar16_t* source);
__attribute__((dllimport)) wchar16_t* lstrcpynW(wchar16_t* destination,
                                                const wchar16_t* source,
                                                int max_length);
__attribute__((dllimport)) int lstrcmpW(const wchar16_t* left, const wchar16_t* right);
__attribute__((dllimport)) int lstrcmpiW(const wchar16_t* left, const wchar16_t* right);
__attribute__((dllimport)) long SHGetFolderPathW(void* hwnd, int csidl, void* token,
                                                  dword_t flags, wchar16_t* path);
__attribute__((dllimport)) bool_t WriteFile(void* handle, const void* buffer,
                                            dword_t bytes_to_write,
                                            dword_t* bytes_written, void* overlapped);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static void fail(dword_t stage) {
    ExitProcess(stage);
}

void tl_entry(void) {
    startup_info_w_t startup = {0};
    cpinfo_t cpinfo = {0};
    slist_header_t list_head = {~0ULL, ~0ULL};
    unsigned short char_type[2] = {0, 0};
    static const wchar16_t source[] = {'A', 0};
    static unsigned char fls_value = 0x5A;
    void* const input = GetStdHandle((dword_t)-10);
    void* const output = GetStdHandle((dword_t)-11);
    void* const error = GetStdHandle((dword_t)-12);
    if (input == (void*)0 || output == (void*)0 || error == (void*)0) fail(1U);

    GetStartupInfoW(&startup);
    if (startup.cb != sizeof(startup) || startup.std_input != input ||
        startup.std_output != output || startup.std_error != error) fail(2U);
    if (GetFileType(input) == 0U || GetFileType(output) == 0U || GetFileType(error) == 0U) {
        fail(3U);
    }

    if (GetACP() != 1252U || GetOEMCP() != 437U || !IsValidCodePage(1252U) ||
        !GetCPInfo(1252U, &cpinfo) || cpinfo.max_char_size != 1U ||
        !GetStringTypeW(1U, source, -1, char_type) || char_type[0] == 0U) {
        fail(4U);
    }
    InitializeSListHead(&list_head);
    if (list_head.alignment != 0U || list_head.region != 0U) fail(5U);

    const dword_t fls_index = FlsAlloc((void*)0);
    if (fls_index == 0xFFFFFFFFU || !FlsSetValue(fls_index, &fls_value) ||
        FlsGetValue(fls_index) != &fls_value || !FlsFree(fls_index)) {
        fail(6U);
    }

    void* const heap = GetProcessHeap();
    wchar16_t* const environment = GetEnvironmentStringsW();
    if (heap == (void*)0 || environment == (wchar16_t*)0) fail(7U);
    size_t_guest environment_units = 0U;
    for (;;) {
        if (environment[environment_units] == 0U &&
            environment[environment_units + 1U] == 0U) {
            environment_units += 2U;
            break;
        }
        ++environment_units;
    }
    wchar16_t* const environment_copy =
        (wchar16_t*)HeapAlloc(heap, 0U, environment_units * sizeof(wchar16_t));
    if (environment_copy == (wchar16_t*)0) fail(8U);
    for (size_t_guest index = 0U; index < environment_units; ++index) {
        environment_copy[index] = environment[index];
    }
    if (!FreeEnvironmentStringsW(environment)) fail(9U);

    GetStartupInfoW(&startup);
    if (startup.cb != sizeof(startup)) fail(10U);
    static const wchar16_t copy_source[] = {'C', ':', '\\', 'T', 'e', 'm', 'p', 0};
    static const wchar16_t compare_source[] = {'c', ':', '\\', 't', 'e', 'm', 'p', 0};
    wchar16_t copied[32] = {0};
    wchar16_t bounded[32] = {0};
    if (lstrcpyW(copied, copy_source) != copied) fail(11U);
    if (lstrcpynW(bounded, copy_source, 32) != bounded) fail(12U);
    if (lstrcmpW(copied, copy_source) != 0) fail(13U);
    const int insensitive_result = lstrcmpiW(copied, compare_source);
    if (insensitive_result != 0) fail(insensitive_result < 0 ? 14U : 15U);
    unsigned char* first = (unsigned char*)HeapAlloc(heap, 0U, 512U);
    unsigned char* const second = (unsigned char*)HeapAlloc(heap, 0U, 4096U);
    if (first == (unsigned char*)0 || second == (unsigned char*)0) fail(16U);
    for (size_t_guest index = 0U; index < 512U; ++index) first[index] = (unsigned char)index;
    for (size_t_guest index = 0U; index < 4096U; ++index) {
        second[index] = (unsigned char)(index ^ 0x5AU);
    }
    if (HeapSize(heap, 0U, first) < 512U) fail(17U);
    first = (unsigned char*)HeapReAlloc(heap, 0U, first, 1024U);
    if (first == (unsigned char*)0 || first[0] != 0U ||
        HeapSize(heap, 0U, first) < 1024U) fail(18U);

    wchar16_t path[260] = {0};
    if (SHGetFolderPathW((void*)0, 0x1A, (void*)0, 0U, path) != 0 || path[0] == 0) fail(19U);
    if (!HeapFree(heap, 0U, first) || !HeapFree(heap, 0U, second) ||
        !HeapFree(heap, 0U, environment_copy)) {
        fail(20U);
    }

    static const char message[] = "notepad-startup-probe\n";
    dword_t written = 0U;
    if (!WriteFile(output, message, (dword_t)(sizeof(message) - 1U), &written, (void*)0) ||
        written != (dword_t)(sizeof(message) - 1U)) {
        fail(21U);
    }
    ExitProcess(0U);
}
