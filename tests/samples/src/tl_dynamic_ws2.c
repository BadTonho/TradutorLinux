typedef unsigned long dword_t;
typedef unsigned short word_t;
typedef unsigned long long socket_t;
typedef int bool_t;

__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* LoadLibraryA(const char* file_name);
__attribute__((dllimport)) void* GetProcAddress(void* module, const char* name);

typedef int (*wsa_startup_fn)(word_t, void*);
typedef int (*wsa_cleanup_fn)(void);
typedef socket_t (*socket_fn)(int, int, int);
typedef int (*closesocket_fn)(socket_t);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static void fail(const dword_t code) {
    ExitProcess(code);
}

void tl_entry(void) {
    const char library_name[] = "ws2_32.dll";
    const char startup_name[] = "WSAStartup";
    const char cleanup_name[] = "WSACleanup";
    const char socket_name[] = "socket";
    const char close_name[] = "closesocket";
    void* const output = GetStdHandle((dword_t)-11);
    dword_t written = 0;
    void* const winsock = LoadLibraryA(library_name);
    if (winsock == (void*)0) fail(10U);

    const wsa_startup_fn startup = (wsa_startup_fn)GetProcAddress(winsock, startup_name);
    const wsa_cleanup_fn cleanup = (wsa_cleanup_fn)GetProcAddress(winsock, cleanup_name);
    const socket_fn open_socket = (socket_fn)GetProcAddress(winsock, socket_name);
    const closesocket_fn close_socket = (closesocket_fn)GetProcAddress(winsock, close_name);
    if (startup == (wsa_startup_fn)0 || cleanup == (wsa_cleanup_fn)0 ||
        open_socket == (socket_fn)0 || close_socket == (closesocket_fn)0) {
        fail(11U);
    }

    unsigned char wsa_data[400] = {0};
    if (startup(0x0202U, wsa_data) != 0) fail(12U);
    const socket_t socket = open_socket(2, 1, 0);
    if (socket == ~0ULL) fail(13U);
    if (close_socket(socket) != 0 || cleanup() != 0) fail(14U);

    static const char message[] = "dynamic-ws2\n";
    if (!WriteFile(output, message, sizeof(message) - 1U, &written, (void*)0) ||
        written != sizeof(message) - 1U) {
        fail(15U);
    }
    ExitProcess(0U);
}
