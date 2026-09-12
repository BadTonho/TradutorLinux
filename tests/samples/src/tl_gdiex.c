typedef unsigned long dword_t;
typedef int bool_t;
typedef unsigned short word_t;
typedef void* handle_t;
typedef unsigned long long size_t_guest;

__attribute__((dllimport)) void* GetStdHandle(dword_t nStdHandle);
__attribute__((dllimport)) bool_t WriteFile(void* hFile, const void* lpBuffer, dword_t nNumberOfBytesToWrite,
                                            dword_t* lpNumberOfBytesWritten, void* lpOverlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t uExitCode);
__attribute__((dllimport)) void* GetDC(void* hwnd);
__attribute__((dllimport)) int ReleaseDC(void* hwnd, void* hdc);
__attribute__((dllimport)) void* GetStockObject(int fnObject);
__attribute__((dllimport)) void* CreateFontW(int h, int w, int esc, int ori, int weight, dword_t it, dword_t ul,
                                             dword_t so, dword_t cs, dword_t out, dword_t clip, dword_t qual,
                                             dword_t pitch, const word_t* face);
__attribute__((dllimport)) dword_t SetDCBrushColor(void* hdc, dword_t color);
__attribute__((dllimport)) dword_t SetDCPenColor(void* hdc, dword_t color);
__attribute__((dllimport)) int DeleteObject(void* obj);
__attribute__((dllimport)) int GdiplusStartup(void* token, const void* input, void* output);
__attribute__((dllimport)) void GdiplusShutdown(void* token);
__attribute__((dllimport)) void* GdipAlloc(size_t_guest size);
__attribute__((dllimport)) void GdipFree(void* ptr);
__attribute__((dllimport)) int GdipCreateBitmapFromStream(void* stream, void** bitmap);
__attribute__((dllimport)) int GdipCloneImage(void* image, void** clone);
__attribute__((dllimport)) int GdipDisposeImage(void* image);
__attribute__((dllimport)) int GdipCreateHBITMAPFromBitmap(void* bitmap, void** hbm, dword_t color);
__attribute__((dllimport)) int SetWindowTheme(void* hwnd, const word_t* subApp, const word_t* subId);
__attribute__((dllimport)) dword_t timeSetEvent(dword_t delay, dword_t res, void* cb, size_t_guest user, dword_t flags);
__attribute__((dllimport)) int SymFromAddr(void* process, unsigned long long addr, unsigned long long* disp, void* sym);
__attribute__((dllimport)) void* GetCurrentProcess(void);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    void* stdout_handle = GetStdHandle((dword_t)-11);
    dword_t written = 0;

    // GDI32
    void* dc = GetDC((void*)0);
    if (dc == (void*)0) ExitProcess(10U);
    const word_t kArial[] = {'A','r','i','a','l',0};
    void* font = CreateFontW(12,0,0,0,400,0,0,0,0,0,0,0,0,kArial);
    if (font == (void*)0) ExitProcess(11U);
    dword_t c1 = SetDCBrushColor(dc, 0x00FF00);
    (void)c1;
    dword_t c2 = SetDCPenColor(dc, 0xFF0000);
    (void)c2;
    if (!DeleteObject(font)) ExitProcess(12U);
    ReleaseDC((void*)0, dc);

    // gdiplus
    void* token = (void*)1;
    char input[16] = {1,0,0,0};
    if (GdiplusStartup(&token, input, (void*)0) != 1U) ExitProcess(20U);
    if (token != (void*)0) ExitProcess(21U);
    void* bmp = (void*)1;
    if (GdipCreateBitmapFromStream((void*)0, &bmp) != 1) ExitProcess(22U);
    if (bmp != (void*)0) ExitProcess(23U);
    void* clone = (void*)1;
    if (GdipCloneImage((void*)0, &clone) != 1) ExitProcess(24U);
    if (clone != (void*)0) ExitProcess(25U);
    void* hbm = (void*)1;
    if (GdipCreateHBITMAPFromBitmap((void*)0, &hbm, 0) != 1) ExitProcess(26U);
    if (hbm != (void*)0) ExitProcess(27U);
    if (GdipDisposeImage((void*)0) != 1) ExitProcess(28U);
    GdiplusShutdown((void*)0);
    void* mem = GdipAlloc(32);
    if (mem != (void*)0) ExitProcess(29U);
    GdipFree(mem);

    // UxTheme
    const word_t kExplorer[] = {'E','x','p','l','o','r','e','r',0};
    if (SetWindowTheme((void*)0, kExplorer, (word_t*)0) != (int)0x80004001U) ExitProcess(30U);
    if (SetWindowTheme((void*)0, (word_t*)0, (word_t*)0) != (int)0x80004001U) ExitProcess(31U);

    // WINMM
    dword_t tid = timeSetEvent(10, 0, (void*)0, 0, 0);
    if (tid == 0) ExitProcess(40U);

    // dbghelp
    void* proc = GetCurrentProcess();
    unsigned long long disp = 0;
    char sym[64] = {0};
    // SYMBOL_INFO minimal: SizeOfStruct = 88? But our stub just validates 4 bytes, so any buffer works
    *(dword_t*)sym = 88;
    int r = SymFromAddr(proc, 0x140000000ULL, &disp, sym);
    (void)r; // stub returns 0, but should not crash
    // Test with null displacement
    r = SymFromAddr(proc, 0, (unsigned long long*)0, sym);
    (void)r;

    static const char msg[] = "gdiex\n";
    if (!WriteFile(stdout_handle, msg, sizeof(msg)-1, &written, (void*)0) || written != sizeof(msg)-1) {
        ExitProcess(99U);
    }
    ExitProcess(0U);
}
