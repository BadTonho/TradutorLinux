#include <windows.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <dwmapi.h>

void tl_entry(void) {
    HANDLE stdout_h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdout_h == INVALID_HANDLE_VALUE) {
        ExitProcess(1);
    }

    // DWMAPI
    BOOL dwm_enabled = FALSE;
    DwmIsCompositionEnabled(&dwm_enabled);
    DwmSetWindowAttribute(NULL, 0, NULL, 0);

    // UxTheme
    HTHEME theme = OpenThemeData(NULL, L"EDIT");
    if (theme != NULL) {
        DrawThemeBackground(theme, NULL, 0, 0, NULL, NULL);
        CloseThemeData(theme);
    }

    // COMCTL32
    InitCommonControls();
    HIMAGELIST himl = ImageList_Create(16, 16, 0, 1, 1);
    if (himl != NULL) {
        ImageList_Destroy(himl);
    }

    // USER32 & GDI32
    SetProcessDPIAware();
    HBRUSH brush = CreateHatchBrush(HS_HORIZONTAL, RGB(255, 0, 0));
    if (brush != NULL) {
        DeleteObject(brush);
    }
    HRGN rgn = CreatePolygonRgn(NULL, 0, 0);
    if (rgn != NULL) {
        DeleteObject(rgn);
    }

    const char msg[] = "NOTEPAD++ APIS OK\n";
    DWORD written = 0;
    WriteFile(stdout_h, msg, (DWORD)(sizeof(msg) - 1), &written, NULL);

    ExitProcess(0);
}

