typedef unsigned long dword_t;
typedef unsigned long long uintptr_guest_t;
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

typedef struct __attribute__((aligned(16))) slist_header_t {
    unsigned long long alignment;
    unsigned long long region;
} slist_header_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t SetStdHandle(dword_t standard_handle, void* handle);
__attribute__((dllimport)) dword_t GetFileType(const void* handle);
__attribute__((dllimport)) dword_t GetSystemDirectoryW(wchar16_t* buffer, dword_t size);
__attribute__((dllimport)) void GetStartupInfoW(startup_info_w_t* startup_info);
__attribute__((dllimport)) bool_t ReadConsoleW(const void* input, wchar16_t* buffer,
                                               dword_t chars_to_read,
                                               dword_t* chars_read,
                                               const void* input_control);
__attribute__((dllimport)) bool_t WriteConsoleW(const void* output,
                                                const wchar16_t* buffer,
                                                dword_t chars_to_write,
                                                dword_t* chars_written,
                                                const void* reserved);
__attribute__((dllimport)) bool_t IsDebuggerPresent(void);
__attribute__((dllimport)) bool_t IsProcessorFeaturePresent(dword_t feature);
__attribute__((dllimport)) void* EncodePointer(void* pointer);
__attribute__((dllimport)) void* DecodePointer(void* pointer);
__attribute__((dllimport)) void InitializeSListHead(slist_header_t* list_head);

static int equals_wide(const wchar16_t* left, const wchar16_t* right) {
    while (*left != 0 || *right != 0) {
        if (*left++ != *right++) return 0;
    }
    return 1;
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    static const wchar16_t expected_system[] = {
        'C',':','\\','W','i','n','d','o','w','s','\\','S','y','s','t','e','m','3','2',0};
    static const wchar16_t message[] = {
        'p','r','o','c','e','s','s','-','c','o','n','s','o','l','e','-',0x00E9,'\n'};
    startup_info_w_t startup = {0};
    slist_header_t list_head = {~0ULL, ~0ULL};
    wchar16_t system_directory[64] = {0};
    wchar16_t input_buffer[32] = {0};
    dword_t chars_read = 0;
    dword_t chars_written = 0;
    dword_t failure_stage = 0;

    void* input = GetStdHandle((dword_t)-10);
    void* output = GetStdHandle((dword_t)-11);
    void* error = GetStdHandle((dword_t)-12);
    if (input == (void*)0 || output == (void*)0 || error == (void*)0) failure_stage = 1U;

    GetStartupInfoW(&startup);
    if (startup.cb != sizeof(startup) || (startup.flags & 0x100U) == 0U ||
        startup.std_input != input || startup.std_output != output || startup.std_error != error) {
        failure_stage = 2U;
    }

    if (GetSystemDirectoryW((void*)0, 0) != 20U ||
        GetSystemDirectoryW(system_directory, 64) != 19U ||
        !equals_wide(system_directory, expected_system)) failure_stage = 3U;

    dword_t output_type = GetFileType(output);
    if (output_type != 2U && output_type != 3U && output_type != 1U) failure_stage = 4U;
    if (!SetStdHandle((dword_t)-11, error) || GetStdHandle((dword_t)-11) != error ||
        !SetStdHandle((dword_t)-11, output) || GetStdHandle((dword_t)-11) != output) {
        failure_stage = 5U;
    }

    if (IsDebuggerPresent() || !IsProcessorFeaturePresent(10U) ||
        IsProcessorFeaturePresent(0xFFFFFFFFU)) failure_stage = 6U;

    void* original = (void*)(uintptr_guest_t)0x12345678ULL;
    void* encoded = EncodePointer(original);
    if (DecodePointer(encoded) != original) failure_stage = 7U;

    InitializeSListHead(&list_head);
    if (list_head.alignment != 0 || list_head.region != 0) failure_stage = 8U;

    if (!ReadConsoleW(input, input_buffer, 32U, &chars_read, (void*)0) ||
        chars_read < 2U || input_buffer[0] != 'e' || input_buffer[1] != 'n') {
        failure_stage = 9U;
    }

    if (failure_stage != 0U) ExitProcess(failure_stage);
    if (!WriteConsoleW(output, message, (dword_t)(sizeof(message) / sizeof(message[0])),
                       &chars_written, (void*)0) ||
        chars_written != (dword_t)(sizeof(message) / sizeof(message[0]))) {
        ExitProcess(10U);
    }
    ExitProcess(0U);
}
