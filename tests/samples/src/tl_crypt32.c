typedef unsigned char byte_t;
typedef unsigned int dword_t;
typedef int bool_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) dword_t GetLastError(void);
__attribute__((dllimport)) dword_t CertGetNameStringW(const void* cert_context, dword_t type,
                                                       dword_t flags, const void* type_parameter,
                                                       unsigned short* name_string,
                                                       dword_t name_string_capacity);

struct cert_context {
    dword_t encoding_type;
    byte_t* encoded;
    dword_t encoded_size;
    void* cert_info;
    void* cert_store;
};

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static void fail(const void* output, dword_t* bytes_written, dword_t code) {
    static const char message[] = "FAIL\n";
    (void)WriteFile(output, message, sizeof(message) - 1, bytes_written, (void*)0);
    ExitProcess(code);
}

static bool_t equal_wide(const unsigned short* actual, const unsigned short* expected,
                         dword_t length) {
    for (dword_t index = 0; index < length; ++index) {
        if (actual[index] != expected[index]) return 0;
    }
    return 1;
}

void tl_entry(void) {
    static const byte_t certificate[] = {
        0x30, 0x3B, 0x30, 0x34, 0x02, 0x01, 0x01, 0x30, 0x00,
        0x30, 0x12, 0x31, 0x10, 0x30, 0x0E, 0x06, 0x03, 0x55, 0x04, 0x03,
        0x0C, 0x07, 'T', 'L', ' ', 'R', 'o', 'o', 't',
        0x30, 0x00,
        0x30, 0x15, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x03,
        0x0C, 0x0A, 'T', 'L', ' ', 'F', 'i', 'x', 't', 'u', 'r', 'e',
        0x30, 0x00, 0x30, 0x00, 0x03, 0x01, 0x00,
    };
    const struct cert_context context = {1U, (byte_t*)certificate, sizeof(certificate), (void*)0,
                                         (void*)0};
    void* output = GetStdHandle((dword_t)-11);
    dword_t bytes_written = 0;
    unsigned short name[32] = {0};
    static const unsigned short subject[] = {'T', 'L', ' ', 'F', 'i', 'x', 't', 'u', 'r', 'e', 0};
    static const unsigned short issuer[] = {'T', 'L', ' ', 'R', 'o', 'o', 't', 0};
    static const char common_name_oid[] = "2.5.4.3";

    if (CertGetNameStringW(&context, 4, 0, (void*)0, (unsigned short*)0, 0) != 11U ||
        CertGetNameStringW(&context, 4, 0, (void*)0, name, 32) != 11U ||
        !equal_wide(name, subject, 11)) {
        fail(output, &bytes_written, 1);
    }
    if (CertGetNameStringW(&context, 4, 1, (void*)0, name, 32) != 8U ||
        !equal_wide(name, issuer, 8)) {
        fail(output, &bytes_written, 2);
    }
    if (CertGetNameStringW(&context, 6, 0, (void*)0, name, 32) != 11U ||
        !equal_wide(name, subject, 11)) {
        fail(output, &bytes_written, 3);
    }
    if (CertGetNameStringW(&context, 3, 0, common_name_oid, name, 32) != 11U ||
        !equal_wide(name, subject, 11)) {
        fail(output, &bytes_written, 4);
    }
    if (CertGetNameStringW(&context, 4, 0, (void*)0, name, 2) != 0U ||
        GetLastError() != 122U) {
        fail(output, &bytes_written, 5);
    }
    if (CertGetNameStringW(&context, 7, 0, (void*)0, name, 32) != 0U ||
        GetLastError() != 87U) {
        fail(output, &bytes_written, 6);
    }

    static const char result[] = "crypt32\n";
    if (!WriteFile(output, result, sizeof(result) - 1, &bytes_written, (void*)0) ||
        bytes_written != sizeof(result) - 1) {
        ExitProcess(7);
    }
    ExitProcess(0);
}
