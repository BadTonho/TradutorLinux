typedef unsigned char byte_t;
typedef unsigned short word_t;
typedef unsigned int dword_t;
typedef unsigned long long socket_t;
typedef int bool_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);

__attribute__((dllimport)) int WSAStartup(word_t version_requested, void* data);
__attribute__((dllimport)) int WSACleanup(void);
__attribute__((dllimport)) int getaddrinfo(const char* node, const char* service,
                                           const void* hints, void* result);
__attribute__((dllimport)) void freeaddrinfo(void* address_info);

__attribute__((dllimport)) dword_t GetAdaptersAddresses(dword_t family, dword_t flags,
                                                         void* reserved, void* addresses,
                                                         dword_t* size);
__attribute__((dllimport)) dword_t if_nametoindex(const char* name);

__attribute__((dllimport)) void* CertOpenStore(const char* store_provider,
                                                dword_t encoding_type, void* crypt_prov,
                                                dword_t flags, const void* para);
__attribute__((dllimport)) bool_t CertCloseStore(void* store, dword_t flags);

__attribute__((dllimport)) bool_t WTSEnumerateSessionsW(void* server, dword_t reserved,
                                                         dword_t version, void** sessions,
                                                         dword_t* count);
__attribute__((dllimport)) void WTSFreeMemory(void* memory);

typedef struct guest_addrinfo {
    int flags;
    int family;
    int socktype;
    int protocol;
    unsigned long long address_length;
    char* canonname;
    void* address;
    struct guest_addrinfo* next;
} guest_addrinfo;

typedef struct guest_wts_session_info {
    dword_t session_id;
    word_t* win_station_name;
    dword_t state;
} guest_wts_session_info;

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static void fail(void* output, dword_t* written, dword_t code) {
    static const char message[] = "FAIL\n";
    (void)WriteFile(output, message, sizeof(message) - 1U, written, (void*)0);
    ExitProcess(code);
}

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t written = 0;
    byte_t wsa_data[400] = {0};
    if (WSAStartup(0x0202U, wsa_data) != 0) fail(output, &written, 1U);

    void* address_info = (void*)0;
    if (getaddrinfo("localhost", "0", (void*)0, &address_info) != 0 ||
        address_info == (void*)0 || ((guest_addrinfo*)address_info)->family != 2 ||
        ((guest_addrinfo*)address_info)->address == (void*)0) {
        fail(output, &written, 2U);
    }
    freeaddrinfo(address_info);

    dword_t adapter_size = 0;
    dword_t result = GetAdaptersAddresses(2U, 0U, (void*)0, (void*)0, &adapter_size);
    if (result == 232U) ExitProcess(77U);
    if (result != 111U) fail(output, &written, 10U + (result % 10U));
    if (adapter_size == 0U) fail(output, &written, 20U);
    static byte_t adapters[65536];
    adapter_size = sizeof(adapters);
    if (GetAdaptersAddresses(2U, 0U, (void*)0, adapters, &adapter_size) != 0U ||
        *(dword_t*)adapters != 184U || *(void**)(adapters + 24U) == (void*)0) {
        fail(output, &written, 4U);
    }
    if (if_nametoindex("lo") == 0U || if_nametoindex("tl-no-such-interface") != 0U) {
        fail(output, &written, 5U);
    }

    void* store = CertOpenStore((const char*)2, 0U, (void*)0, 0U, (void*)0);
    if (store == (void*)0 || !CertCloseStore(store, 0U)) fail(output, &written, 6U);

    void* sessions = (void*)0;
    dword_t session_count = 0;
    if (!WTSEnumerateSessionsW((void*)0, 0U, 1U, &sessions, &session_count) ||
        sessions == (void*)0 || session_count != 1U ||
        ((guest_wts_session_info*)sessions)->win_station_name == (word_t*)0 ||
        ((guest_wts_session_info*)sessions)->state != 0U) {
        fail(output, &written, 7U);
    }
    WTSFreeMemory(sessions);
    if (WSACleanup() != 0) fail(output, &written, 8U);

    static const char result_text[] = "worker-rsl\n";
    if (!WriteFile(output, result_text, sizeof(result_text) - 1U, &written, (void*)0) ||
        written != sizeof(result_text) - 1U) {
        ExitProcess(9U);
    }
    ExitProcess(0U);
}
