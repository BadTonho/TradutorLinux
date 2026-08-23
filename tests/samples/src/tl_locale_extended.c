typedef unsigned long dword_t;
typedef unsigned long long size_t_guest;
typedef unsigned short wchar16_t;
typedef int bool_t;

typedef struct system_time_t {
    unsigned short year;
    unsigned short month;
    unsigned short day_of_week;
    unsigned short day;
    unsigned short hour;
    unsigned short minute;
    unsigned short second;
    unsigned short milliseconds;
} system_time_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(void* handle, const void* buffer,
                                            dword_t bytes_to_write,
                                            dword_t* bytes_written, void* overlapped);
__attribute__((dllimport)) bool_t IsValidCodePage(dword_t code_page);
__attribute__((dllimport)) bool_t IsValidLocale(dword_t locale, dword_t flags);
__attribute__((dllimport)) int GetLocaleInfoEx(const wchar16_t* locale_name,
                                                dword_t locale_type,
                                                wchar16_t* data, int data_count);
__attribute__((dllimport)) bool_t EnumSystemLocalesW(void* callback, dword_t flags);
__attribute__((dllimport)) bool_t GetStringTypeW(dword_t info_type,
                                                  const wchar16_t* source,
                                                  int source_count,
                                                  unsigned short* char_type);
__attribute__((dllimport)) int GetDateFormatW(dword_t locale, dword_t flags,
                                              const system_time_t* date,
                                              const wchar16_t* format,
                                              wchar16_t* data, int data_count);
__attribute__((dllimport)) int GetTimeFormatW(dword_t locale, dword_t flags,
                                              const system_time_t* time,
                                              const wchar16_t* format,
                                              wchar16_t* data, int data_count);

static volatile dword_t g_enum_count;

static int equals_wide(const wchar16_t* left, const wchar16_t* right) {
    while (*left != 0 || *right != 0) {
        if (*left++ != *right++) return 0;
    }
    return 1;
}

__attribute__((ms_abi)) int locale_callback(wchar16_t* locale) {
    static const wchar16_t expected[] = {'0', '4', '0', '9', 0};
    if (!equals_wide(locale, expected)) return 0;
    ++g_enum_count;
    return 1;
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    static const wchar16_t en_us[] = {'e', 'n', '-', 'U', 'S', 0};
    static const wchar16_t united_states[] = {
        'U','n','i','t','e','d',' ','S','t','a','t','e','s',0};
    static const wchar16_t string_source[] = {'A', '7', ' ', 0x00E9, 0};
    static const wchar16_t short_date[] = {'1','/','2','/','2','0','2','4',0};
    static const wchar16_t time_pm[] = {'3',':','0','4',':','0','5',' ','P','M',0};
    system_time_t sample = {2024, 1, 2, 2, 15, 4, 5, 0};
    wchar16_t buffer[64] = {0};
    unsigned short types[5] = {0};
    dword_t written = 0;
    dword_t failure_stage = 0;

    if (!IsValidCodePage(1252U) || IsValidCodePage(932U) ||
        !IsValidLocale(0x0409U, 2U) ||
        GetLocaleInfoEx(en_us, 0x00000006U, buffer, 64) != 14 ||
        !equals_wide(buffer, united_states)) failure_stage = 1U;
    if (!EnumSystemLocalesW((void*)locale_callback, 2U) || g_enum_count != 1U) failure_stage = 2U;
    if (!GetStringTypeW(1U, string_source, -1, types) ||
        types[0] != 0x0181U || types[1] != 0x0084U || types[2] != 0x0048U ||
        types[3] != 0x0102U) failure_stage = 3U;
    if (GetDateFormatW(0x0409U, 0U, &sample, (void*)0, buffer, 64) != 9 ||
        !equals_wide(buffer, short_date) ||
        GetTimeFormatW(0x0409U, 0U, &sample, (void*)0, buffer, 64) != 11 ||
        !equals_wide(buffer, time_pm)) failure_stage = 4U;

    void* stdout_handle = GetStdHandle((dword_t)-11);
    if (failure_stage == 0U) {
        static const char message[] = "locale-extended\n";
        WriteFile(stdout_handle, message, sizeof(message) - 1U, &written, (void*)0);
        ExitProcess(0U);
    }
    static const char failure[] = "locale-extended-fail\n";
    WriteFile(stdout_handle, failure, sizeof(failure) - 1U, &written, (void*)0);
    ExitProcess(failure_stage);
}
