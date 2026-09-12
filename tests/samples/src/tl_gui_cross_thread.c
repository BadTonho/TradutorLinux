typedef unsigned long dword_t;
typedef unsigned int uint_t;
typedef unsigned short word_t;
typedef unsigned long long wparam_t;
typedef long long lparam_t;
typedef void* hwnd_t;
typedef long long lresult_t;
typedef unsigned long long size_t_guest;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) int WriteFile(const void* handle, const void* buffer,
                                         dword_t bytes_to_write, dword_t* bytes_written,
                                         const void* overlapped);
__attribute__((dllimport)) void* CreateThread(void* security_attributes,
                                               size_t_guest stack_size,
                                               void* start_address, void* parameter,
                                               dword_t creation_flags, dword_t* thread_id);
__attribute__((dllimport)) dword_t WaitForSingleObject(void* handle, dword_t milliseconds);
__attribute__((dllimport)) int CloseHandle(void* handle);
__attribute__((dllimport)) word_t RegisterClassExA(const void* wnd_class);
__attribute__((dllimport)) hwnd_t CreateWindowExA(dword_t ex_style, const char* class_name,
                                                  const char* window_name, dword_t style,
                                                  int x, int y, int width, int height,
                                                  hwnd_t parent, void* menu, void* instance,
                                                  void* param);
__attribute__((dllimport)) int ShowWindow(hwnd_t window, int command);
__attribute__((dllimport)) int GetMessageA(void* message, hwnd_t window, uint_t filter_min,
                                           uint_t filter_max);
__attribute__((dllimport)) lresult_t DispatchMessageA(const void* message);
__attribute__((dllimport)) lresult_t DefWindowProcA(hwnd_t window, uint_t message,
                                                    wparam_t wparam, lparam_t lparam);
__attribute__((dllimport)) int PostMessageA(hwnd_t window, uint_t message, wparam_t wparam,
                                            lparam_t lparam);
__attribute__((dllimport)) int DestroyWindow(hwnd_t window);
__attribute__((dllimport)) void PostQuitMessage(int exit_code);

typedef struct {
    hwnd_t hwnd;
    uint_t message;
    uint_t padding;
    wparam_t wparam;
    lparam_t lparam;
    uint_t time;
    int point_x;
    int point_y;
} guest_msg_t;

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

static const uint_t kMessageCrossThread = 0x8002U;
static const char kClassName[] = "tlcrosspost";
static const char kWindowName[] = "tl cross-thread PostMessage";
static hwnd_t g_window;

static dword_t post_from_worker(void* parameter) {
    (void)parameter;
    return PostMessageA(g_window, kMessageCrossThread, 0U, 0L) ? 0U : 1U;
}

static lresult_t wndproc(hwnd_t window, uint_t message, wparam_t wparam, lparam_t lparam) {
    if (message == kMessageCrossThread) {
        void* const output = GetStdHandle((dword_t)-11);
        static const char text[] = "cross-thread-post\n";
        dword_t written = 0;
        if (output == (void*)0 ||
            !WriteFile(output, text, sizeof(text) - 1U, &written, (void*)0) ||
            written != sizeof(text) - 1U) {
            ExitProcess(2U);
        }
        DestroyWindow(window);
        return 0;
    }
    if (message == 2U) {  /* WM_DESTROY */
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    wndclass_t window_class = {
        sizeof(wndclass_t), 0U, (void*)wndproc, 0, 0, (void*)0, (void*)0, (void*)0,
        (void*)0, (const char*)0, kClassName, (void*)0};
    if (RegisterClassExA(&window_class) == 0U) ExitProcess(1U);
    g_window = CreateWindowExA(0U, kClassName, kWindowName, 0U, 0, 0, 480, 180,
                               (hwnd_t)0, (void*)0, (void*)0, (void*)0);
    if (g_window == (hwnd_t)0) ExitProcess(1U);
    ShowWindow(g_window, 1);

    void* const thread = CreateThread((void*)0, 0U, (void*)post_from_worker, (void*)0, 0U,
                                      (dword_t*)0);
    if (thread == (void*)0 || WaitForSingleObject(thread, 0xFFFFFFFFU) != 0U) {
        ExitProcess(3U);
    }
    CloseHandle(thread);

    guest_msg_t message;
    while (GetMessageA(&message, (hwnd_t)0, 0U, 0U) > 0) {
        DispatchMessageA(&message);
    }
    ExitProcess((dword_t)message.wparam);
}
