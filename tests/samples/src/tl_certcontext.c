typedef unsigned char byte_t;
typedef unsigned int dword_t;
typedef int bool_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) dword_t GetLastError(void);

__attribute__((dllimport)) void* CertDuplicateCertificateContext(const void* cert_context);
__attribute__((dllimport)) bool_t CertFreeCertificateContext(const void* cert_context);
__attribute__((dllimport)) void* CertOpenStore(const char* store_provider, dword_t encoding_type,
                                               void* crypt_prov, dword_t flags, const void* para);
__attribute__((dllimport)) bool_t CertCloseStore(void* cert_store, dword_t flags);
__attribute__((dllimport)) bool_t CertGetCertificateContextProperty(
    const void* cert_context, dword_t prop_id, void* data, dword_t* data_size);
__attribute__((dllimport)) void* CertOpenSystemStoreA(void* crypt_prov, const char* system_store_name);

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

    // Test 1: CertDuplicateCertificateContext
    const struct cert_context* dup = (const struct cert_context*)CertDuplicateCertificateContext(&context);
    if (!dup || dup->encoded_size != sizeof(certificate)) {
        fail(output, &bytes_written, 1);
    }

    // Test 2: CertGetCertificateContextProperty (SHA-1)
    dword_t hash_size = 0;
    if (!CertGetCertificateContextProperty(dup, 3U, (void*)0, &hash_size) || hash_size != 20U) {
        fail(output, &bytes_written, 2);
    }
    byte_t hash[20] = {0};
    if (!CertGetCertificateContextProperty(dup, 3U, hash, &hash_size) || hash_size != 20U) {
        fail(output, &bytes_written, 3);
    }

    // Test 3: CertFreeCertificateContext
    if (!CertFreeCertificateContext(dup)) {
        fail(output, &bytes_written, 4);
    }
    if (!CertFreeCertificateContext((const void*)0)) {
        fail(output, &bytes_written, 5);
    }

    // Test 4: CertOpenStore & CertCloseStore
    if (CertCloseStore((void*)0, 0) || GetLastError() != 6U) {
        fail(output, &bytes_written, 6);
    }
    void* mem_store = CertOpenStore((const char*)2, 0, (void*)0, 0, (void*)0);
    if (!mem_store) {
        fail(output, &bytes_written, 7);
    }
    if (!CertCloseStore(mem_store, 0)) {
        fail(output, &bytes_written, 8);
    }

    // Test 5: CertOpenSystemStoreA & CertCloseStore
    void* sys_store = CertOpenSystemStoreA((void*)0, "MY");
    if (!sys_store) {
        fail(output, &bytes_written, 9);
    }
    if (!CertCloseStore(sys_store, 0)) {
        fail(output, &bytes_written, 10);
    }

    static const char result[] = "certcontext\n";
    if (!WriteFile(output, result, sizeof(result) - 1, &bytes_written, (void*)0) ||
        bytes_written != sizeof(result) - 1) {
        ExitProcess(10);
    }
    ExitProcess(0);
}

