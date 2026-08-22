typedef unsigned long dword_t;
typedef int bool_t;
typedef unsigned short word_t;
typedef void* handle_t;

__attribute__((dllimport)) void* GetStdHandle(dword_t nStdHandle);
__attribute__((dllimport)) bool_t WriteFile(void* hFile, const void* lpBuffer, dword_t nNumberOfBytesToWrite,
                                            dword_t* lpNumberOfBytesWritten, void* lpOverlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t uExitCode);
__attribute__((dllimport)) dword_t GetCurrentProcessId(void);
__attribute__((dllimport)) dword_t GetLastError(void);
__attribute__((dllimport)) bool_t CloseHandle(void* hObject);
__attribute__((dllimport)) void* CreateToolhelp32Snapshot(dword_t dwFlags, dword_t th32ProcessID);
__attribute__((dllimport)) bool_t Process32FirstW(void* hSnapshot, void* lppe);
__attribute__((dllimport)) bool_t Process32NextW(void* hSnapshot, void* lppe);
__attribute__((dllimport)) void* OpenProcess(dword_t dwDesiredAccess, bool_t bInheritHandle, dword_t dwProcessId);

typedef struct {
    dword_t dwSize;
    dword_t cntUsage;
    dword_t th32ProcessID;
    dword_t pad1;
    void* th32DefaultHeapID;
    dword_t th32ModuleID;
    dword_t cntThreads;
    dword_t th32ParentProcessID;
    long pcPriClassBase;
    dword_t dwFlags;
    word_t szExeFile[260];
    dword_t pad2;
} PROCESSENTRY32W;

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    void* stdout_handle = GetStdHandle((dword_t)-11);
    dword_t written = 0;
    dword_t myPid = GetCurrentProcessId();
    if (myPid == 0) ExitProcess(10U);

    // 1) Snapshot com flags inválidas (0) deve falhar com INVALID_HANDLE_VALUE (-1) e 87
    void* badSnap = CreateToolhelp32Snapshot(0, 0);
    if (badSnap != (void*)(~0ULL)) ExitProcess(11U);
    if (GetLastError() != 87U) ExitProcess(12U);

    // 2) Snapshot válido SNAPPROCESS
    void* snap = CreateToolhelp32Snapshot(2, 0); // TH32CS_SNAPPROCESS =2
    if (snap == (void*)(~0ULL) || snap == (void*)0) ExitProcess(13U);

    // 3) Process32FirstW
    PROCESSENTRY32W entry;
    entry.dwSize = 568;
    // zero resto para evitar lixo
    for (int i=0;i<260;i++) entry.szExeFile[i]=0;
    if (!Process32FirstW(snap, &entry)) ExitProcess(14U);
    if (entry.dwSize != 568) ExitProcess(15U);
    if (entry.th32ProcessID == 0) ExitProcess(16U);
    // Verifica que ao menos um processo foi encontrado
    bool_t found_self = 0;
    bool_t found_any = 0;
    int count = 0;
    do {
        found_any = 1;
        count++;
        if (entry.th32ProcessID == myPid) {
            found_self = 1;
            if (entry.cntThreads == 0) ExitProcess(17U);
            if (entry.szExeFile[0] == 0) ExitProcess(18U);
        }
        if (count > 10000) break; // evita loop infinito se snapshot quebrado
        // Se já achou self e já iterou alguns, pode continuar até fim para validar Process32Next
    } while (Process32NextW(snap, &entry));
    if (!found_any) ExitProcess(19U);
    // Após fim, GetLastError deve ser 18 (NO_MORE_FILES)
    if (GetLastError() != 18U) ExitProcess(20U);
    if (!found_self) ExitProcess(21U);

    // 4) Process32NextW sem snapshot válido deve falhar 87
    PROCESSENTRY32W dummy;
    dummy.dwSize = 568;
    if (Process32NextW((void*)0, &dummy)) ExitProcess(22U);

    // 5) OpenProcess self
    void* hProc = OpenProcess(0x1F0FFF, 0, myPid);
    if (hProc == (void*)0) ExitProcess(30U);
    if (!CloseHandle(hProc)) ExitProcess(31U);

    // 6) OpenProcess pid inválido 0xFFFFFF deve falhar
    void* hBad = OpenProcess(0, 0, 0xFFFFFFU);
    if (hBad != (void*)0) {
        CloseHandle(hBad);
        ExitProcess(32U);
    }
    if (GetLastError() != 87U) ExitProcess(33U);

    // 7) OpenProcess pid 0 deve falhar 87
    void* hZero = OpenProcess(0, 0, 0);
    if (hZero != (void*)0) ExitProcess(34U);

    if (!CloseHandle(snap)) ExitProcess(40U);
    // CloseHandle em snapshot já fechado deve falhar 6
    if (CloseHandle(snap)) ExitProcess(41U);
    if (GetLastError() != 6U) ExitProcess(42U);

    static const char msg[] = "toolhelp\n";
    if (!WriteFile(stdout_handle, msg, sizeof(msg)-1, &written, (void*)0) || written != sizeof(msg)-1) {
        ExitProcess(99U);
    }
    ExitProcess(0U);
}
