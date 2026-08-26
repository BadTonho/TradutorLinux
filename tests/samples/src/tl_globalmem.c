typedef unsigned int dword_t;
typedef int bool_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) dword_t GetLastError(void);
__attribute__((dllimport)) void* GlobalAlloc(dword_t flags, dword_t bytes);
__attribute__((dllimport)) void* GlobalLock(void* memory);
__attribute__((dllimport)) bool_t GlobalUnlock(void* memory);
__attribute__((dllimport)) void* GlobalFree(void* memory);
__attribute__((dllimport)) void* LocalAlloc(dword_t flags, dword_t bytes);
__attribute__((dllimport)) void* LocalFree(void* memory);

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
    void* global = GlobalAlloc(0x00000042U, 32);
    if (global == (void*)0) {
        fail(output, &bytes_written, 1);
    }
    unsigned char* global_bytes = (unsigned char*)GlobalLock(global);
    if (global_bytes == (unsigned char*)0) {
        fail(output, &bytes_written, 2);
    }
    for (dword_t index = 0; index < 32; ++index) {
        if (global_bytes[index] != 0) {
            fail(output, &bytes_written, 3);
        }
    }
    global_bytes[0] = 0x5A;
    if (GlobalUnlock(global) || GetLastError() != 0U || GlobalFree(global) != (void*)0) {
        fail(output, &bytes_written, 4);
    }

    void* fixed = GlobalAlloc(0, 8);
    if (fixed == (void*)0 || GlobalLock(fixed) != fixed || GlobalUnlock(fixed) != 0U ||
        GlobalFree(fixed) != (void*)0) {
        fail(output, &bytes_written, 5);
    }
    if (GlobalAlloc(0x8000U, 8) != (void*)0 || GetLastError() != 87U) {
        fail(output, &bytes_written, 6);
    }

    void* local = LocalAlloc(0x00000040U, 16);
    if (local == (void*)0) {
        fail(output, &bytes_written, 7);
    }
    unsigned char* local_bytes = (unsigned char*)local;
    for (dword_t index = 0; index < 16; ++index) {
        if (local_bytes[index] != 0) {
            fail(output, &bytes_written, 8);
        }
    }
    if (LocalFree(local) != (void*)0) {
        fail(output, &bytes_written, 9);
    }

    static const char result[] = "globalmem\n";
    if (!WriteFile(output, result, sizeof(result) - 1, &bytes_written, (void*)0) ||
        bytes_written != sizeof(result) - 1) {
        ExitProcess(10);
    }
    ExitProcess(0);
}
