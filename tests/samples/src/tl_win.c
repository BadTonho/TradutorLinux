typedef unsigned long dword_t;
typedef unsigned int uint_t;
typedef unsigned short word_t;
typedef void* hwnd_t;
typedef unsigned long wparam_t;
typedef long lparam_t;
typedef long lresult_t;

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
__attribute__((dllimport)) int UpdateWindow(hwnd_t hwnd);
__attribute__((dllimport)) int GetMessageA(msg_t* msg, hwnd_t hwnd, uint_t filter_min,
                                           uint_t filter_max);
__attribute__((dllimport)) int TranslateMessage(const msg_t* msg);
__attribute__((dllimport)) lresult_t DispatchMessageA(const msg_t* msg);
__attribute__((dllimport)) lresult_t DefWindowProcA(hwnd_t hwnd, uint_t message, wparam_t wparam,
                                                    lparam_t lparam);
__attribute__((dllimport)) int DestroyWindow(hwnd_t hwnd);
__attribute__((dllimport)) void PostQuitMessage(int exit_code);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);

static const char kClassName[] = "tlwin";
static const char kWindowName[] = "Ola do Windows no Linux!";

static lresult_t wndproc(hwnd_t hwnd, uint_t message, wparam_t wparam, lparam_t lparam) {
    (void)hwnd;
    (void)wparam;
    (void)lparam;
    if (message == 0x0002U) { /* WM_DESTROY */
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    wndclass_t wc;
    wc.cb_size = (uint_t)sizeof(wc);
    wc.style = 0U;
    wc.window_proc = (void*)wndproc;
    wc.class_extra = 0;
    wc.window_extra = 0;
    wc.instance = (void*)0;
    wc.icon = (void*)0;
    wc.cursor = (void*)0;
    wc.background = (void*)0;
    wc.menu_name = (const char*)0;
    wc.class_name = kClassName;
    wc.icon_sm = (void*)0;

    if (RegisterClassExA(&wc) == 0U) {
        ExitProcess(2U);
    }

    hwnd_t hwnd = CreateWindowExA(0U, kClassName, kWindowName, 0U, 0, 0, 480, 180,
                                  (hwnd_t)0, (void*)0, (void*)0, (void*)0);
    if (hwnd == (hwnd_t)0) {
        ExitProcess(3U);
    }

    ShowWindow(hwnd, 1);
    UpdateWindow(hwnd);

    msg_t msg;
    while (GetMessageA(&msg, (hwnd_t)0, 0U, 0U) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    ExitProcess(msg.wparam);
}