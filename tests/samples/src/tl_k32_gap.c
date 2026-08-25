typedef unsigned int dword_t;
typedef int bool_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) dword_t GetLastError(void);
__attribute__((dllimport)) bool_t InitializeCriticalSectionAndSpinCount(void* critical_section,
                                                                          dword_t spin_count);
__attribute__((dllimport)) bool_t InitializeCriticalSectionEx(void* critical_section,
                                                                dword_t spin_count,
                                                                dword_t flags);
__attribute__((dllimport)) void EnterCriticalSection(void* critical_section);
__attribute__((dllimport)) void LeaveCriticalSection(void* critical_section);
__attribute__((dllimport)) void DeleteCriticalSection(void* critical_section);
__attribute__((dllimport)) bool_t AreFileApisANSI(void);
__attribute__((dllimport)) dword_t FormatMessageA(dword_t flags, const void* source,
                                                   dword_t message_id, dword_t language_id,
                                                   char* buffer, dword_t size,
                                                   const void* arguments);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static dword_t string_length(const char* value) {
    dword_t length = 0;
    while (value[length] != '\0') {
        ++length;
    }
    return length;
}

static int string_equal(const char* left, const char* right) {
    dword_t index = 0;
    for (;;) {
        if (left[index] != right[index]) {
            return 0;
        }
        if (left[index] == '\0') {
            return 1;
        }
        ++index;
    }
}

static void fail(const void* output, dword_t* bytes_written, dword_t code) {
    static const char message[] = "FAIL\n";
    (void)WriteFile(output, message, sizeof(message) - 1, bytes_written, (void*)0);
    ExitProcess(code);
}

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t bytes_written = 0;
    unsigned char first[64] __attribute__((aligned(8))) = {0};
    unsigned char second[64] __attribute__((aligned(8))) = {0};

    if (!InitializeCriticalSectionAndSpinCount(first, 4000)) {
        fail(output, &bytes_written, 1);
    }
    EnterCriticalSection(first);
    LeaveCriticalSection(first);
    DeleteCriticalSection(first);

    if (!InitializeCriticalSectionEx(second, 4000, 0x01000000U)) {
        fail(output, &bytes_written, 2);
    }
    EnterCriticalSection(second);
    LeaveCriticalSection(second);
    DeleteCriticalSection(second);

    if (InitializeCriticalSectionEx(second, 0, 1) || GetLastError() != 87U) {
        fail(output, &bytes_written, 3);
    }
    if (!AreFileApisANSI()) {
        fail(output, &bytes_written, 4);
    }

    static const dword_t kFromSystem = 0x1000U;
    char small[4] = {0};
    if (FormatMessageA(kFromSystem, (const void*)0, 2, 0, small, sizeof(small), (const void*)0) != 0 ||
        GetLastError() != 122U) {
        fail(output, &bytes_written, 5);
    }

    static const char expected[] = "The system cannot find the file specified.";
    char message[128] = {0};
    const dword_t written = FormatMessageA(kFromSystem, (const void*)0, 2, 0,
                                           message, sizeof(message), (const void*)0);
    if (written != string_length(expected) || !string_equal(message, expected)) {
        fail(output, &bytes_written, 6);
    }

    static const char result[] = "k32-gap\n";
    if (!WriteFile(output, result, sizeof(result) - 1, &bytes_written, (const void*)0) ||
        bytes_written != sizeof(result) - 1) {
        ExitProcess(7);
    }
    ExitProcess(0);
}
