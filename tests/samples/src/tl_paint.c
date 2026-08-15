/* tl_paint.exe: demo interativa de mouse + desenho de formas.
 * Desenha um "botao" com FillRect/Rectangle, pinta textos e reage a
 * WM_LBUTTONDOWN, WM_LBUTTONUP e WM_MOUSEMOVE. Fecha com 'q' (exit code =
 * soma das flags) ou pelo botao de fechar da janela.
 */
typedef unsigned long dword_t;
typedef unsigned int uint_t;
typedef unsigned short word_t;
typedef void* hwnd_t;
typedef void* hdc_t;
typedef void* hbrush_t;
typedef unsigned long wparam_t;
typedef long lparam_t;
typedef long lresult_t;

typedef struct {
    int left;
    int top;
    int right;
    int bottom;
} rect_t;

typedef struct {
    hdc_t hdc;
    int f_erase;
    rect_t rc_paint;
    int f_restore;
    int f_inc_update;
    unsigned char rgb_reserved[32];
} paintstruct_t;

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
__attribute__((dllimport)) void* GetStockObject(int object);
__attribute__((dllimport)) hdc_t BeginPaint(hwnd_t hwnd, paintstruct_t* ps);
__attribute__((dllimport)) int EndPaint(hwnd_t hwnd, const paintstruct_t* ps);
__attribute__((dllimport)) int TextOutA(hdc_t hdc, int x, int y, const char* text, int length);
__attribute__((dllimport)) int FillRect(hdc_t hdc, const rect_t* rect, hbrush_t brush);
__attribute__((dllimport)) int Rectangle(hdc_t hdc, int left, int top, int right, int bottom);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);

#define WHITE_BRUSH 0
#define LTGRAY_BRUSH 1
#define GRAY_BRUSH 2
#define DKGRAY_BRUSH 3
#define BLACK_BRUSH 4

static const char kClassName[] = "tlpaint";
static const char kWindowName[] = "Pinte e Clique";
static const char kInstructions[] = "Clique no botao, mova o mouse. 'q' fecha.";

static const int kButtonLeft = 170;
static const int kButtonTop = 90;
static const int kButtonRight = 310;
static const int kButtonBottom = 130;

/* Flags do exit code: 1 create, 2 paint, 4 down, 8 up, 16 move, 32 click, 64 q. */
static int g_flags = 0;
static int g_clicks = 0;
static int g_mouse_x = 0;
static int g_mouse_y = 0;

static int format_int(char* out, int value) {
    char tmp[16];
    int len = 0;
    const int negative = value < 0;
    unsigned int v;
    if (negative) {
        v = (unsigned int)(-(value + 1)) + 1U;
    } else {
        v = (unsigned int)value;
    }
    do {
        tmp[len++] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v != 0U);
    int i = 0;
    if (negative) {
        out[i++] = '-';
    }
    while (len > 0) {
        out[i++] = tmp[--len];
    }
    out[i] = '\0';
    return i;
}

static void paint(hwnd_t hwnd) {
    paintstruct_t ps;
    hdc_t hdc = BeginPaint(hwnd, &ps);
    if (hdc == (hdc_t)0) {
        return;
    }
    if (ps.hdc == hdc && ps.rc_paint.right >= 480 && ps.rc_paint.bottom >= 260) {
        g_flags |= 2;
    }
    rect_t client = {0, 0, 480, 260};
    rect_t button = {kButtonLeft, kButtonTop, kButtonRight, kButtonBottom};
    char counter[32];
    char position[32];
    char py[16];
    const int counter_len = format_int(counter, g_clicks);
    const int position_len = format_int(position, g_mouse_x);
    const int py_len = format_int(py, g_mouse_y);

    FillRect(hdc, &client, GetStockObject(WHITE_BRUSH));
    TextOutA(hdc, 10, 10, kInstructions, 41);
    TextOutA(hdc, 10, 34, "Cliques:", 8);
    TextOutA(hdc, 90, 34, counter, counter_len);
    TextOutA(hdc, 10, 58, "Mouse:", 6);
    TextOutA(hdc, 68, 58, position, position_len);
    TextOutA(hdc, 108, 58, ",", 1);
    TextOutA(hdc, 116, 58, py, py_len);
    FillRect(hdc, &button, GetStockObject(LTGRAY_BRUSH));
    Rectangle(hdc, kButtonLeft, kButtonTop, kButtonRight, kButtonBottom);
    TextOutA(hdc, 222, 104, "CLIQUE", 6);
    EndPaint(hwnd, &ps);
}

static lresult_t wndproc(hwnd_t hwnd, uint_t message, wparam_t wparam, lparam_t lparam) {
    (void)wparam;
    if (message == 0x0001U) { /* WM_CREATE */
        g_flags |= 1;
        return 0;
    }
    if (message == 0x000FU) { /* WM_PAINT */
        paint(hwnd);
        return 0;
    }
    if (message == 0x0201U) { /* WM_LBUTTONDOWN */
        const int x = (int)(lparam & 0xFFFF);
        const int y = (int)((lparam >> 16) & 0xFFFF);
        g_flags |= 4;
        if (x >= kButtonLeft && x < kButtonRight && y >= kButtonTop && y < kButtonBottom) {
            g_flags |= 32;
            g_clicks++;
        }
        UpdateWindow(hwnd);
        return 0;
    }
    if (message == 0x0202U) { /* WM_LBUTTONUP */
        g_flags |= 8;
        return 0;
    }
    if (message == 0x0200U) { /* WM_MOUSEMOVE */
        const int x = (int)(lparam & 0xFFFF);
        const int y = (int)((lparam >> 16) & 0xFFFF);
        g_flags |= 16;
        g_mouse_x = x;
        g_mouse_y = y;
        UpdateWindow(hwnd);
        return 0;
    }
    if (message == 0x0102U) { /* WM_CHAR */
        if (wparam == (wparam_t)0x71U) { /* 'q' */
            g_flags |= 64;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    if (message == 0x0002U) { /* WM_DESTROY */
        PostQuitMessage(g_flags);
        return 0;
    }
    (void)lparam;
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
    wc.background = GetStockObject(WHITE_BRUSH);
    wc.menu_name = (const char*)0;
    wc.class_name = kClassName;
    wc.icon_sm = (void*)0;

    if (RegisterClassExA(&wc) == 0U) {
        ExitProcess(2U);
    }

    hwnd_t hwnd = CreateWindowExA(0U, kClassName, kWindowName, 0U, 0, 0, 480, 260,
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
