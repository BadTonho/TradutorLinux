#include <windows.h>

static const char kClassName[] = "TlProtonInputProbeWindow";
static const char kWindowTitle[] = "TradutorLinux Proton Input Probe";
static const char kSuccessMessage[] = "Proton input ready\n";

enum {
    kCreateSeen = 1U,
    kKeyDownSeen = 2U,
    kCharSeen = 4U,
    kKeyUpSeen = 8U,
    kMouseMoveSeen = 16U,
    kButtonDownSeen = 32U,
    kButtonUpSeen = 64U,
    kExpectedFlags = 127U,
};

static DWORD g_event_flags = 0U;

static void write_message(const char* const message, const DWORD length) {
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    (void)WriteFile(output, message, length, &written, NULL);
}

static LRESULT CALLBACK input_window_proc(const HWND window, const UINT message,
                                          const WPARAM wparam, const LPARAM lparam) {
    (void)lparam;
    switch (message) {
        case WM_CREATE:
            g_event_flags |= kCreateSeen;
            return 0;
        case WM_MOUSEMOVE:
            g_event_flags |= kMouseMoveSeen;
            return 0;
        case WM_LBUTTONDOWN:
            g_event_flags |= kButtonDownSeen;
            return 0;
        case WM_LBUTTONUP:
            g_event_flags |= kButtonUpSeen;
            return 0;
        case WM_KEYDOWN:
            if (wparam == (WPARAM)'Q') g_event_flags |= kKeyDownSeen;
            return 0;
        case WM_CHAR:
            if (wparam == (WPARAM)'q') g_event_flags |= kCharSeen;
            return 0;
        case WM_KEYUP:
            if (wparam == (WPARAM)'Q') {
                g_event_flags |= kKeyUpSeen;
                (void)DestroyWindow(window);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage((int)g_event_flags);
            return 0;
        default:
            return DefWindowProcA(window, message, wparam, lparam);
    }
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    const HINSTANCE instance = GetModuleHandleA(NULL);
    if (instance == NULL) {
        ExitProcess(1U);
    }
    WNDCLASSEXA window_class = {0};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = input_window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = kClassName;
    if (RegisterClassExA(&window_class) == 0U) {
        ExitProcess(2U);
    }

    const HWND window = CreateWindowExA(0U, kClassName, kWindowTitle,
                                        WS_OVERLAPPEDWINDOW, 0, 0, 240, 140,
                                        NULL, NULL, instance, NULL);
    if (window == NULL) {
        (void)UnregisterClassA(kClassName, instance);
        ExitProcess(3U);
    }
    (void)ShowWindow(window, SW_SHOW);
    (void)UpdateWindow(window);

    MSG message = {0};
    int result = 0;
    while ((result = GetMessageA(&message, NULL, 0U, 0U)) > 0) {
        (void)TranslateMessage(&message);
        (void)DispatchMessageA(&message);
    }

    (void)UnregisterClassA(kClassName, instance);
    if (result < 0 || (DWORD)message.wParam != kExpectedFlags) {
        ExitProcess(10U + (g_event_flags & 0x7FU));
    }
    write_message(kSuccessMessage, (DWORD)(sizeof(kSuccessMessage) - 1U));
    ExitProcess(0U);
}
