typedef unsigned long dword_t;
typedef int bool_t;
typedef long hresult_t;

typedef struct {
    dword_t Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} guid_t;

__attribute__((dllimport)) void* GetStdHandle(dword_t nStdHandle);
__attribute__((dllimport)) bool_t WriteFile(void* hFile, const void* lpBuffer, dword_t nNumberOfBytesToWrite,
                                            dword_t* lpNumberOfBytesWritten, void* lpOverlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t uExitCode);
__attribute__((dllimport)) hresult_t CoInitialize(void* reserved);
__attribute__((dllimport)) hresult_t CoInitializeEx(void* reserved, dword_t coInit);
__attribute__((dllimport)) void CoUninitialize(void);
__attribute__((dllimport)) hresult_t CoCreateInstance(const guid_t* rclsid, void* unkOuter, dword_t clsCtx,
                                                        const guid_t* riid, void** ppv);
__attribute__((dllimport)) hresult_t CoGetClassObject(const guid_t* rclsid, dword_t clsCtx, void* srv, const guid_t* riid, void** ppv);
__attribute__((dllimport)) hresult_t OleInitialize(void* reserved);
__attribute__((dllimport)) void OleUninitialize(void);
__attribute__((dllimport)) void* CoTaskMemAlloc(dword_t size);
__attribute__((dllimport)) void CoTaskMemFree(void* ptr);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    void* stdout_handle = GetStdHandle((dword_t)-11);
    dword_t written = 0;

    hresult_t hr = CoInitialize((void*)0);
    if (hr != 0) ExitProcess(10U);
    CoUninitialize();

    hr = CoInitializeEx((void*)0, 0);
    if (hr != 0) ExitProcess(11U);

    // CoCreateInstance with null should fail E_INVALIDARG 0x80070057
    void* out = (void*)0;
    static const guid_t dummy = {0x12345678, 0x1234, 0x5678, {0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0}};
    hr = CoCreateInstance((guid_t*)0, (void*)0, 1, &dummy, &out);
    if (hr != (hresult_t)0x80070057) ExitProcess(12U);
    // Com pUnkOuter não nulo deve retornar CLASS_E_NOAGGREGATION 0x80040110
    hr = CoCreateInstance(&dummy, (void*)0x1, 1, &dummy, &out);
    if (hr != (hresult_t)0x80040110) ExitProcess(13U);
    // Com CLSID válido mas não registrado deve retornar REGDB_E_CLASSNOTREG 0x80040154
    hr = CoCreateInstance(&dummy, (void*)0, 1, &dummy, &out);
    if (hr != (hresult_t)0x80040154) ExitProcess(14U);
    if (out != (void*)0) ExitProcess(15U);

    hr = CoGetClassObject(&dummy, 1, (void*)0, &dummy, &out);
    if (hr != (hresult_t)0x80040154) ExitProcess(16U);

    hr = OleInitialize((void*)0);
    if (hr != 0) ExitProcess(20U);
    OleUninitialize();
    CoUninitialize();

    void* mem = CoTaskMemAlloc(32);
    if (mem == (void*)0) ExitProcess(30U);
    CoTaskMemFree(mem);

    static const char msg[] = "com\n";
    if (!WriteFile(stdout_handle, msg, sizeof(msg)-1, &written, (void*)0) || written != sizeof(msg)-1) {
        ExitProcess(99U);
    }
    ExitProcess(0U);
}
