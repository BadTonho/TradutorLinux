typedef unsigned short word_t;
typedef unsigned long dword_t;
typedef unsigned long long wparam_t;
typedef long long lparam_t;
typedef long long lresult_t;
typedef void* hwnd_t;
typedef int bool_t;

typedef struct {
    hwnd_t hwnd;
    dword_t message;
    dword_t padding;
    wparam_t wparam;
    lparam_t lparam;
    dword_t time;
    int pt_x;
    int pt_y;
} msg_t;

typedef struct {
    dword_t size;
    dword_t classes;
} init_common_controls_t;

typedef struct {
    int left;
    int top;
    int right;
    int bottom;
} rect_t;

__attribute__((dllimport)) void* GetModuleHandleW(const word_t* name);
__attribute__((dllimport)) lresult_t DialogBoxParamW(const void* instance, const word_t* name,
                                                     hwnd_t parent, void* proc,
                                                     lparam_t parameter);
__attribute__((dllimport)) bool_t EndDialog(hwnd_t dialog, lparam_t result);
__attribute__((dllimport)) hwnd_t GetDlgItem(hwnd_t dialog, int identifier);
__attribute__((dllimport)) bool_t SetDlgItemTextW(hwnd_t dialog, int identifier,
                                                  const word_t* text);
__attribute__((dllimport)) lresult_t SendDlgItemMessageW(hwnd_t dialog, int identifier,
                                                         dword_t message, wparam_t wparam,
                                                         lparam_t lparam);
__attribute__((dllimport)) hwnd_t GetNextDlgTabItem(hwnd_t dialog, hwnd_t control, int previous);
__attribute__((dllimport)) bool_t GetWindowRect(hwnd_t window, rect_t* rect);
__attribute__((dllimport)) int GetWindowLongW(hwnd_t window, int index);
__attribute__((dllimport)) int SetWindowLongW(hwnd_t window, int index, int value);
__attribute__((dllimport)) void* LoadIconW(const void* instance, const word_t* name);
__attribute__((dllimport)) void* CopyImage(const void* image, dword_t type, int width, int height,
                                            dword_t flags);
__attribute__((dllimport)) bool_t DestroyIcon(const void* icon);
__attribute__((dllimport)) bool_t InitCommonControlsEx(const init_common_controls_t* controls);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* written,
                                             const void* overlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t code);

static const word_t* const kTemplate = (const word_t*)(unsigned long long)101U;
static const word_t kValue[] = {'f', 'i', 'x', 't', 'u', 'r', 'e', 0};
static const word_t kEdited[] = {'e', 'd', 'i', 't', 'e', 'd', 0};
static const char kDialogOutput[] = {'d', 'i', 'a', 'l', 'o', 'g', '\n', 0};

static int g_init_seen;
static int g_checks_ok;

static lresult_t dialog_proc(hwnd_t dialog, dword_t message, wparam_t wparam,
                             lparam_t lparam) {
    (void)lparam;
    if (message == 0x0110U) { /* WM_INITDIALOG */
        hwnd_t edit = GetDlgItem(dialog, 100);
        rect_t rect;
        init_common_controls_t common = {8U, 0x4000U};
        void* icon = LoadIconW((const void*)0, (const word_t*)0);
        void* copied = CopyImage(icon, 1U, 0, 0, 0U);
        g_init_seen = edit != (hwnd_t)0 && GetNextDlgTabItem(dialog, (hwnd_t)0, 0) == edit &&
                      SetDlgItemTextW(dialog, 100, kValue) != 0 &&
                      SendDlgItemMessageW(dialog, 100, 0x000CU, 0, (lparam_t)kEdited) != 0 &&
                      GetWindowRect(edit, &rect) != 0 && GetWindowLongW(edit, -12) == 100 &&
                      SetWindowLongW(edit, -21, 0x1234) == 0 &&
                      GetWindowLongW(edit, -21) == 0x1234 &&
                      InitCommonControlsEx(&common) != 0 && copied != (void*)0 &&
                      DestroyIcon(copied) != 0;
        g_checks_ok = g_init_seen;
        return 1;
    }
    if (message == 0x0111U && (wparam >> 16U) == 0U &&
        (wparam & 0xFFFFU) == 1U) { /* WM_COMMAND/BN_CLICKED/IDOK */
        if (g_init_seen) {
            EndDialog(dialog, 42);
        }
        return 1;
    }
    return 0;
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    const lresult_t result = DialogBoxParamW(GetModuleHandleW((const word_t*)0), kTemplate,
                                             (hwnd_t)0, (void*)dialog_proc, 0);
    if (result != 42 || !g_checks_ok) {
        ExitProcess(1U);
    }
    void* const output = GetStdHandle((dword_t)-11);
    dword_t written = 0;
    if (!WriteFile(output, kDialogOutput, 7U, &written, (const void*)0) || written != 7U) {
        ExitProcess(2U);
    }
    ExitProcess(0U);
}
