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
    const word_t* menu_name;
    const word_t* class_name;
    void* icon_sm;
} wndclassW_t;

__attribute__((dllimport)) word_t RegisterClassExW(const wndclassW_t* wc);
__attribute__((dllimport)) hwnd_t CreateWindowExW(dword_t ex_style, const word_t* class_name,
                                                  const word_t* window_name, dword_t style,
                                                  int x, int y, int width, int height,
                                                  hwnd_t parent, void* menu, void* instance,
                                                  void* param);
__attribute__((dllimport)) int ShowWindow(hwnd_t hwnd, int cmd_show);
__attribute__((dllimport)) int UpdateWindow(hwnd_t hwnd);
__attribute__((dllimport)) int GetMessageW(msg_t* msg, hwnd_t hwnd, uint_t filter_min,
                                           uint_t filter_max);
__attribute__((dllimport)) int TranslateMessage(const msg_t* msg);
__attribute__((dllimport)) lresult_t DispatchMessageW(const msg_t* msg);
__attribute__((dllimport)) lresult_t DefWindowProcW(hwnd_t hwnd, uint_t message, wparam_t wparam,
                                                    lparam_t lparam);
__attribute__((dllimport)) int DestroyWindow(hwnd_t hwnd);
__attribute__((dllimport)) void PostQuitMessage(int exit_code);
__attribute__((dllimport)) int SetWindowTextW(hwnd_t hwnd, const word_t* text);
__attribute__((dllimport)) int GetWindowTextW(hwnd_t hwnd, word_t* text, int capacity);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);

static const word_t kClassNameW[] = {'t','l','w','i','n','w',0};
static const word_t kWindowNameW[] = {'O','l','a',' ','W',0};

static int g_wm_create_seen = 0;

static lresult_t wndproc(hwnd_t hwnd, uint_t message, wparam_t wparam, lparam_t lparam) {
    (void)hwnd;
    (void)lparam;
    if (message == 0x0001U) {
        g_wm_create_seen = 1;
        return 0;
    }
    if (message == 0x0102U) {
        if (wparam == (wparam_t)0x71U) {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    if (message == 0x0002U) {
        PostQuitMessage(g_wm_create_seen);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    wndclassW_t wc;
    wc.cb_size = (uint_t)sizeof(wc);
    wc.style = 0U;
    wc.window_proc = (void*)wndproc;
    wc.class_extra = 0;
    wc.window_extra = 0;
    wc.instance = (void*)0;
    wc.icon = (void*)0;
    wc.cursor = (void*)0;
    wc.background = (void*)0;
    wc.menu_name = (const word_t*)0;
    wc.class_name = kClassNameW;
    wc.icon_sm = (void*)0;

    if (RegisterClassExW(&wc) == 0U) {
        ExitProcess(2U);
    }

    hwnd_t hwnd = CreateWindowExW(0U, kClassNameW, kWindowNameW, 0U, 0, 0, 480, 180,
                                  (hwnd_t)0, (void*)0, (void*)0, (void*)0);
    if (hwnd == (hwnd_t)0) {
        ExitProcess(3U);
    }

    // Test Set/GetWindowTextW
    const word_t kNewTitle[] = {'N','e','w',0};
    if (!SetWindowTextW(hwnd, kNewTitle)) ExitProcess(4U);
    word_t buf[16] = {0};
    int len = GetWindowTextW(hwnd, buf, 16);
    if (len != 3) ExitProcess(5U);
    if (buf[0] != 'N' || buf[1] != 'e' || buf[2] != 'w') ExitProcess(6U);

    ShowWindow(hwnd, 1);
    UpdateWindow(hwnd);

    msg_t msg;
    while (GetMessageW(&msg, (hwnd_t)0, 0U, 0U) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    ExitProcess(msg.wparam);
}
