typedef unsigned long dword_t;
typedef unsigned long long uqword_t;
typedef long long qword_t;
typedef int hresult_t;
typedef int bool_t;

typedef struct i_stream i_stream;
typedef struct stat_stg {
    unsigned short* pwcs_name;
    dword_t type;
    uqword_t cb_size;
    dword_t mtime_low;
    dword_t mtime_high;
    dword_t ctime_low;
    dword_t ctime_high;
    dword_t atime_low;
    dword_t atime_high;
    dword_t grf_mode;
    dword_t grf_locks_supported;
    unsigned char clsid[16];
    dword_t grf_state_bits;
    dword_t reserved;
} stat_stg;

typedef struct i_stream_vtbl {
    hresult_t (__attribute__((ms_abi)) *query_interface)(i_stream*, const void*, i_stream**);
    dword_t (__attribute__((ms_abi)) *add_ref)(i_stream*);
    dword_t (__attribute__((ms_abi)) *release)(i_stream*);
    hresult_t (__attribute__((ms_abi)) *read)(i_stream*, void*, dword_t, dword_t*);
    hresult_t (__attribute__((ms_abi)) *write)(i_stream*, const void*, dword_t, dword_t*);
    hresult_t (__attribute__((ms_abi)) *seek)(i_stream*, qword_t, dword_t, uqword_t*);
    hresult_t (__attribute__((ms_abi)) *set_size)(i_stream*, uqword_t);
    hresult_t (__attribute__((ms_abi)) *copy_to)(i_stream*, i_stream*, uqword_t, uqword_t*, uqword_t*);
    hresult_t (__attribute__((ms_abi)) *commit)(i_stream*, dword_t);
    hresult_t (__attribute__((ms_abi)) *revert)(i_stream*);
    hresult_t (__attribute__((ms_abi)) *lock_region)(i_stream*, uqword_t, uqword_t, dword_t);
    hresult_t (__attribute__((ms_abi)) *unlock_region)(i_stream*, uqword_t, uqword_t, dword_t);
    hresult_t (__attribute__((ms_abi)) *stat)(i_stream*, stat_stg*, dword_t);
    hresult_t (__attribute__((ms_abi)) *clone)(i_stream*, i_stream**);
} i_stream_vtbl;

struct i_stream {
    i_stream_vtbl* vtbl;
};

__attribute__((dllimport)) void* GetStdHandle(dword_t nStdHandle);
__attribute__((dllimport)) bool_t WriteFile(void* hFile, const void* buffer, dword_t bytes,
                                             dword_t* written, void* overlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t code);
__attribute__((dllimport)) hresult_t CreateStreamOnHGlobal(void* hglobal, hresult_t delete_on_release,
                                                            i_stream** stream);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static void fail(void* output, dword_t code) {
    static const char message[] = "FAIL\n";
    dword_t written = 0;
    (void)WriteFile(output, message, sizeof(message) - 1, &written, (void*)0);
    ExitProcess(code);
}

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    i_stream* stream = (i_stream*)0;
    if (CreateStreamOnHGlobal((void*)0, 1, &stream) != 0 || stream == (i_stream*)0) fail(output, 1);

    static const unsigned char iid_i_stream[16] = {
        0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46,
    };
    i_stream* same = (i_stream*)0;
    if (stream->vtbl->query_interface(stream, iid_i_stream, &same) != 0 || same != stream) fail(output, 2);
    if (stream->vtbl->release(same) != 1) fail(output, 3);

    static const char payload[] = "stream-data";
    dword_t written = 0;
    if (stream->vtbl->write(stream, payload, sizeof(payload) - 1, &written) != 0 ||
        written != sizeof(payload) - 1) fail(output, 4);

    stat_stg stat;
    if (stream->vtbl->stat(stream, &stat, 0) != 0 || stat.type != 2 || stat.cb_size != sizeof(payload) - 1) {
        fail(output, 5);
    }

    uqword_t position = 0;
    if (stream->vtbl->seek(stream, -4, 2, &position) != 0 || position != 7) fail(output, 6);
    char suffix[5] = {0, 0, 0, 0, 0};
    dword_t read = 0;
    if (stream->vtbl->read(stream, suffix, 4, &read) != 0 || read != 4 ||
        suffix[0] != 'd' || suffix[1] != 'a' || suffix[2] != 't' || suffix[3] != 'a') fail(output, 7);

    if (stream->vtbl->seek(stream, 0, 0, &position) != 0) fail(output, 8);
    char round_trip[sizeof(payload)] = {0};
    if (stream->vtbl->read(stream, round_trip, sizeof(payload) - 1, &read) != 0 || read != sizeof(payload) - 1) {
        fail(output, 9);
    }
    for (dword_t index = 0; index < sizeof(payload) - 1; ++index) {
        if (round_trip[index] != payload[index]) fail(output, 10);
    }

    if (stream->vtbl->set_size(stream, 16) != 0 || stream->vtbl->stat(stream, &stat, 0) != 0 || stat.cb_size != 16) {
        fail(output, 11);
    }
    if (stream->vtbl->commit(stream, 0) != 0 || stream->vtbl->revert(stream) != 0) fail(output, 12);
    if (stream->vtbl->release(stream) != 0) fail(output, 13);

    static const char message[] = "ole-stream\n";
    if (!WriteFile(output, message, sizeof(message) - 1, &written, (void*)0) || written != sizeof(message) - 1) {
        ExitProcess(99);
    }
    ExitProcess(0);
}
