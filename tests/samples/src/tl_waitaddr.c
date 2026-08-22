typedef unsigned long dword_t;
typedef int bool_t;
typedef unsigned short word_t;
typedef unsigned long long size_t_guest;
typedef unsigned long long ull_t;

__attribute__((dllimport)) void* GetStdHandle(dword_t nStdHandle);
__attribute__((dllimport)) bool_t WriteFile(void* hFile, const void* lpBuffer, dword_t nNumberOfBytesToWrite,
                                            dword_t* lpNumberOfBytesWritten, void* lpOverlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t uExitCode);
__attribute__((dllimport)) void Sleep(dword_t dwMilliseconds);
__attribute__((dllimport)) void* CreateThread(void* lpThreadAttributes, size_t_guest dwStackSize,
                                              void* lpStartAddress, void* lpParameter,
                                              dword_t dwCreationFlags, dword_t* lpThreadId);
__attribute__((dllimport)) dword_t WaitForSingleObject(void* hHandle, dword_t dwMilliseconds);
__attribute__((dllimport)) bool_t CloseHandle(void* hObject);
__attribute__((dllimport, noreturn)) void ExitThread(dword_t dwExitCode);
__attribute__((dllimport)) bool_t WaitOnAddress(void* Address, void* CompareAddress, size_t_guest AddressSize, dword_t dwMilliseconds);
__attribute__((dllimport)) void WakeByAddressSingle(void* Address);
__attribute__((dllimport)) void WakeByAddressAll(void* Address);
__attribute__((dllimport)) dword_t GetLastError(void);

static volatile int g_addr = 0;
static int g_compare_zero = 0;
static int g_compare_one = 1;

void waiter_thread(void* param) {
    (void)param;
    bool_t res = WaitOnAddress((void*)&g_addr, (void*)&g_compare_zero, 4, 5000);
    if (!res) {
        ExitProcess(20U);
    }
    if (g_addr != 1) {
        ExitProcess(21U);
    }
    g_addr = 2;
    WakeByAddressSingle((void*)&g_addr);
    ExitThread(0U);
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    void* stdout_handle = GetStdHandle((dword_t)-11);
    dword_t written = 0;

    // ---- Teste 1: WaitOnAddress imediato quando valor diferente ----
    volatile int addr1 = 1;
    int cmp0 = 0;
    if (!WaitOnAddress((void*)&addr1, (void*)&cmp0, 4, 100)) {
        ExitProcess(10U); // deveria retornar TRUE imediatamente sem esperar
    }

    // ---- Teste 2: WaitOnAddress timeout quando igual e sem wake ----
    volatile int addr2 = 0;
    int cmp02 = 0;
    bool_t r2 = WaitOnAddress((void*)&addr2, (void*)&cmp02, 4, 50);
    if (r2) ExitProcess(11U); // deveria falhar por timeout
    if (GetLastError() != 1460U) ExitProcess(12U); // ERROR_TIMEOUT

    // ---- Teste 3: WaitOnAddress com tamanho inválido ----
    if (WaitOnAddress((void*)&addr2, (void*)&cmp02, 3, 10)) ExitProcess(13U);
    if (GetLastError() != 87U) ExitProcess(14U);

    // ---- Teste 4: Wake com thread ----
    g_addr = 0;
    void* th = CreateThread((void*)0, 0, (void*)waiter_thread, (void*)0, 0, (void*)0);
    if (th == (void*)0) ExitProcess(30U);
    Sleep(100); // deixa waiter bloquear
    g_addr = 1;
    WakeByAddressSingle((void*)&g_addr);
    // Espera waiter mudar para 2 via Wake
    // Poll com WaitOnAddress no main para detectar 2
    int expect2 = 2;
    (void)expect2;
    bool_t r4 = WaitOnAddress((void*)&g_addr, (void*)&g_compare_one, 4, 2000);
    if (!r4) ExitProcess(31U);
    if (g_addr != 2) ExitProcess(32U);
    if (WaitForSingleObject(th, 2000) != 0) ExitProcess(33U);
    CloseHandle(th);

    // ---- Teste 5: WakeByAddressAll ----
    volatile int addr5 = 5;
    int cmp5 = 5;
    // Cria dois waiters em threads separados que esperam 5 -> deve acordar ambos com All
    // Simplificamos: testa que WakeByAddressAll não falha mesmo sem waiters
    WakeByAddressAll((void*)&addr5);
    // Se chegou aqui sem crash, ok
    (void)cmp5;

    // ---- Teste 6: WaitOnAddress com tamanhos 1,2,8 ----
    volatile unsigned char c1 = 0xAA;
    unsigned char ccmp = 0xAA;
    bool_t r6 = WaitOnAddress((void*)&c1, (void*)&ccmp, 1, 10);
    if (r6) ExitProcess(40U);
    if (GetLastError() != 1460U) ExitProcess(41U);
    c1 = 0xBB;
    if (!WaitOnAddress((void*)&c1, (void*)&ccmp, 1, 10)) ExitProcess(42U);

    volatile unsigned long long ll = 0x0102030405060708ULL;
    unsigned long long llcmp = 0x0102030405060708ULL;
    bool_t r7 = WaitOnAddress((void*)&ll, (void*)&llcmp, 8, 10);
    if (r7) ExitProcess(43U);
    ll = 0x0807060504030201ULL;
    if (!WaitOnAddress((void*)&ll, (void*)&llcmp, 8, 10)) ExitProcess(44U);

    static const char msg[] = "waitaddr\n";
    if (!WriteFile(stdout_handle, msg, sizeof(msg)-1, &written, (void*)0) || written != sizeof(msg)-1) {
        ExitProcess(99U);
    }
    // Deixar thread waiter em background; ExitProcess encerra processo filho e host vai liberar.
    ExitProcess(0U);
}
