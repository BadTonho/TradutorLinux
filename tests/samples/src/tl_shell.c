typedef unsigned long dword_t;
typedef int bool_t;
typedef unsigned short word_t;
typedef void* handle_t;
typedef long hresult_t;

__attribute__((dllimport)) void* GetStdHandle(dword_t nStdHandle);
__attribute__((dllimport)) bool_t WriteFile(void* hFile, const void* lpBuffer, dword_t nNumberOfBytesToWrite,
                                            dword_t* lpNumberOfBytesWritten, void* lpOverlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t uExitCode);
__attribute__((dllimport)) hresult_t SHGetKnownFolderPath(const void* rfid, dword_t flags, void* token, word_t** outPath);
__attribute__((dllimport)) hresult_t SHGetFolderPathW(void* hwnd, int csidl, void* token, dword_t flags, word_t* path);
__attribute__((dllimport)) hresult_t SHGetFolderPathAndSubDirW(void* hwnd, int csidl, void* token, dword_t flags,
                                                               const word_t* subDir, word_t* path);
__attribute__((dllimport)) void* ShellExecuteW(void* hwnd, const word_t* operation, const word_t* file,
                                               const word_t* parameters, const word_t* directory, int show);
__attribute__((dllimport)) bool_t ShellExecuteExW(void* execInfo);

typedef struct {
    dword_t Data1;
    word_t Data2;
    word_t Data3;
    unsigned char Data4[8];
} guid_t;

typedef struct {
    dword_t cbSize;
    dword_t fMask;
    void* hwnd;
    const word_t* lpVerb;
    const word_t* lpFile;
    const word_t* lpParameters;
    const word_t* lpDirectory;
    int nShow;
    void* hInstApp;
    void* lpIDList;
    const word_t* lpClass;
    void* hkeyClass;
    dword_t dwHotKey;
    dword_t pad;
    void* hMonitor;
    void* hProcess;
} shellex_t;

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    void* stdout_handle = GetStdHandle((dword_t)-11);
    dword_t written = 0;

    // GUID RoamingAppData: 3EB685DB-65F9-4CF6-A03A-E3EF65729F3D
    static const guid_t kRoaming = {0x3EB685DB, 0x65F9, 0x4CF6, {0xA0,0x3A,0xE3,0xEF,0x65,0x72,0x9F,0x3D}};
    word_t* outPath = (word_t*)0;
    hresult_t hr = SHGetKnownFolderPath(&kRoaming, 0, (void*)0, &outPath);
    if (hr != 0) ExitProcess(10U);
    if (outPath == (word_t*)0) ExitProcess(11U);
    if (outPath[0] == 0) {
        ExitProcess(12U);
    }

    // SHGetKnownFolderPath com rfid null deve falhar
    word_t* badOut = (word_t*)0;
    hresult_t hrBad = SHGetKnownFolderPath((void*)0, 0, (void*)0, &badOut);
    if (hrBad == 0) ExitProcess(13U);

    // SHGetFolderPathW CSIDL_APPDATA 0x1A
    word_t pathA[260] = {0};
    hr = SHGetFolderPathW((void*)0, 0x1A, (void*)0, 0, pathA);
    if (hr != 0) ExitProcess(20U);
    if (pathA[0] == 0) ExitProcess(21U);

    // SHGetFolderPathW com buffer null deve falhar
    hr = SHGetFolderPathW((void*)0, 0x1A, (void*)0, 0, (word_t*)0);
    if (hr == 0) ExitProcess(22U);

    // SHGetFolderPathAndSubDirW
    const word_t kSub[] = {'T','e','s','t','S','u','b',0};
    word_t pathB[260] = {0};
    hr = SHGetFolderPathAndSubDirW((void*)0, 0x1A, (void*)0, 0, kSub, pathB);
    if (hr != 0) ExitProcess(30U);
    // Verifica que contém "TestSub"
    int found = 0;
    for (int i=0; pathB[i]!=0; i++) {
        if (pathB[i]=='T' && pathB[i+1]=='e') { found=1; break; }
    }
    if (!found) ExitProcess(31U);

    // ShellExecuteW
    const word_t kFile[] = {'n','o','t','e','p','a','d','.','e','x','e',0};
    void* h = ShellExecuteW((void*)0, (word_t*)0, kFile, (word_t*)0, (word_t*)0, 1);
    if ((unsigned long long)h > 32) ExitProcess(40U);
    void* h2 = ShellExecuteW((void*)0, (word_t*)0, (word_t*)0, (word_t*)0, (word_t*)0, 1);
    if ((unsigned long long)h2 > 32) ExitProcess(41U);

    // ShellExecuteExW
    const word_t kVerbOpen[] = {'o','p','e','n',0};
    const word_t kFile2[] = {'t','e','s','t','.','t','x','t',0};
    shellex_t ex;
    for (int i=0;i<(int)sizeof(ex);i++) ((char*)&ex)[i]=0;
    ex.cbSize = sizeof(ex);
    ex.fMask = 0;
    ex.lpVerb = kVerbOpen;
    ex.lpFile = kFile2;
    ex.nShow = 1;
    ex.hInstApp = (void*)1;
    ex.hProcess = (void*)1;
    if (ShellExecuteExW(&ex)) ExitProcess(50U);
    // O layout x64 tem hInstApp em 56 e hProcess em 104; ambos são
    // explicitamente limpos quando a operação não é suportada.
    if (ex.hInstApp != (void*)0 || ex.hProcess != (void*)0) ExitProcess(52U);
    // com cbSize inválido deve falhar
    shellex_t bad;
    for (int i=0;i<(int)sizeof(bad);i++) ((char*)&bad)[i]=0;
    bad.cbSize = 10;
    if (ShellExecuteExW(&bad)) ExitProcess(51U);

    static const char msg[] = "shell\n";
    if (!WriteFile(stdout_handle, msg, sizeof(msg)-1, &written, (void*)0) || written != sizeof(msg)-1) {
        ExitProcess(99U);
    }
    ExitProcess(0U);
}
