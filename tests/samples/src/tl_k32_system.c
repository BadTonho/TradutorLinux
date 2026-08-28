typedef unsigned char byte_t;
typedef unsigned int dword_t;
typedef int bool_t;
typedef short int16_t;
typedef int int32_t;
typedef unsigned short uint16_t;
typedef unsigned long long uint64_t;
typedef unsigned long long size_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) dword_t GetLastError(void);

__attribute__((dllimport)) void OutputDebugStringA(const char* str);
__attribute__((dllimport)) void OutputDebugStringW(const uint16_t* str);
__attribute__((dllimport)) bool_t SetDllDirectoryW(const uint16_t* path);
__attribute__((dllimport)) dword_t GetTimeZoneInformation(void* tz_info);
__attribute__((dllimport)) dword_t GetProcessId(const void* process);
__attribute__((dllimport)) bool_t QueryFullProcessImageNameW(const void* process, dword_t flags,
                                                              uint16_t* exe_name, dword_t* size);
__attribute__((dllimport)) bool_t FileTimeToLocalFileTime(const void* file_time, void* local_file_time);
__attribute__((dllimport)) dword_t GetLongPathNameW(const uint16_t* short_path, uint16_t* long_path, dword_t len);
__attribute__((dllimport)) dword_t GetShortPathNameW(const uint16_t* long_path, uint16_t* short_path, dword_t len);
__attribute__((dllimport)) bool_t SetThreadPriority(const void* thread, int priority);
__attribute__((dllimport)) bool_t GetProcessAffinityMask(const void* process, uint64_t* proc_mask, uint64_t* sys_mask);

struct timezone_info_t {
    int32_t bias;
    uint16_t standard_name[32];
    uint16_t standard_date[8];
    int32_t standard_bias;
    uint16_t daylight_name[32];
    uint16_t daylight_date[8];
    int32_t daylight_bias;
};

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

    // 1. OutputDebugString
    OutputDebugStringA("debug test A\n");
    static const uint16_t wdebug[] = {'d', 'e', 'b', 'u', 'g', ' ', 'W', '\n', 0};
    OutputDebugStringW(wdebug);

    // 2. SetDllDirectoryW
    static const uint16_t dlldir[] = {'C', ':', '\\', 'b', 'i', 'n', 0};
    if (!SetDllDirectoryW(dlldir)) {
        fail(output, &bytes_written, 1);
    }
    (void)SetDllDirectoryW((const uint16_t*)0);

    // 3. GetTimeZoneInformation
    struct timezone_info_t tzi;
    dword_t tz_res = GetTimeZoneInformation(&tzi);
    if (tz_res == 0xFFFFFFFFU) {
        fail(output, &bytes_written, 2);
    }

    // 4. GetProcessId
    dword_t pid = GetProcessId((void*)0);
    if (pid == 0) {
        fail(output, &bytes_written, 3);
    }

    // 5. QueryFullProcessImageNameW
    uint16_t img_name[260];
    dword_t img_size = 260;
    if (!QueryFullProcessImageNameW((void*)0, 0, img_name, &img_size) || img_size == 0) {
        fail(output, &bytes_written, 4);
    }

    // 6. FileTimeToLocalFileTime
    uint64_t ft = 0x01D9E00000000000ULL;
    uint64_t lft = 0;
    if (!FileTimeToLocalFileTime(&ft, &lft) || lft == 0) {
        fail(output, &bytes_written, 5);
    }

    // 7. Path names
    uint16_t long_path[260];
    dword_t lp_len = GetLongPathNameW(dlldir, long_path, 260);
    if (lp_len == 0) {
        fail(output, &bytes_written, 6);
    }
    uint16_t short_path[260];
    dword_t sp_len = GetShortPathNameW(long_path, short_path, 260);
    if (sp_len == 0) {
        fail(output, &bytes_written, 7);
    }

    // 8. Thread priority & Affinity
    if (!SetThreadPriority((void*)0, 0)) {
        fail(output, &bytes_written, 8);
    }
    uint64_t proc_mask = 0, sys_mask = 0;
    if (!GetProcessAffinityMask((void*)0, &proc_mask, &sys_mask) || proc_mask == 0) {
        fail(output, &bytes_written, 9);
    }

    static const char result[] = "k32system\n";
    if (!WriteFile(output, result, sizeof(result) - 1, &bytes_written, (void*)0) ||
        bytes_written != sizeof(result) - 1) {
        ExitProcess(10);
    }
    ExitProcess(0);
}

