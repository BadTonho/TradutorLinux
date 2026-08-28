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
__attribute__((dllimport)) int32_t lstrlenW(const uint16_t* s);
__attribute__((dllimport)) uint16_t GetSystemDefaultLangID(void);
__attribute__((dllimport)) uint16_t GetUserDefaultLangID(void);
__attribute__((dllimport)) dword_t GetWindowsDirectoryW(uint16_t* buf, dword_t sz);
__attribute__((dllimport)) uint64_t GlobalSize(void* mem);
__attribute__((dllimport)) bool_t SetPriorityClass(void* proc, dword_t prio);
__attribute__((dllimport)) void* FindFirstChangeNotificationW(const uint16_t* path, bool_t sub, dword_t filter);
__attribute__((dllimport)) bool_t FindNextChangeNotification(void* handle);
__attribute__((dllimport)) bool_t FindCloseChangeNotification(void* handle);

// USER32
__attribute__((dllimport)) void* GetMenu(void* hwnd);
__attribute__((dllimport)) bool_t SetMenu(void* hwnd, void* menu);
__attribute__((dllimport)) void* GetSubMenu(void* menu, int32_t pos);
__attribute__((dllimport)) int32_t GetMenuItemCount(void* menu);
__attribute__((dllimport)) bool_t CheckDlgButton(void* hdlg, int32_t id, dword_t check);
__attribute__((dllimport)) dword_t IsDlgButtonChecked(void* hdlg, int32_t id);
__attribute__((dllimport)) bool_t CheckRadioButton(void* hdlg, int32_t f, int32_t l, int32_t c);
__attribute__((dllimport)) bool_t MapDialogRect(void* hdlg, void* rect);
__attribute__((dllimport)) dword_t GetDialogBaseUnits(void);
__attribute__((dllimport)) void* WindowFromPoint(int64_t pt);
__attribute__((dllimport)) bool_t ScreenToClient(void* hwnd, void* pt);

// MPR
__attribute__((dllimport)) dword_t WNetOpenEnumW(dword_t scope, dword_t type, dword_t usage, const void* nr, void** handle);
__attribute__((dllimport)) dword_t WNetEnumResourceW(void* handle, dword_t* count, void* buf, dword_t* sz);
__attribute__((dllimport)) dword_t WNetCloseEnum(void* handle);

// COMCTL32
__attribute__((dllimport)) void* CreateStatusWindowW(int32_t style, const uint16_t* text, void* parent, dword_t id);
__attribute__((dllimport)) int32_t ImageList_GetImageCount(void* il);

// SHELL32
__attribute__((dllimport)) dword_t ExtractIconExW(const uint16_t* file, int32_t idx, void** lg, void** sm, dword_t n);
__attribute__((dllimport)) int32_t SHGetDesktopFolder(void** ppshf);

// ADVAPI32
__attribute__((dllimport)) bool_t GetUserNameW(uint16_t* buf, dword_t* sz);

// msvcrt
__attribute__((dllimport)) int32_t rand(void);
__attribute__((dllimport)) void srand(dword_t seed);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t bytes_written = 0;
    if (output == (void*)0 || output == (void*)(uint64_t)-1) {
        ExitProcess(1);
    }

    srand(42);
    (void)rand();

    uint16_t path[] = {'C', ':', '\\', 0};
    (void)lstrlenW(path);
    (void)GetSystemDefaultLangID();
    (void)GetUserDefaultLangID();
    (void)GetWindowsDirectoryW(path, 3);
    (void)GlobalSize((void*)0x1000);
    (void)SetPriorityClass(GetCurrentProcess(), 0x20);

    void* change = FindFirstChangeNotificationW(path, 0, 1);
    if (change != (void*)0) {
        (void)FindNextChangeNotification(change);
        (void)FindCloseChangeNotification(change);
    }

    void* m = GetMenu((void*)0);
    (void)SetMenu((void*)0, m);
    (void)GetSubMenu(m, 0);
    (void)GetMenuItemCount(m);
    (void)CheckDlgButton((void*)0, 1, 1);
    (void)IsDlgButtonChecked((void*)0, 1);
    (void)CheckRadioButton((void*)0, 1, 2, 1);
    (void)MapDialogRect((void*)0, (void*)0);
    (void)GetDialogBaseUnits();
    (void)WindowFromPoint(0);
    (void)ScreenToClient((void*)0, (void*)0);

    void* enum_h = (void*)0;
    if (WNetOpenEnumW(0, 0, 0, (void*)0, &enum_h) == 0) {
        dword_t cnt = 0;
        (void)WNetEnumResourceW(enum_h, &cnt, (void*)0, (void*)0);
        (void)WNetCloseEnum(enum_h);
    }

    (void)CreateStatusWindowW(0, path, (void*)0, 1);
    (void)ImageList_GetImageCount((void*)0);

    void* ic = (void*)0;
    (void)ExtractIconExW(path, 0, &ic, (void*)0, 1);
    void* shf = (void*)0;
    (void)SHGetDesktopFolder(&shf);

    dword_t user_sz = 3;
    (void)GetUserNameW(path, &user_sz);

    const char message[] = "7ZFM GUI APIS OK\n";
    WriteFile(output, message, sizeof(message) - 1, &bytes_written, (void*)0);
    ExitProcess(0);
}
