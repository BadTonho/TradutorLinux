typedef unsigned long dword_t;
typedef unsigned long long size_t_guest;
typedef int bool_t;
typedef unsigned short wchar16_t;

typedef struct cpinfo_t {
    dword_t max_char_size;
    unsigned char default_char[2];
    unsigned char lead_byte[12];
} cpinfo_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport, noreturn)) void ExitThread(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(void* handle, const void* buffer,
                                            dword_t bytes_to_write,
                                            dword_t* bytes_written, void* overlapped);
__attribute__((dllimport)) bool_t SetEnvironmentVariableW(const wchar16_t* name,
                                                           const wchar16_t* value);
__attribute__((dllimport)) dword_t GetEnvironmentVariableW(const wchar16_t* name,
                                                            wchar16_t* value, dword_t size);
__attribute__((dllimport)) wchar16_t* GetEnvironmentStringsW(void);
__attribute__((dllimport)) bool_t FreeEnvironmentStringsW(wchar16_t* block);
__attribute__((dllimport)) dword_t ExpandEnvironmentStringsW(const wchar16_t* source,
                                                              wchar16_t* destination,
                                                              dword_t size);
__attribute__((dllimport)) dword_t GetACP(void);
__attribute__((dllimport)) dword_t GetOEMCP(void);
__attribute__((dllimport)) bool_t GetCPInfo(dword_t code_page, cpinfo_t* info);
__attribute__((dllimport)) int MultiByteToWideChar(dword_t code_page, dword_t flags,
                                                    const char* source, int source_count,
                                                    wchar16_t* destination, int destination_count);
__attribute__((dllimport)) int WideCharToMultiByte(dword_t code_page, dword_t flags,
                                                    const wchar16_t* source, int source_count,
                                                    char* destination, int destination_count,
                                                    const char* default_char,
                                                    bool_t* used_default);
__attribute__((dllimport)) int GetLocaleInfoW(dword_t locale, dword_t locale_type,
                                               wchar16_t* data, int data_count);
__attribute__((dllimport)) int LCMapStringW(dword_t locale, dword_t flags,
                                             const wchar16_t* source, int source_count,
                                             wchar16_t* destination, int destination_count);
__attribute__((dllimport)) int LCMapStringEx(const wchar16_t* locale_name, dword_t flags,
                                              const wchar16_t* source, int source_count,
                                              wchar16_t* destination, int destination_count,
                                              const void* version, void* reserved,
                                              size_t_guest sort_handle);
__attribute__((dllimport)) dword_t FlsAlloc(void* callback);
__attribute__((dllimport)) bool_t FlsFree(dword_t index);
__attribute__((dllimport)) void* FlsGetValue(dword_t index);
__attribute__((dllimport)) bool_t FlsSetValue(dword_t index, void* value);
__attribute__((dllimport)) void* CreateThread(void* attributes, size_t_guest stack_size,
                                              void* start, void* parameter,
                                              dword_t flags, dword_t* thread_id);
__attribute__((dllimport)) dword_t WaitForSingleObject(void* handle, dword_t milliseconds);
__attribute__((dllimport)) bool_t CloseHandle(void* handle);

static volatile dword_t g_callback_count;
static dword_t g_fls_index;
static dword_t g_main_value;
static dword_t g_thread_value;

static int equals_wide(const wchar16_t* left, const wchar16_t* right) {
    while (*left != 0 || *right != 0) {
        if (*left++ != *right++) return 0;
    }
    return 1;
}

static int block_has_phase(const wchar16_t* block) {
    static const wchar16_t wanted[] = {'t','l','_','p','h','a','s','e','=','a','l','p','h','a',0};
    while (*block != 0) {
        if (equals_wide(block, wanted)) return 1;
        while (*block++ != 0) {}
    }
    return 0;
}

void fls_callback(void* value) {
    if (value == &g_main_value || value == &g_thread_value) {
        ++g_callback_count;
    }
}

void fls_thread(void* ignored) {
    (void)ignored;
    if (FlsGetValue(g_fls_index) != (void*)0 ||
        !FlsSetValue(g_fls_index, &g_thread_value)) {
        ExitThread(1U);
    }
    ExitThread(0U);
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    static const wchar16_t phase_name[] = {'t','l','_','p','h','a','s','e',0};
    static const wchar16_t phase_name_upper[] = {'T','L','_','P','H','A','S','E',0};
    static const wchar16_t phase_value[] = {'a','l','p','h','a',0};
    static const wchar16_t expansion[] = {'%','T','L','_','P','H','A','S','E','%','!',0};
    static const wchar16_t expected_expansion[] = {'a','l','p','h','a','!',0};
    static const wchar16_t en_us[] = {'e','n','-','U','S',0};
    static const wchar16_t accent_upper[] = {'A',0x00C9,0};
    static const wchar16_t accent_lower[] = {'a',0x00E9,0};
    wchar16_t buffer[64] = {0};
    wchar16_t mapped[8] = {0};
    char cp437_in[] = {(char)0x82, 0};
    char cp437_out[4] = {0};
    cpinfo_t info = {0};
    void* stdout_handle;
    dword_t written = 0;
    dword_t failure_stage = 0;

    if (!SetEnvironmentVariableW(phase_name, phase_value) ||
        GetEnvironmentVariableW(phase_name_upper, buffer, 64U) != 5U ||
        !equals_wide(buffer, phase_value) ||
        ExpandEnvironmentStringsW(expansion, buffer, 64U) != 7U ||
        !equals_wide(buffer, expected_expansion)) failure_stage = 1U;
    wchar16_t* block = GetEnvironmentStringsW();
    if (block == (void*)0 || !block_has_phase(block) || !FreeEnvironmentStringsW(block)) failure_stage = 2U;

    if (GetACP() != 1252U || GetOEMCP() != 437U || !GetCPInfo(437U, &info) ||
        info.max_char_size != 1U || !GetCPInfo(65001U, &info) || info.max_char_size != 4U ||
        MultiByteToWideChar(437U, 0, cp437_in, -1, buffer, 64) != 2 || buffer[0] != 0x00E9U ||
        WideCharToMultiByte(437U, 0, buffer, -1, cp437_out, 4, (void*)0, (void*)0) != 2 ||
        (unsigned char)cp437_out[0] != 0x82U) failure_stage = 3U;

    if (GetLocaleInfoW(0x0409U, 0x0000000BU, buffer, 64) != 5 ||
        !equals_wide(buffer, (const wchar16_t[]){'1','2','5','2',0}) ||
        LCMapStringW(0x0409U, 0x00000100U, accent_upper, -1, mapped, 8) != 3 ||
        !equals_wide(mapped, accent_lower) ||
        LCMapStringEx(en_us, 0x00000200U, accent_lower, -1, mapped, 8,
                      (void*)0, (void*)0, 0) != 3 ||
        !equals_wide(mapped, accent_upper)) failure_stage = 4U;

    g_fls_index = FlsAlloc((void*)fls_callback);
    if (g_fls_index == 0xFFFFFFFFU || !FlsSetValue(g_fls_index, &g_main_value)) failure_stage = 5U;
    void* thread = CreateThread((void*)0, 0, (void*)fls_thread, (void*)0, 0, (void*)0);
    if (thread == (void*)0 || WaitForSingleObject(thread, 0xFFFFFFFFU) != 0U ||
        !CloseHandle(thread) || g_callback_count != 1U || !FlsFree(g_fls_index) ||
        g_callback_count != 2U) failure_stage = 6U;

    stdout_handle = GetStdHandle((dword_t)-11);
    if (failure_stage == 0U) {
        static const char message[] = "locale-env-fls\n";
        WriteFile(stdout_handle, message, sizeof(message) - 1U, &written, (void*)0);
        ExitProcess(0U);
    }
    static const char failure[] = "locale-env-fls-fail\n";
    WriteFile(stdout_handle, failure, sizeof(failure) - 1U, &written, (void*)0);
    ExitProcess(failure_stage);
}
