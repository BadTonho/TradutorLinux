/* tl_peek.exe: valida PeekMessageA sobre eventos nativos sem removê-los. */
typedef unsigned long dword_t;
typedef unsigned int uint_t;
typedef unsigned short word_t;
typedef unsigned long long wparam_t;
typedef long long lparam_t;
typedef void* hwnd_t;
typedef long long lresult_t;

typedef struct {
    hwnd_t hwnd;
    uint_t message;
    uint_t padding;
    wparam_t wparam;
    lparam_t lparam;
    uint_t time;
    int pt_x;
    int pt_y;
} msg_t;

typedef struct {
    uint_t cb_size;
    uint_t style;
    void* window_proc;
    int class_extra;
    int window_extra;
    void* instance;
    void* icon;
    void* cursor;
    void* background;
    const char* menu_name;
    const char* class_name;
    void* icon_sm;
} wndclass_t;

__attribute__((dllimport)) word_t RegisterClassExA(const wndclass_t* wc);
__attribute__((dllimport)) hwnd_t CreateWindowExA(dword_t ex_style, const char* class_name,
                                                  const char* window_name, dword_t style,
                                                  int x, int y, int width, int height,
                                                  hwnd_t parent, void* menu, void* instance,
                                                  void* param);
__attribute__((dllimport)) int ShowWindow(hwnd_t hwnd, int cmd_show);
__attribute__((dllimport)) int PeekMessageA(msg_t* msg, hwnd_t hwnd, uint_t filter_min,
                                            uint_t filter_max, uint_t remove_msg);
__attribute__((dllimport)) lresult_t DefWindowProcA(hwnd_t hwnd, uint_t message, wparam_t wparam,
                                                    lparam_t lparam);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);

static const char kClassName[] = "tlpeek";
static const char kWindowName[] = "Peek Message";
static int g_flags = 0;

static lresult_t wndproc(hwnd_t hwnd, uint_t message, wparam_t wparam, lparam_t lparam) {
    (void)lparam;
    if (message == 0x0001U) { /* WM_CREATE */
        g_flags |= 1;
        return 0;
    }
    (void)hwnd;
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    wndclass_t wc = {0};
    wc.cb_size = (uint_t)sizeof(wc);
    wc.window_proc = (void*)wndproc;
    wc.class_name = kClassName;
    if (RegisterClassExA(&wc) == 0U) {
        ExitProcess(2U);
    }

    hwnd_t hwnd = CreateWindowExA(0U, kClassName, kWindowName, 0U, 0, 0, 480, 180,
                                  (hwnd_t)0, (void*)0, (void*)0, (void*)0);
    if (hwnd == (hwnd_t)0) {
        ExitProcess(3U);
    }
    ShowWindow(hwnd, 1);

    for (;;) {
        msg_t message;
        if (PeekMessageA(&message, (hwnd_t)0, 0U, 0U, 0U) == 0) {
            continue;
        }
        if (message.message != 0x0100U || message.wparam != (wparam_t)'Q') {
            if (PeekMessageA(&message, (hwnd_t)0, 0U, 0U, 1U) == 0) {
                ExitProcess(4U);
            }
            continue;
        }
        g_flags |= 2;
        if (PeekMessageA(&message, (hwnd_t)0, 0U, 0U, 1U) == 0 ||
            message.message != 0x0100U || message.wparam != (wparam_t)'Q') {
            ExitProcess(5U);
        }
        g_flags |= 4;
        ExitProcess((dword_t)g_flags);
    }
}
