typedef unsigned short word_t;
typedef unsigned long dword_t;
typedef long long lparam_t;
typedef unsigned long long internet_t;
typedef int bool_t;

typedef struct {
    dword_t dw_struct_size;
    word_t* scheme;
    dword_t scheme_length;
    dword_t scheme_type;
    word_t* host;
    dword_t host_length;
    word_t port;
    word_t reserved;
    word_t* user;
    dword_t user_length;
    word_t* password;
    dword_t password_length;
    word_t* path;
    dword_t path_length;
    word_t* extra;
    dword_t extra_length;
} url_components_w;

__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) dword_t GetEnvironmentVariableW(const word_t* name, word_t* buffer,
                                                            dword_t size);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* written,
                                             const void* overlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t code);

__attribute__((dllimport)) internet_t InternetOpenW(const word_t* agent, dword_t access_type,
                                                     const word_t* proxy, const word_t* bypass,
                                                     dword_t flags);
__attribute__((dllimport)) internet_t InternetConnectW(internet_t internet, const word_t* server,
                                                        word_t port, const word_t* user,
                                                        const word_t* password, dword_t service,
                                                        dword_t flags, unsigned long long context);
__attribute__((dllimport)) internet_t HttpOpenRequestW(internet_t connect, const word_t* verb,
                                                       const word_t* object, const word_t* version,
                                                       const word_t* referrer,
                                                       const word_t* const* accept_types,
                                                       dword_t flags, unsigned long long context);
__attribute__((dllimport)) bool_t HttpAddRequestHeadersW(internet_t request, const word_t* headers,
                                                          long headers_length, dword_t modifiers);
__attribute__((dllimport)) bool_t HttpSendRequestW(internet_t request, const word_t* headers,
                                                    dword_t headers_length, const void* data,
                                                    dword_t data_length);
__attribute__((dllimport)) bool_t InternetReadFile(internet_t request, void* buffer,
                                                    dword_t length, dword_t* read);
__attribute__((dllimport)) bool_t InternetQueryDataAvailable(internet_t request, dword_t* available,
                                                              dword_t flags, unsigned long long context);
__attribute__((dllimport)) bool_t HttpQueryInfoW(internet_t request, dword_t level, void* buffer,
                                                 dword_t* length, dword_t* index);
__attribute__((dllimport)) bool_t InternetSetOptionW(internet_t internet, dword_t option,
                                                      void* buffer, dword_t length);
__attribute__((dllimport)) bool_t InternetCloseHandle(internet_t internet);
__attribute__((dllimport)) bool_t InternetCrackUrlW(const word_t* url, dword_t length,
                                                     dword_t flags, url_components_w* components);

static const word_t kPortName[] = {'T','L','_','W','I','N','I','N','E','T','_','P','O','R','T',0};
static const word_t kCaFileName[] = {'T','L','_','W','I','N','I','N','E','T','_','C','A','_','F','I','L','E',0};
static const word_t kAgent[] = {'T','r','a','d','u','t','o','r','L','i','n','u','x',' ','t','e','s','t',0};
static const word_t kHost[] = {'l','o','c','a','l','h','o','s','t',0};
static const word_t kUrlPrefix[] = {'h','t','t','p','s',':','/','/','l','o','c','a','l','h','o','s','t',':',0};
static const word_t kPath[] = {'/','f','i','x','t','u','r','e','?','x','=','1',0};
static const word_t kVerb[] = {'G','E','T',0};
static const word_t kHeader[] = {'X','-','T','L','-','F','i','x','t','u','r','e',':',' ','y','e','s',0};
static const word_t kBody[] = {'w','i','n','i','n','e','t','-','r','e','s','p','o','n','s','e','\n'};
static const char kOutput[] = "wininet\n";

static void fail(void* output, dword_t* written, dword_t code) {
    static const char message[] = "FAIL\n";
    (void)WriteFile(output, message, sizeof(message) - 1U, written, (void*)0);
    ExitProcess(code);
}

static word_t decimal_port(const word_t* text, dword_t length) {
    dword_t value = 0;
    dword_t index;
    if (length == 0 || length >= 6U) return 0;
    for (index = 0; index < length; ++index) {
        if (text[index] < '0' || text[index] > '9') return 0;
        value = value * 10U + (dword_t)(text[index] - '0');
    }
    return value > 0U && value <= 65535U ? (word_t)value : 0;
}

static int same_bytes(const char* left, const char* right, dword_t length) {
    dword_t index;
    for (index = 0; index < length; ++index) {
        if (left[index] != right[index]) return 0;
    }
    return 1;
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t written = 0;
    word_t port_text[16] = {0};
    word_t url[96] = {0};
    word_t* cursor = url;
    word_t scheme[8] = {0};
    word_t host[32] = {0};
    word_t path[64] = {0};
    word_t extra[32] = {0};
    url_components_w components = {0};
    dword_t available = 0;
    dword_t read = 0;
    dword_t status = 0;
    dword_t status_length = sizeof(status);
    if (GetEnvironmentVariableW(kCaFileName, port_text, 16U) != 0) fail(output, &written, 1U);
    dword_t port_length = GetEnvironmentVariableW(kPortName, port_text, 16U);
    word_t port = decimal_port(port_text, port_length);
    if (port == 0) fail(output, &written, 1U);

    /* Monta https://localhost:<porta>/fixture?x=1 sem CRT. */
    {
        const word_t* part = kUrlPrefix;
        while (*part) *cursor++ = *part++;
        {
            word_t digits[6];
            dword_t count = 0;
            dword_t value = port;
            do { digits[count++] = (word_t)('0' + (value % 10U)); value /= 10U; } while (value != 0U);
            while (count != 0U) *cursor++ = digits[--count];
        }
        part = kPath;
        while (*part) *cursor++ = *part++;
        *cursor = 0;
    }

    components.dw_struct_size = sizeof(components);
    components.scheme = scheme; components.scheme_length = 8U;
    components.host = host; components.host_length = 32U;
    components.path = path; components.path_length = 64U;
    components.extra = extra; components.extra_length = 32U;
    if (!InternetCrackUrlW(url, 0, 0, &components) || components.scheme_type != 2U ||
        components.port != port || components.scheme_length != 5U ||
        components.host_length != 9U || components.path_length != 8U ||
        components.extra_length != 4U) fail(output, &written, 2U);

    internet_t session = InternetOpenW(kAgent, 1U, (const word_t*)0, (const word_t*)0, 0U);
    internet_t connection = InternetConnectW(session, kHost, port, (const word_t*)0,
                                              (const word_t*)0, 3U, 0U, 0ULL);
    internet_t request = HttpOpenRequestW(connection, kVerb, kPath, (const word_t*)0,
                                           (const word_t*)0, (const word_t* const*)0,
                                           0x00800000U, 0ULL);
    if (session == 0 || connection == 0 || request == 0) fail(output, &written, 3U);

    {
        dword_t timeout = 5000U;
        if (!InternetSetOptionW(request, 2U, &timeout, sizeof(timeout)) ||
            !HttpAddRequestHeadersW(request, kHeader, -1L, 0x20000000U) ||
            !HttpSendRequestW(request, (const word_t*)0, 0U, (const void*)0, 0U)) {
            fail(output, &written, 4U);
        }
    }
    if (!HttpQueryInfoW(request, 19U | 0x20000000U, &status, &status_length, (dword_t*)0) ||
        status != 200U || !InternetQueryDataAvailable(request, &available, 0U, 0ULL) ||
        available != sizeof(kBody) / sizeof(kBody[0])) fail(output, &written, 5U);

    {
        char body[64] = {0};
        if (!InternetReadFile(request, body, 7U, &read) || read != 7U ||
            !same_bytes(body, "wininet", 7U) ||
            !InternetReadFile(request, body + 7, 64U - 7U, &read) || read != 10U ||
            !same_bytes(body + 7, "-response\n", 10U)) fail(output, &written, 6U);
    }
    if (!InternetCloseHandle(request) || !InternetCloseHandle(connection) ||
        !InternetCloseHandle(session) || !WriteFile(output, kOutput, sizeof(kOutput) - 1U,
                                                     &written, (const void*)0) ||
        written != sizeof(kOutput) - 1U) fail(output, &written, 7U);
    ExitProcess(0U);
}
