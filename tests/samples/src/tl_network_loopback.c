typedef unsigned long dword_t;
typedef unsigned short word_t;
typedef unsigned long long socket_t;
typedef int bool_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) void* CreateThread(const void* security_attributes,
                                               unsigned long long stack_size, void* start_address,
                                               void* parameter, dword_t creation_flags,
                                               dword_t* thread_id);
__attribute__((dllimport, noreturn)) void ExitThread(dword_t exit_code);
__attribute__((dllimport)) dword_t WaitForSingleObject(const void* handle, dword_t milliseconds);
__attribute__((dllimport)) int CloseHandle(const void* handle);

__attribute__((dllimport)) int WSAStartup(word_t version_requested, void* data);
__attribute__((dllimport)) int WSACleanup(void);
__attribute__((dllimport)) int WSAGetLastError(void);
__attribute__((dllimport)) socket_t socket(int address_family, int type, int protocol);
__attribute__((dllimport)) int closesocket(socket_t socket);
__attribute__((dllimport)) int bind(socket_t socket, const void* name, int name_length);
__attribute__((dllimport)) int listen(socket_t socket, int backlog);
__attribute__((dllimport)) socket_t accept(socket_t socket, void* name, int* name_length);
__attribute__((dllimport)) int connect(socket_t socket, const void* name, int name_length);
__attribute__((dllimport)) void* WSACreateEvent(void);
__attribute__((dllimport)) int WSACloseEvent(void* event_handle);
__attribute__((dllimport)) int WSAEventSelect(socket_t socket, void* event_handle, long network_events);
__attribute__((dllimport)) dword_t WSAWaitForMultipleEvents(dword_t count, const void* const* events,
                                                             int wait_all, dword_t timeout, int alertable);
__attribute__((dllimport)) int WSAEnumNetworkEvents(socket_t socket, void* event_handle,
                                                     void* network_events);
__attribute__((dllimport)) int send(socket_t socket, const char* buffer, int length, int flags);
__attribute__((dllimport)) int recv(socket_t socket, char* buffer, int length, int flags);
__attribute__((dllimport)) int sendto(socket_t socket, const char* buffer, int length, int flags,
                                      const void* to, int to_length);
__attribute__((dllimport)) int recvfrom(socket_t socket, char* buffer, int length, int flags,
                                        void* from, int* from_length);
__attribute__((dllimport)) int getsockname(socket_t socket, void* name, int* name_length);
__attribute__((dllimport)) int shutdown(socket_t socket, int how);
__attribute__((dllimport)) int getaddrinfo(const char* node, const char* service,
                                           const void* hints, void* result);
__attribute__((dllimport)) void freeaddrinfo(void* address_info);
__attribute__((dllimport)) word_t htons(word_t host_short);
__attribute__((dllimport)) word_t ntohs(word_t network_short);
__attribute__((dllimport)) dword_t inet_addr(const char* address);
__attribute__((dllimport)) int WSAPoll(void* descriptors, dword_t count, int timeout);

typedef struct sockaddr_in_guest {
    word_t family;
    word_t port;
    dword_t address;
    unsigned char zero[8];
} sockaddr_in_guest;

typedef struct addrinfo_guest {
    int flags;
    int family;
    int socktype;
    int protocol;
    unsigned long long address_length;
    char* canonname;
    sockaddr_in_guest* address;
    struct addrinfo_guest* next;
} addrinfo_guest;

typedef struct pollfd_guest {
    socket_t socket;
    short events;
    short revents;
} pollfd_guest;

typedef struct wsa_network_events_guest {
    dword_t network_events;
    dword_t error_codes[10];
} wsa_network_events_guest;

static socket_t g_client_socket = ~0ULL;
static sockaddr_in_guest g_client_address;

void client_thread(void* parameter) {
    (void)parameter;
    g_client_socket = socket(2, 1, 0);
    if (g_client_socket == ~0ULL || connect(g_client_socket, &g_client_address,
                                             sizeof(g_client_address)) != 0 ||
        send(g_client_socket, "tcp", 3, 0) != 3) {
        ExitThread(1U);
    }
    shutdown(g_client_socket, 2);
    closesocket(g_client_socket);
    ExitThread(0U);
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static void fail(void* output, dword_t* written, dword_t code) {
    static const char message[] = "FAIL\n";
    (void)WriteFile(output, message, sizeof(message) - 1, written, (void*)0);
    ExitProcess(code);
}

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t written = 0;
    unsigned char wsa_data[400] = {0};
    if (WSAStartup(0x0202U, wsa_data) != 0) {
        fail(output, &written, 1U);
    }

    void* addrinfo_pointer = (void*)0;
    if (getaddrinfo("localhost", "0", (void*)0, &addrinfo_pointer) != 0 ||
        addrinfo_pointer == (void*)0 ||
        ((addrinfo_guest*)addrinfo_pointer)->family != 2 ||
        ((addrinfo_guest*)addrinfo_pointer)->address == (void*)0) {
        fail(output, &written, 2U);
    }
    g_client_address = *((addrinfo_guest*)addrinfo_pointer)->address;
    freeaddrinfo(addrinfo_pointer);
    g_client_address.port = 0;
    g_client_address.address = inet_addr("127.0.0.1");

    socket_t listener = socket(2, 1, 0);
    int address_length = sizeof(g_client_address);
    if (listener == ~0ULL) {
        const dword_t error = (dword_t)WSAGetLastError();
        if (error == 10013U) {
            ExitProcess(77U);
        }
        fail(output, &written, 30U + (error % 200U));
    }
    if (bind(listener, &g_client_address, address_length) != 0) {
        fail(output, &written, 31U);
    }
    if (getsockname(listener, &g_client_address, &address_length) != 0) {
        fail(output, &written, 32U);
    }
    if (listen(listener, 1) != 0) {
        fail(output, &written, 33U);
    }
    void* listener_event = WSACreateEvent();
    if (listener_event == (void*)0 || WSAEventSelect(listener, listener_event, 0x0008L | 0x0020L) != 0) {
        fail(output, &written, 34U);
    }
    void* thread = CreateThread((void*)0, 0, (void*)client_thread, (void*)0, 0, (dword_t*)0);
    if (thread == (void*)0) {
        fail(output, &written, 4U);
    }
    const void* listener_events[] = {listener_event};
    if (WSAWaitForMultipleEvents(1U, listener_events, 0, 5000U, 0) != 0U) {
        fail(output, &written, 35U);
    }
    wsa_network_events_guest network_events = {0};
    if (WSAEnumNetworkEvents(listener, listener_event, &network_events) != 0 ||
        (network_events.network_events & 0x0008U) == 0U) {
        fail(output, &written, 36U);
    }
    socket_t accepted = accept(listener, (void*)0, (int*)0);
    char tcp_buffer[4] = {0};
    if (accepted == ~0ULL || recv(accepted, tcp_buffer, 3, 0) != 3 ||
        tcp_buffer[0] != 't' || tcp_buffer[1] != 'c' || tcp_buffer[2] != 'p') {
        fail(output, &written, 5U);
    }
    closesocket(accepted);
    WSACloseEvent(listener_event);
    closesocket(listener);
    WaitForSingleObject(thread, 5000U);
    CloseHandle(thread);

    socket_t datagram = socket(2, 2, 0);
    sockaddr_in_guest udp_address = {2, 0, inet_addr("127.0.0.1"), {0}};
    address_length = sizeof(udp_address);
    if (datagram == ~0ULL || bind(datagram, &udp_address, address_length) != 0 ||
        getsockname(datagram, &udp_address, &address_length) != 0 ||
        sendto(datagram, "udp", 3, 0, &udp_address, sizeof(udp_address)) != 3) {
        fail(output, &written, 6U);
    }
    pollfd_guest descriptor = {datagram, 0x0100, 0};
    if (WSAPoll(&descriptor, 1, 5000) != 1 || descriptor.revents == 0) {
        fail(output, &written, 7U);
    }
    char udp_buffer[4] = {0};
    if (recvfrom(datagram, udp_buffer, 3, 0, (void*)0, (int*)0) != 3 ||
        udp_buffer[0] != 'u' || udp_buffer[1] != 'd' || udp_buffer[2] != 'p') {
        fail(output, &written, 8U);
    }
    closesocket(datagram);
    if (ntohs(htons(0x1234U)) != 0x1234U || WSACleanup() != 0) {
        fail(output, &written, 9U);
    }

    static const char message[] = "network\n";
    if (!WriteFile(output, message, sizeof(message) - 1, &written, (void*)0)) {
        ExitProcess(10U);
    }
    ExitProcess(0U);
}
