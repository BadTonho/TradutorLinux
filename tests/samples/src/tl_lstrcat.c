typedef unsigned short uint16_t;
typedef unsigned int dword_t;
typedef int bool_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) uint16_t* lstrcatW(uint16_t* destination,
                                               const uint16_t* source);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static int wide_equal(const uint16_t* left, const uint16_t* right) {
    for (dword_t index = 0;; ++index) {
        if (left[index] != right[index]) return 0;
        if (left[index] == 0) return 1;
    }
}

void tl_entry(void) {
    void* const output = GetStdHandle((dword_t)-11);
    dword_t written = 0;
    uint16_t result[32] = {'C', ':', '\\', 0};
    static const uint16_t suffix[] = {'T', 'e', 'm', 'p', 0};
    static const uint16_t expected[] = {'C', ':', '\\', 'T', 'e', 'm', 'p', 0};
    if (output == (void*)0 || lstrcatW(result, suffix) != result ||
        !wide_equal(result, expected)) {
        ExitProcess(1);
    }
    static const char message[] = "lstrcat\n";
    if (!WriteFile(output, message, sizeof(message) - 1U, &written, (void*)0) ||
        written != sizeof(message) - 1U) {
        ExitProcess(2);
    }
    ExitProcess(0);
}
