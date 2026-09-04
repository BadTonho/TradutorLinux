typedef unsigned char byte_t;
typedef unsigned int dword_t;
typedef unsigned long long uint64_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) int WriteFile(const void* handle, const void* buffer,
                                         dword_t bytes_to_write, dword_t* bytes_written,
                                         const void* overlapped);

/* The runtime must preserve the raw TLS byte and provide the zero-fill tail. */
__declspec(thread) static byte_t tl_tls_template[0x438] = {0x5A};

static void* read_pointer_backed_tls_slot(void) {
    void* tls_array = 0;
    __asm__ volatile("movq %%gs:0x58, %0" : "=r"(tls_array));
    if (tls_array == 0) return 0;
    return *(void**)((byte_t*)*(void**)tls_array + 0x430);
}

static void fail(void* output, dword_t* written, dword_t code) {
    static const char message[] = "FAIL\n";
    (void)WriteFile(output, message, sizeof(message) - 1, written, 0);
    ExitProcess(code);
}

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t written = 0;
    if (tl_tls_template[0] != 0x5A) fail(output, &written, 1);

    void* state = read_pointer_backed_tls_slot();
    if (state == 0) fail(output, &written, 2);
    if (*(uint64_t*)((byte_t*)state + 0x68) != 0) fail(output, &written, 3);

    static const char message[] = "tls-generic\n";
    if (!WriteFile(output, message, sizeof(message) - 1, &written, 0) ||
        written != sizeof(message) - 1) {
        ExitProcess(4);
    }
    ExitProcess(0);
}
