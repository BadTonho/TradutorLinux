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

static const char kClassA[] = "tlwin2a";
static const char kClassB[] = "tlwin2b";
static const char kWindowNameA[] = "Janela A";
static const char kWindowNameB[] = "Janela B";

static int g_a_create = 0;
static int g_a_q = 0;
static int g_a_destroy = 0;
static int g_b_create = 0;
static int g_b_k = 0;
static int g_b_destroy = 0;

static void maybe_finish(void) {
    if (g_a_destroy != 0 && g_b_destroy != 0) {
        int code = g_a_create;
        if (g_a_q != 0) {
            code += 2;
        }
        if (g_b_create != 0) {
            code += 4;
        }
        if (g_b_k != 0) {
            code += 8;
        }
        PostQuitMessage(code);
    }
}

static lresult_t wndproc_a(hwnd_t hwnd, uint_t message, wparam_t wparam, lparam_t lparam) {
    (void)lparam;
    if (message == 0x0001U) { /* WM_CREATE */
        g_a_create = 1;
        return 0;
    }
    if (message == 0x0102U) { /* WM_CHAR */
        if (wparam == (wparam_t)0x71U) { /* 'q' */
            g_a_q = 1;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    if (message == 0x0002U) { /* WM_DESTROY */
        g_a_destroy = 1;
        maybe_finish();
        return 0;
    }
    (void)wparam;
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

static lresult_t wndproc_b(hwnd_t hwnd, uint_t message, wparam_t wparam, lparam_t lparam) {
    (void)lparam;
    if (message == 0x0001U) { /* WM_CREATE */
        g_b_create = 1;
        return 0;
    }
    if (message == 0x0102U) { /* WM_CHAR */
        if (wparam == (wparam_t)0x6BU) { /* 'k' */
            g_b_k = 1;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    if (message == 0x0002U) { /* WM_DESTROY */
        g_b_destroy = 1;
        maybe_finish();
        return 0;
    }
    (void)wparam;
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

static int register_class(const char* class_name, void* wndproc) {
    wndclass_t wc;
    wc.cb_size = (uint_t)sizeof(wc);
    wc.style = 0U;
    wc.window_proc = wndproc;
    wc.class_extra = 0;
    wc.window_extra = 0;
    wc.instance = (void*)0;
    wc.icon = (void*)0;
    wc.cursor = (void*)0;
    wc.background = (void*)0;
    wc.menu_name = (const char*)0;
    wc.class_name = class_name;
    wc.icon_sm = (void*)0;
    return (int)RegisterClassExA(&wc);
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    if (register_class(kClassA, (void*)wndproc_a) == 0) {
        ExitProcess(2U);
    }
    if (register_class(kClassB, (void*)wndproc_b) == 0) {
        ExitProcess(3U);
    }

    hwnd_t hwnd_a = CreateWindowExA(0U, kClassA, kWindowNameA, 0U, 10, 10, 240, 120,
                                    (hwnd_t)0, (void*)0, (void*)0, (void*)0);
    if (hwnd_a == (hwnd_t)0) {
        ExitProcess(4U);
    }
    hwnd_t hwnd_b = CreateWindowExA(0U, kClassB, kWindowNameB, 0U, 280, 10, 240, 120,
                                    (hwnd_t)0, (void*)0, (void*)0, (void*)0);
    if (hwnd_b == (hwnd_t)0) {
        ExitProcess(5U);
    }

    ShowWindow(hwnd_a, 1);
    ShowWindow(hwnd_b, 1);
    UpdateWindow(hwnd_a);
    UpdateWindow(hwnd_b);

    msg_t msg;
    while (GetMessageA(&msg, (hwnd_t)0, 0U, 0U) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    ExitProcess(msg.wparam);
}
