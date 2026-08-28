#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>
#include <commctrl.h>

void tl_entry(void) {
    HANDLE stdout_h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdout_h == INVALID_HANDLE_VALUE) {
        ExitProcess(1);
    }

    // KERNEL32
    DWORD proc_num = GetCurrentProcessorNumber();
    (void)proc_num;
    HANDLE curr_th = GetCurrentThread();
    (void)curr_th;

    // ADVAPI32
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    if (CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(prov, CALG_SHA, 0, 0, &hash)) {
            CryptHashData(hash, (const BYTE*)"test", 4, 0);
            CryptDestroyHash(hash);
        }
        CryptReleaseContext(prov, 0);
    }

    // WS2_32
    u_short net_val = htons(80);
    (void)net_val;

    // USER32
    HWINSTA wsta = GetProcessWindowStation();
    (void)wsta;

    const char msg[] = "ROBLOX APIS OK\n";
    DWORD written = 0;
    WriteFile(stdout_h, msg, (DWORD)(sizeof(msg) - 1), &written, NULL);

    ExitProcess(0);
}

