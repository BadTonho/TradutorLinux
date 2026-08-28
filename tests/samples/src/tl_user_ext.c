typedef unsigned char byte_t;
typedef unsigned int dword_t;
typedef int bool_t;
typedef short int16_t;
typedef int int32_t;
typedef unsigned short uint16_t;
typedef unsigned long long uint64_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) dword_t GetLastError(void);

__attribute__((dllimport)) void* GetDesktopWindow(void);
__attribute__((dllimport)) void* GetFocus(void);
__attribute__((dllimport)) void* SetCapture(void* window);
__attribute__((dllimport)) bool_t ReleaseCapture(void);
__attribute__((dllimport)) void* GetCapture(void);
__attribute__((dllimport)) dword_t GetWindowThreadProcessId(void* window, dword_t* process_id);
__attribute__((dllimport)) bool_t PtInRect(const void* rect, int32_t x, int32_t y);
__attribute__((dllimport)) bool_t CopyRect(void* dest, const void* src);
__attribute__((dllimport)) int MapWindowPoints(void* from, void* to, void* points, dword_t count);
__attribute__((dllimport)) dword_t GetSysColor(int index);
__attribute__((dllimport)) uint16_t* CharUpperW(uint16_t* str);
__attribute__((dllimport)) int DrawTextW(void* dc, const uint16_t* text, int count, void* rect, dword_t format);

struct rect_t {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

struct point_t {
    int32_t x;
    int32_t y;
};

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static void fail(const void* output, dword_t* bytes_written, dword_t code) {
    static const char message[] = "FAIL\n";
    (void)WriteFile(output, message, sizeof(message) - 1, bytes_written, (void*)0);
    ExitProcess(code);
}

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t bytes_written = 0;

    // 1. GetDesktopWindow & Process/Thread ID
    (void)GetLastError();
    (void)GetFocus();
    void* desktop = GetDesktopWindow();
    if (!desktop) {
        fail(output, &bytes_written, 1);
    }
    dword_t pid = 0;
    dword_t tid = GetWindowThreadProcessId(desktop, &pid);
    if (tid == 0 || pid == 0) {
        fail(output, &bytes_written, 2);
    }

    // 2. Capture APIs
    (void)SetCapture((void*)0);
    if (GetCapture() != (void*)0) {
        fail(output, &bytes_written, 3);
    }
    if (!ReleaseCapture()) {
        fail(output, &bytes_written, 4);
    }

    // 3. Rect & Point operations
    struct rect_t r1 = {10, 20, 100, 200};
    if (!PtInRect(&r1, 50, 50)) {
        fail(output, &bytes_written, 5);
    }
    if (PtInRect(&r1, 5, 50) || PtInRect(&r1, 150, 50)) {
        fail(output, &bytes_written, 6);
    }

    struct rect_t r2 = {0, 0, 0, 0};
    if (!CopyRect(&r2, &r1) || r2.left != 10 || r2.bottom != 200) {
        fail(output, &bytes_written, 7);
    }

    struct point_t pt = {5, 10};
    (void)MapWindowPoints((void*)0, (void*)0, &pt, 1);
    if (pt.x != 5 || pt.y != 10) {
        fail(output, &bytes_written, 8);
    }

    // 4. SysColor
    dword_t win_color = GetSysColor(5); // COLOR_WINDOW
    if (win_color != 0x00FFFFFFU) {
        fail(output, &bytes_written, 9);
    }

    // 5. CharUpperW
    uint16_t str[] = {'h', 'e', 'l', 'l', 'o', 0};
    uint16_t expected[] = {'H', 'E', 'L', 'L', 'O', 0};
    (void)CharUpperW(str);
    for (int i = 0; i < 5; ++i) {
        if (str[i] != expected[i]) {
            fail(output, &bytes_written, 10);
        }
    }

    // 6. DrawTextW (DT_CALCRECT = 0x400)
    struct rect_t calc_r = {0, 0, 0, 0};
    int height = DrawTextW((void*)0, str, 5, &calc_r, 0x00000400U);
    if (height <= 0 || calc_r.right <= 0 || calc_r.bottom <= 0) {
        fail(output, &bytes_written, 11);
    }

    static const char result[] = "userext\n";
    if (!WriteFile(output, result, sizeof(result) - 1, &bytes_written, (void*)0) ||
        bytes_written != sizeof(result) - 1) {
        ExitProcess(12);
    }
    ExitProcess(0);
}

