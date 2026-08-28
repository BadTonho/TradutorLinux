#include <windows.h>
#include <psapi.h>

int main(void) {
    HBITMAP hbmp = CreateBitmap(100, 100, 1, 32, NULL);
    if (hbmp == NULL) {
        ExitProcess(1);
    }

    BITMAP bmp;
    int res = GetObjectW(hbmp, sizeof(bmp), &bmp);
    if (res <= 0) {
        ExitProcess(2);
    }

    StretchBlt(NULL, 0, 0, 10, 10, NULL, 0, 0, 10, 10, SRCCOPY);

    wchar_t mod_name[MAX_PATH] = {0};
    K32GetModuleFileNameExW(GetCurrentProcess(), NULL, mod_name, MAX_PATH);

    // Test CreateHardLinkW with dummy params (expect 0 error code or success)
    CreateHardLinkW(L"non_existent_target_link.txt", L"non_existent_source.txt", NULL);

    HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdout_handle == NULL || stdout_handle == INVALID_HANDLE_VALUE) {
        ExitProcess(3);
    }

    const char output[] = "k32gdilink\n";
    DWORD written = 0;
    if (!WriteFile(stdout_handle, output, (DWORD)(sizeof(output) - 1), &written, NULL)) {
        ExitProcess(4);
    }

    ExitProcess(0);
    return 0;
}

