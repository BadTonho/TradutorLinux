typedef unsigned char byte_t;
typedef unsigned int dword_t;
typedef int bool_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
typedef unsigned short uint16_t;
typedef unsigned long long uint64_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) void* GetCurrentProcess(void);

// KERNEL32
__attribute__((dllimport)) bool_t SetNamedPipeHandleState(void* pipe, dword_t* mode, dword_t* mcc, dword_t* cdt);
__attribute__((dllimport)) bool_t WaitNamedPipeW(const uint16_t* name, dword_t timeout);
__attribute__((dllimport)) bool_t PeekNamedPipe(void* pipe, void* buf, dword_t sz, dword_t* read, dword_t* avail, dword_t* left);
__attribute__((dllimport)) dword_t WaitForSingleObjectEx(void* handle, dword_t ms, bool_t alertable);
__attribute__((dllimport)) bool_t GetExitCodeThread(void* thread, dword_t* code);
__attribute__((dllimport)) bool_t TryAcquireSRWLockExclusive(void* lock);
__attribute__((dllimport)) bool_t SetThreadLocale(dword_t locale);
__attribute__((dllimport)) uint16_t SetThreadUILanguage(uint16_t lang);
__attribute__((dllimport)) uint16_t GetUserDefaultUILanguage(void);
__attribute__((dllimport)) dword_t GetLogicalDrives(void);
__attribute__((dllimport)) bool_t GetPhysicallyInstalledSystemMemory(uint64_t* total_kb);
__attribute__((dllimport)) bool_t GetVolumePathNameA(const char* file, char* path, dword_t len);
__attribute__((dllimport)) bool_t UnregisterWaitEx(void* handle, void* event);
__attribute__((dllimport)) bool_t RegisterWaitForSingleObject(void** obj, void* handle, void* cb, void* ctx, dword_t ms, dword_t flags);
__attribute__((dllimport)) bool_t SetSearchPathMode(dword_t flags);
__attribute__((dllimport)) void* InterlockedPushEntrySList(void* list, void* entry);

// COMCTL32
__attribute__((dllimport)) bool_t SetWindowSubclass(void* hwnd, void* cb, uint64_t id, uint64_t ref);
__attribute__((dllimport)) uint64_t DefSubclassProc(void* hwnd, dword_t msg, uint64_t wp, int64_t lp);

// ADVAPI32
__attribute__((dllimport)) int32_t RegDeleteTreeW(void* key, const uint16_t* subkey);
__attribute__((dllimport)) int32_t RegDeleteKeyExW(void* key, const uint16_t* subkey, dword_t sam, dword_t res);

// USER32
__attribute__((dllimport)) bool_t OpenClipboard(void* owner);
__attribute__((dllimport)) bool_t CloseClipboard(void);
__attribute__((dllimport)) void* SetClipboardData(dword_t format, void* mem);
__attribute__((dllimport)) bool_t EmptyClipboard(void);
__attribute__((dllimport)) int32_t MessageBoxExW(void* hwnd, const uint16_t* txt, const uint16_t* cap, dword_t type, uint16_t lang);
__attribute__((dllimport)) bool_t ClientToScreen(void* hwnd, void* pt);

// GDI32
__attribute__((dllimport)) bool_t GetTextExtentPoint32W(void* hdc, const uint16_t* str, int32_t len, void* sz);
__attribute__((dllimport)) int32_t StartDocW(void* hdc, const void* doc_info);
__attribute__((dllimport)) int32_t EndDoc(void* hdc);

// SHLWAPI
__attribute__((dllimport)) bool_t PathStripToRootW(uint16_t* path);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t bytes_written = 0;
    if (output == (void*)0 || output == (void*)(uint64_t)-1) {
        ExitProcess(1);
    }

    (void)GetCurrentProcess();
    (void)SetNamedPipeHandleState((void*)0, (void*)0, (void*)0, (void*)0);
    static const uint16_t kPipeName[] = {'\\', '\\', '.', '\\', 'p', 'i', 'p', 'e', 0};
    (void)WaitNamedPipeW(kPipeName, 0);
    dword_t bread = 0, bavail = 0, bleft = 0;
    (void)PeekNamedPipe((void*)0, (void*)0, 0, &bread, &bavail, &bleft);
    (void)WaitForSingleObjectEx((void*)0, 0, 0);
    dword_t thread_exit = 0;
    (void)GetExitCodeThread((void*)0, &thread_exit);
    uint64_t lock = 0;
    (void)TryAcquireSRWLockExclusive(&lock);

    (void)SetThreadLocale(1033);
    (void)SetThreadUILanguage(1033);
    (void)GetUserDefaultUILanguage();
    (void)GetLogicalDrives();
    uint64_t total_kb = 0;
    (void)GetPhysicallyInstalledSystemMemory(&total_kb);
    char vol[16] = {0};
    (void)GetVolumePathNameA("C:\\test", vol, 16);

    (void)RegDeleteTreeW((void*)0, (void*)0);
    (void)RegDeleteKeyExW((void*)0, (void*)0, 0, 0);

    (void)OpenClipboard((void*)0);
    (void)EmptyClipboard();
    (void)SetClipboardData(1, (void*)0);
    (void)CloseClipboard();

    uint16_t pt[2] = {0, 0};
    (void)ClientToScreen((void*)0, pt);

    uint16_t sz[2] = {0, 0};
    (void)GetTextExtentPoint32W((void*)0, kPipeName, 8, sz);
    (void)StartDocW((void*)0, (void*)0);
    (void)EndDoc((void*)0);

    uint16_t path[16] = {'C', ':', '\\', 'a', 0};
    (void)PathStripToRootW(path);

    (void)UnregisterWaitEx((void*)0, (void*)0);
    void* wobj = (void*)0;
    (void)RegisterWaitForSingleObject(&wobj, (void*)0, (void*)0, (void*)0, 0, 0);
    (void)SetSearchPathMode(1);
    void* lhead = (void*)0;
    void* lentry = (void*)0;
    (void)InterlockedPushEntrySList(&lhead, &lentry);

    (void)SetWindowSubclass((void*)0, (void*)0, 1, 0);
    (void)DefSubclassProc((void*)0, 0, 0, 0);

    static const char success[] = "rockstarpipes\n";
    if (!WriteFile(output, success, sizeof(success) - 1, &bytes_written, (void*)0)) {
        ExitProcess(2);
    }

    ExitProcess(0);
}

