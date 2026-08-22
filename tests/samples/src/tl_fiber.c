typedef unsigned long dword_t;
typedef int bool_t;
typedef unsigned long long size_t_guest;

__attribute__((dllimport)) void* GetStdHandle(dword_t nStdHandle);
__attribute__((dllimport)) bool_t WriteFile(void* hFile, const void* lpBuffer, dword_t nNumberOfBytesToWrite,
                                            dword_t* lpNumberOfBytesWritten, void* lpOverlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t uExitCode);
__attribute__((dllimport)) void* ConvertThreadToFiber(void* lpParameter);
__attribute__((dllimport)) void* ConvertThreadToFiberEx(void* lpParameter, dword_t dwFlags);
__attribute__((dllimport)) bool_t ConvertFiberToThread(void);
__attribute__((dllimport)) void* CreateFiber(size_t_guest dwStackSize, void* lpStartAddress, void* lpParameter);
__attribute__((dllimport)) void* CreateFiberEx(size_t_guest dwStackCommitSize, size_t_guest dwStackReserveSize,
                                               dword_t dwFlags, void* lpStartAddress, void* lpParameter);
__attribute__((dllimport)) void SwitchToFiber(void* lpFiber);
__attribute__((dllimport)) void DeleteFiber(void* lpFiber);
__attribute__((dllimport)) void* GetFiberData(void);

void fiber_dummy(void* param) {
    (void)param;
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    void* stdout_handle = GetStdHandle((dword_t)-11);
    dword_t written = 0;

    // 1) ConvertThreadToFiber
    void* fiber_main = ConvertThreadToFiber((void*)0x1234ULL);
    if (fiber_main == (void*)0) ExitProcess(10U);
    if (GetFiberData() != (void*)0x1234ULL) ExitProcess(11U);

    // 2) ConvertThreadToFiberEx com flags 0 e 1 (FIBER_FLAG_FLOAT_SWITCH)
    void* fiber_ex = ConvertThreadToFiberEx((void*)0x5678ULL, 0);
    if (fiber_ex == (void*)0) ExitProcess(12U);
    if (GetFiberData() != (void*)0x5678ULL) ExitProcess(13U);
    void* fiber_ex2 = ConvertThreadToFiberEx((void*)0x9ABCULL, 1);
    if (fiber_ex2 == (void*)0) ExitProcess(14U);
    if (GetFiberData() != (void*)0x9ABCULL) ExitProcess(15U);

    // 3) CreateFiber
    void* f1 = CreateFiber(0, (void*)fiber_dummy, (void*)0x1111ULL);
    if (f1 == (void*)0) ExitProcess(20U);
    // CreateFiberEx with commit/reserve and flags
    void* f2 = CreateFiberEx(4096, 65536, 0, (void*)fiber_dummy, (void*)0x2222ULL);
    if (f2 == (void*)0) ExitProcess(21U);
    void* f3 = CreateFiberEx(0, 0, 0, (void*)0, (void*)0);
    if (f3 == (void*)0) ExitProcess(22U);

    // 4) SwitchToFiber (stub não faz nada, mas não deve crashar)
    SwitchToFiber(f1);
    SwitchToFiber(f2);
    SwitchToFiber(fiber_main);

    // 5) GetFiberData ainda deve ser do thread convertido (último Convert)
    if (GetFiberData() != (void*)0x9ABCULL) ExitProcess(30U);

    // 6) DeleteFiber
    DeleteFiber(f1);
    DeleteFiber(f2);
    DeleteFiber(f3);

    // 7) ConvertFiberToThread
    if (!ConvertFiberToThread()) ExitProcess(40U);
    if (GetFiberData() != (void*)0) ExitProcess(41U);

    // Re-converter para testar novamente após ConvertFiberToThread
    void* fiber_again = ConvertThreadToFiberEx((void*)0xDEADULL, 0);
    if (fiber_again == (void*)0) ExitProcess(42U);
    if (GetFiberData() != (void*)0xDEADULL) ExitProcess(43U);
    ConvertFiberToThread();

    static const char msg[] = "fiber\n";
    if (!WriteFile(stdout_handle, msg, sizeof(msg)-1, &written, (void*)0) || written != sizeof(msg)-1) {
        ExitProcess(99U);
    }
    ExitProcess(0U);
}
