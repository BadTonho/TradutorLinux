typedef unsigned char byte_t;
typedef unsigned int dword_t;
typedef int bool_t;
typedef short int16_t;
typedef int int32_t;
typedef unsigned short uint16_t;
typedef unsigned long long uint64_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);

__attribute__((dllimport)) uint16_t* SysAllocString(const uint16_t* sz);
__attribute__((dllimport)) void SysFreeString(uint16_t* bstr);
__attribute__((dllimport)) dword_t SysStringLen(const uint16_t* bstr);

struct variant_t {
    uint16_t vt;
    uint16_t wReserved1;
    uint16_t wReserved2;
    uint16_t wReserved3;
    union {
        uint64_t llVal;
        int32_t lVal;
        uint16_t* bstrVal;
        byte_t raw[16];
    } data;
};

__attribute__((dllimport)) void VariantInit(struct variant_t* pvarg);
__attribute__((dllimport)) int32_t VariantClear(struct variant_t* pvarg);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static void fail(const void* output, dword_t* bytes_written, dword_t code) {
    static const char message[] = "FAIL\n";
    (void)WriteFile(output, message, sizeof(message) - 1, bytes_written, (void*)0);
    ExitProcess(code);
}

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t bytes_written = 0;
    if (output == (void*)0 || output == (void*)(uint64_t)-1) {
        ExitProcess(1);
    }

    static const uint16_t kText[] = {'T', 'e', 's', 't', 0};
    uint16_t* bstr = SysAllocString(kText);
    if (bstr == (void*)0) {
        fail(output, &bytes_written, 2);
    }
    if (SysStringLen(bstr) != 4) {
        SysFreeString(bstr);
        fail(output, &bytes_written, 3);
    }

    struct variant_t var;
    VariantInit(&var);
    var.vt = 8; // VT_BSTR
    var.data.bstrVal = bstr;
    VariantClear(&var);

    static const char success[] = "oleautbstr\n";
    if (!WriteFile(output, success, sizeof(success) - 1, &bytes_written, (void*)0)) {
        ExitProcess(4);
    }

    ExitProcess(0);
}
