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
__attribute__((dllimport)) dword_t GetLastError(void);

__attribute__((dllimport)) int SHAutoComplete(void* hwnd_edit, dword_t flags);
__attribute__((dllimport)) int SHFileOperationW(void* file_op);
__attribute__((dllimport)) bool_t PathRemoveFileSpecW(uint16_t* path);
__attribute__((dllimport)) uint16_t* PathCombineW(uint16_t* dest, const uint16_t* dir, const uint16_t* file);
__attribute__((dllimport)) bool_t PathIsRelativeW(const uint16_t* path);

struct shfileop_t {
    void* hwnd;
    dword_t func;
    const uint16_t* from;
    const uint16_t* to;
    uint16_t flags;
    int any_aborted;
    void* name_mappings;
    const uint16_t* progress_title;
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

    // 1. SHAutoComplete
    (void)GetLastError();
    if (SHAutoComplete((void*)0, 0) != 0) {
        fail(output, &bytes_written, 1);
    }

    // 2. SHFileOperationW
    struct shfileop_t op;
    op.hwnd = (void*)0;
    op.func = 1; // FO_MOVE
    op.from = (const uint16_t*)0;
    op.to = (const uint16_t*)0;
    op.flags = 0;
    op.any_aborted = 0;
    op.name_mappings = (void*)0;
    op.progress_title = (const uint16_t*)0;
    if (SHFileOperationW(&op) != 0) {
        fail(output, &bytes_written, 2);
    }

    // 3. PathIsRelativeW
    static const uint16_t rel_path[] = {'f', 'o', 'o', '\\', 'b', 'a', 'r', 0};
    static const uint16_t abs_path[] = {'C', ':', '\\', 'f', 'o', 'o', 0};
    if (!PathIsRelativeW(rel_path)) {
        fail(output, &bytes_written, 3);
    }
    if (PathIsRelativeW(abs_path)) {
        fail(output, &bytes_written, 4);
    }

    // 4. PathCombineW
    uint16_t combined[260];
    static const uint16_t cdir[] = {'C', ':', '\\', 'd', 'i', 'r', 0};
    static const uint16_t cfile[] = {'f', 'i', 'l', 'e', '.', 't', 'x', 't', 0};
    if (PathCombineW(combined, cdir, cfile) != combined) {
        fail(output, &bytes_written, 5);
    }

    // 5. PathRemoveFileSpecW
    if (!PathRemoveFileSpecW(combined)) {
        fail(output, &bytes_written, 6);
    }

    static const char result[] = "shellpath\n";
    if (!WriteFile(output, result, sizeof(result) - 1, &bytes_written, (void*)0) ||
        bytes_written != sizeof(result) - 1) {
        ExitProcess(7);
    }
    ExitProcess(0);
}

