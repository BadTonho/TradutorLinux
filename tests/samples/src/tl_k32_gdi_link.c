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
__attribute__((dllimport)) void* GetCurrentProcess(void);
__attribute__((dllimport)) bool_t CreateHardLinkW(const uint16_t* new_name,
                                                  const uint16_t* exist_name,
                                                  void* sec);
__attribute__((dllimport)) dword_t K32GetModuleFileNameExW(void* proc, void* mod,
                                                           uint16_t* filename, dword_t size);

__attribute__((dllimport)) void* CreateBitmap(int32_t w, int32_t h, dword_t planes, dword_t bits, const void* pbits);
__attribute__((dllimport)) int32_t GetObjectW(const void* obj, int32_t buf_size, void* out_buf);
__attribute__((dllimport)) int32_t StretchBlt(void* dest_dc, int32_t x, int32_t y, int32_t w, int32_t h,
                                              void* src_dc, int32_t sx, int32_t sy, int32_t sw, int32_t sh,
                                              dword_t rop);

struct bitmap_t {
    int32_t bmType;
    int32_t bmWidth;
    int32_t bmHeight;
    int32_t bmWidthBytes;
    uint16_t bmPlanes;
    uint16_t bmBitsPixel;
    void* bmBits;
};

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

    void* hbmp = CreateBitmap(100, 100, 1, 32, (void*)0);
    if (hbmp == (void*)0) {
        fail(output, &bytes_written, 2);
    }

    struct bitmap_t bmp;
    if (GetObjectW(hbmp, sizeof(bmp), &bmp) <= 0) {
        fail(output, &bytes_written, 3);
    }

    (void)StretchBlt((void*)0, 0, 0, 10, 10, (void*)0, 0, 0, 10, 10, 0x00CC0020);

    uint16_t mod_name[260] = {0};
    (void)K32GetModuleFileNameExW(GetCurrentProcess(), (void*)0, mod_name, 260);

    static const uint16_t kNonExist1[] = {'n', '1', 0};
    static const uint16_t kNonExist2[] = {'n', '2', 0};
    (void)CreateHardLinkW(kNonExist2, kNonExist1, (void*)0);

    static const char success[] = "k32gdilink\n";
    if (!WriteFile(output, success, sizeof(success) - 1, &bytes_written, (void*)0)) {
        ExitProcess(4);
    }

    ExitProcess(0);
}
