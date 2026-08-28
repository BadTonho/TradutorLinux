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

// WS2_32
__attribute__((dllimport)) int32_t WSAStartup(uint16_t ver, void* data);
__attribute__((dllimport)) void* WSACreateEvent(void);
__attribute__((dllimport)) bool_t WSACloseEvent(void* ev);
__attribute__((dllimport)) bool_t WSASetEvent(void* ev);
__attribute__((dllimport)) bool_t WSAResetEvent(void* ev);
__attribute__((dllimport)) void* gethostbyname(const char* name);
__attribute__((dllimport)) void* getservbyname(const char* name, const char* proto);
__attribute__((dllimport)) void WSASetLastError(int32_t err);
__attribute__((dllimport)) int32_t WSAGetLastError(void);

// GDI32
__attribute__((dllimport)) bool_t GetTextMetricsW(void* hdc, void* tm);
__attribute__((dllimport)) void* CreatePen(int32_t style, int32_t width, dword_t color);
__attribute__((dllimport)) bool_t ExtTextOutW(void* hdc, int32_t x, int32_t y, dword_t opts,
                                              const void* rc, const uint16_t* s, dword_t cnt, const int32_t* dx);
__attribute__((dllimport)) bool_t MoveToEx(void* hdc, int32_t x, int32_t y, void* pt);
__attribute__((dllimport)) bool_t LineTo(void* hdc, int32_t x, int32_t y);
__attribute__((dllimport)) bool_t Polyline(void* hdc, const void* pts, int32_t cnt);
__attribute__((dllimport)) void* CreateRectRgn(int32_t l, int32_t t, int32_t r, int32_t b);
__attribute__((dllimport)) int32_t SelectClipRgn(void* hdc, void* rgn);
__attribute__((dllimport)) bool_t GetCharWidthW(void* hdc, dword_t first, dword_t last, int32_t* buf);
__attribute__((dllimport)) bool_t GetTextExtentPoint32A(void* hdc, const char* s, int32_t len, void* sz);
__attribute__((dllimport)) dword_t SetTextAlign(void* hdc, dword_t align);

// USER32
__attribute__((dllimport)) bool_t CreateCaret(void* hwnd, void* bm, int32_t w, int32_t h);
__attribute__((dllimport)) bool_t DestroyCaret(void);
__attribute__((dllimport)) bool_t SetCaretPos(int32_t x, int32_t y);
__attribute__((dllimport)) bool_t ShowCaret(void* hwnd);
__attribute__((dllimport)) bool_t HideCaret(void* hwnd);
__attribute__((dllimport)) int32_t SetScrollInfo(void* hwnd, int32_t bar, const void* si, bool_t redr);
__attribute__((dllimport)) bool_t ShowScrollBar(void* hwnd, int32_t bar, bool_t show);
__attribute__((dllimport)) void* SetCapture(void* hwnd);
__attribute__((dllimport)) bool_t ReleaseCapture(void);
__attribute__((dllimport)) int16_t GetAsyncKeyState(int32_t key);
__attribute__((dllimport)) int16_t GetKeyState(int32_t key);
__attribute__((dllimport)) bool_t FlashWindow(void* hwnd, bool_t inv);
__attribute__((dllimport)) dword_t GetSysColor(int32_t idx);
__attribute__((dllimport)) bool_t MessageBeep(dword_t type);
__attribute__((dllimport)) bool_t TrackPopupMenu(void* menu, dword_t flags, int32_t x, int32_t y,
                                                 int32_t rsv, void* hwnd, const void* rc);
__attribute__((dllimport)) void* GetClipboardData(dword_t fmt);
__attribute__((dllimport)) dword_t RegisterClipboardFormatA(const char* name);

// COMDLG32
__attribute__((dllimport)) bool_t ChooseFontW(void* cf);

// IMM32
__attribute__((dllimport)) dword_t ImmGetVirtualKey(void* hwnd);

// SHELL32
__attribute__((dllimport)) bool_t Shell_NotifyIconW(dword_t msg, void* data);

// ADVAPI32
__attribute__((dllimport)) int32_t RegQueryInfoKeyW(void* key, uint16_t* cls, dword_t* cch_cls,
                                                    dword_t* rsv, dword_t* sub_keys, dword_t* max_sub,
                                                    dword_t* max_cls, dword_t* vals, dword_t* max_val_nm,
                                                    dword_t* max_val_len, dword_t* sec, void* time);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t bytes_written = 0;
    if (output == (void*)0 || output == (void*)(uint64_t)-1) {
        ExitProcess(1);
    }

    (void)WSAStartup(0x0202, (void*)0);
    void* ev = WSACreateEvent();
    if (ev != (void*)0) {
        (void)WSASetEvent(ev);
        (void)WSAResetEvent(ev);
        (void)WSACloseEvent(ev);
    }
    (void)gethostbyname("localhost");
    (void)getservbyname("ssh", "tcp");
    WSASetLastError(0);
    (void)WSAGetLastError();

    char tm[64] = {0};
    (void)GetTextMetricsW((void*)0, tm);
    void* pen = CreatePen(0, 1, 0);
    (void)pen;
    uint16_t s[] = {'P', 'u', 'T', 'T', 'Y', 0};
    (void)ExtTextOutW((void*)0, 0, 0, 0, (void*)0, s, 5, (void*)0);
    (void)MoveToEx((void*)0, 0, 0, (void*)0);
    (void)LineTo((void*)0, 10, 10);
    (void)Polyline((void*)0, (void*)0, 0);
    void* rgn = CreateRectRgn(0, 0, 10, 10);
    (void)SelectClipRgn((void*)0, rgn);
    int32_t char_w[5] = {0};
    (void)GetCharWidthW((void*)0, 0, 4, char_w);
    int32_t txt_sz[2] = {0};
    (void)GetTextExtentPoint32A((void*)0, "PuTTY", 5, txt_sz);
    (void)SetTextAlign((void*)0, 0);

    (void)CreateCaret((void*)0, (void*)0, 1, 10);
    (void)SetCaretPos(0, 0);
    (void)ShowCaret((void*)0);
    (void)HideCaret((void*)0);
    (void)DestroyCaret();
    (void)SetScrollInfo((void*)0, 0, (void*)0, 1);
    (void)ShowScrollBar((void*)0, 0, 1);
    (void)SetCapture((void*)0);
    (void)ReleaseCapture();
    (void)GetAsyncKeyState(0);
    (void)GetKeyState(0);
    (void)FlashWindow((void*)0, 1);
    (void)GetSysColor(0);
    (void)MessageBeep(0);
    (void)TrackPopupMenu((void*)0, 0, 0, 0, 0, (void*)0, (void*)0);
    (void)GetClipboardData(1);
    (void)RegisterClipboardFormatA("PUTTY_FORMAT");

    (void)ChooseFontW((void*)0);
    (void)ImmGetVirtualKey((void*)0);
    (void)Shell_NotifyIconW(0, (void*)0);

    dword_t subk = 0;
    (void)RegQueryInfoKeyW((void*)0, (void*)0, (void*)0, (void*)0, &subk, (void*)0, (void*)0, (void*)0, (void*)0, (void*)0, (void*)0, (void*)0);

    const char message[] = "PUTTY SSH APIS OK\n";
    WriteFile(output, message, sizeof(message) - 1, &bytes_written, (void*)0);
    ExitProcess(0);
}

