typedef unsigned long dword_t;
typedef unsigned int uint_t;
typedef unsigned short word_t;
typedef unsigned long long socket_t;
typedef void* hwnd_t;
typedef unsigned long wparam_t;
typedef long lparam_t;
typedef long lresult_t;
typedef unsigned long uintptr_t_t;

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

__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) int WriteFile(const void* handle, const void* buffer,
                                         dword_t bytes_to_write, dword_t* bytes_written,
                                         const void* overlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) word_t RegisterClassExA(const wndclass_t* wc);
__attribute__((dllimport)) hwnd_t CreateWindowExA(dword_t ex_style, const char* class_name,
                                                  const char* window_name, dword_t style,
                                                  int x, int y, int width, int height,
                                                  hwnd_t parent, void* menu, void* instance,
                                                  void* param);
__attribute__((dllimport)) int ShowWindow(hwnd_t hwnd, int cmd_show);
__attribute__((dllimport)) int GetMessageA(msg_t* msg, hwnd_t hwnd, uint_t filter_min,
                                           uint_t filter_max);
__attribute__((dllimport)) lresult_t DispatchMessageA(const msg_t* msg);
__attribute__((dllimport)) lresult_t DefWindowProcA(hwnd_t hwnd, uint_t message, wparam_t wparam,
                                                    lparam_t lparam);
__attribute__((dllimport)) int PostMessageA(hwnd_t hwnd, uint_t message, wparam_t wparam,
                                            lparam_t lparam);
__attribute__((dllimport)) int DestroyWindow(hwnd_t hwnd);
__attribute__((dllimport)) void PostQuitMessage(int exit_code);
__attribute__((dllimport)) void* LoadLibraryA(const char* file_name);
__attribute__((dllimport)) void* GetProcAddress(void* module, const char* name);

typedef int (*wsa_startup_fn)(word_t, void*);
typedef int (*wsa_cleanup_fn)(void);
typedef socket_t (*socket_fn)(int, int, int);
typedef int (*closesocket_fn)(socket_t);

static const uint_t kMessageProbe = 0x8001U;
static const char kClassName[] = "tlguinet";
static const char kWindowName[] = "tl generic message probe";

static void fail(const dword_t code) {
    ExitProcess(code);
}

static void run_dynamic_ws2_probe(void) {
    const char library_name[] = "ws2_32.dll";
    const char startup_name[] = "WSAStartup";
    const char cleanup_name[] = "WSACleanup";
    const char socket_name[] = "socket";
    const char close_name[] = "closesocket";
    void* const winsock = LoadLibraryA(library_name);
    if (winsock == (void*)0) fail(10U);

    const wsa_startup_fn startup = (wsa_startup_fn)GetProcAddress(winsock, startup_name);
    const wsa_cleanup_fn cleanup = (wsa_cleanup_fn)GetProcAddress(winsock, cleanup_name);
    const socket_fn open_socket = (socket_fn)GetProcAddress(winsock, socket_name);
    const closesocket_fn close_socket = (closesocket_fn)GetProcAddress(winsock, close_name);
    if (startup == (wsa_startup_fn)0 || cleanup == (wsa_cleanup_fn)0 ||
        open_socket == (socket_fn)0 || close_socket == (closesocket_fn)0) {
        fail(11U);
    }

    unsigned char wsa_data[400] = {0};
    if (startup(0x0202U, wsa_data) != 0) fail(12U);
    const socket_t socket = open_socket(2, 1, 0);
    if (socket == ~0ULL) fail(13U);
    if (close_socket(socket) != 0 || cleanup() != 0) fail(14U);
}

static lresult_t wndproc(hwnd_t hwnd, uint_t message, wparam_t wparam, lparam_t lparam) {
    if (message == kMessageProbe) {
        const char output_text[] = "gui-dynamic-ws2\n";
        void* const output = GetStdHandle((dword_t)-11);
        dword_t written = 0;
        run_dynamic_ws2_probe();
        if (!WriteFile(output, output_text, sizeof(output_text) - 1U, &written, (void*)0) ||
            written != sizeof(output_text) - 1U) {
            fail(15U);
        }
        if (!DestroyWindow(hwnd)) fail(16U);
        return 0;
    }
    if (message == 0x0002U) {
        PostQuitMessage(0);
        return 0;
    }
    (void)wparam;
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
    wc.background = (void*)0;
    wc.menu_name = (const char*)0;
    wc.class_name = kClassName;
    wc.icon_sm = (void*)0;

    if (RegisterClassExA(&wc) == 0U) fail(2U);
    hwnd_t hwnd = CreateWindowExA(0U, kClassName, kWindowName, 0U, 0, 0, 480, 180,
                                  (hwnd_t)0, (void*)0, (void*)0, (void*)0);
    if (hwnd == (hwnd_t)0) fail(3U);
    ShowWindow(hwnd, 1);
    if (!PostMessageA(hwnd, kMessageProbe, 0U, 0L)) fail(4U);

    msg_t msg;
    while (GetMessageA(&msg, (hwnd_t)0, 0U, 0U) > 0) {
        DispatchMessageA(&msg);
    }
    ExitProcess((dword_t)msg.wparam);
}
