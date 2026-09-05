typedef unsigned long dword_t;
typedef unsigned long long qword_t;
typedef int bool_t;

__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) int CompatDependencyEntry(void);

typedef void (*tls_callback_t)(void* module, dword_t reason, void* reserved);
typedef struct {
    qword_t start;
    qword_t end;
    qword_t index;
    qword_t callbacks;
    dword_t zero_fill;
    dword_t characteristics;
} tls_directory_t;

__attribute__((section(".tls$AAA"), used)) const unsigned char compat_tls_start = 0;
__attribute__((section(".tls$ZZZ"), used)) const unsigned char compat_tls_end = 0;
unsigned int _tls_index;

static void compat_tls_callback(void* module, dword_t reason, void* reserved) {
    (void)module;
    (void)reason;
    (void)reserved;
}

__attribute__((section(".CRT$XLB"), used))
const tls_callback_t compat_tls_callbacks[] = {compat_tls_callback, (tls_callback_t)0};

__attribute__((section(".rdata$T"), used))
const tls_directory_t _tls_used = {
    (qword_t)&compat_tls_start, (qword_t)&compat_tls_end, (qword_t)&_tls_index,
    (qword_t)&compat_tls_callbacks, 0, 0};

#ifndef COMPAT_LABEL
#define COMPAT_LABEL "compat dll\n"
#endif

__attribute__((dllexport)) int CompatEntry(void) {
    static const char payload[] = COMPAT_LABEL;
    dword_t written = 0;
    if (CompatDependencyEntry() != 1) return 0;
    void* const output = GetStdHandle((dword_t)-11);
    return output != (void*)0 &&
                   WriteFile(output, payload, sizeof(payload) - 1U, &written, (void*)0) &&
                   written == sizeof(payload) - 1U
               ? 1
               : 0;
}

__attribute__((used, section(".rdata")))
void (*const compat_relocation_anchor)(void) = (void (*)(void))CompatEntry;

int DllMain(void* module, dword_t reason, void* reserved) {
    (void)module;
    (void)reason;
    (void)reserved;
    return 1;
}
