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
__attribute__((dllimport)) dword_t GetVersion(void);
__attribute__((dllimport)) uint64_t GetLargePageMinimum(void);
__attribute__((dllimport)) void SetFileApisToOEM(void);
__attribute__((dllimport)) bool_t SetConsoleCtrlHandler(void* handler, bool_t add);
__attribute__((dllimport)) bool_t GetProcessTimes(void* proc, void* ct, void* et, void* kt, void* ut);
__attribute__((dllimport)) bool_t SetProcessAffinityMask(void* proc, uint64_t mask);
__attribute__((dllimport)) uint64_t SetThreadAffinityMask(void* thread, uint64_t mask);
__attribute__((dllimport)) dword_t ResumeThread(void* thread);
__attribute__((dllimport)) void* OpenEventW(dword_t access, bool_t inherit, const uint16_t* name);
__attribute__((dllimport)) void* OpenFileMappingW(dword_t access, bool_t inherit, const uint16_t* name);
__attribute__((dllimport)) bool_t FileTimeToDosDateTime(const void* ft, uint16_t* date, uint16_t* time);
__attribute__((dllimport)) int32_t CompareFileTime(const void* ft1, const void* ft2);
__attribute__((dllimport)) bool_t GetDiskFreeSpaceW(const uint16_t* root, dword_t* spc, dword_t* bps, dword_t* nfc, dword_t* tnc);
__attribute__((dllimport)) dword_t GetLogicalDriveStringsW(dword_t len, uint16_t* buf);

// ADVAPI32
__attribute__((dllimport)) bool_t GetFileSecurityW(const uint16_t* name, dword_t info, void* sd, dword_t len, dword_t* needed);

// msvcrt
__attribute__((dllimport)) int32_t memcmp(const void* s1, const void* s2, uint64_t n);
__attribute__((dllimport)) int32_t wcscmp(const uint16_t* s1, const uint16_t* s2);
__attribute__((dllimport)) uint16_t* wcsstr(const uint16_t* str, const uint16_t* substr);
__attribute__((dllimport)) int32_t _XcptFilter(uint64_t xcpt, void* info);
__attribute__((dllimport)) void _c_exit(void);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t bytes_written = 0;
    if (output == (void*)0 || output == (void*)(uint64_t)-1) {
        ExitProcess(1);
    }

    (void)GetVersion();
    (void)GetLargePageMinimum();
    SetFileApisToOEM();
    (void)SetConsoleCtrlHandler((void*)0, 1);
    (void)GetProcessTimes(GetCurrentProcess(), (void*)0, (void*)0, (void*)0, (void*)0);
    (void)SetProcessAffinityMask(GetCurrentProcess(), 1);
    (void)SetThreadAffinityMask((void*)0, 1);
    (void)ResumeThread((void*)0);

    static const uint16_t kEventName[] = {'T', 'e', 's', 't', 'E', 'v', 'e', 'n', 't', 0};
    (void)OpenEventW(0, 0, kEventName);
    (void)OpenFileMappingW(0, 0, kEventName);

    uint64_t ft1 = 100, ft2 = 200;
    (void)CompareFileTime(&ft1, &ft2);
    uint16_t fat_d = 0, fat_t = 0;
    (void)FileTimeToDosDateTime(&ft1, &fat_d, &fat_t);

    dword_t spc, bps, nfc, tnc;
    (void)GetDiskFreeSpaceW((void*)0, &spc, &bps, &nfc, &tnc);

    uint16_t drives[16] = {0};
    (void)GetLogicalDriveStringsW(16, drives);

    (void)GetFileSecurityW((void*)0, 0, (void*)0, 0, (void*)0);

    static const char m1[] = "abc";
    static const char m2[] = "abc";
    (void)memcmp(m1, m2, 3);

    static const uint16_t ws1[] = {'a', 'b', 'c', 0};
    static const uint16_t ws2[] = {'a', 'b', 'c', 0};
    (void)wcscmp(ws1, ws2);
    (void)wcsstr(ws1, ws2);

    (void)_XcptFilter(0, (void*)0);
    _c_exit();

    static const char success[] = "7zclicrt\n";
    if (!WriteFile(output, success, sizeof(success) - 1, &bytes_written, (void*)0)) {
        ExitProcess(2);
    }

    ExitProcess(0);
}

