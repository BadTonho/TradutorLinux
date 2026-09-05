typedef unsigned long dword_t;
typedef int bool_t;

__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);

#ifndef COMPAT_LABEL
#define COMPAT_LABEL "compat dll\n"
#endif

__attribute__((dllexport)) int CompatEntry(void) {
    static const char payload[] = COMPAT_LABEL;
    dword_t written = 0;
    void* const output = GetStdHandle((dword_t)-11);
    return output != (void*)0 &&
                   WriteFile(output, payload, sizeof(payload) - 1U, &written, (void*)0) &&
                   written == sizeof(payload) - 1U
               ? 1
               : 0;
}

int DllMain(void* module, dword_t reason, void* reserved) {
    (void)module;
    (void)reason;
    (void)reserved;
    return 1;
}
