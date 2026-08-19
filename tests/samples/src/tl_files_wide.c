typedef unsigned long dword_t;
typedef unsigned short wchar_t_guest;
typedef long long int64_t_guest;
typedef int bool_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) void* CreateFileW(const wchar_t_guest* path, dword_t desired_access,
                                              dword_t share_mode, const void* security_attributes,
                                              dword_t creation_disposition, dword_t flags,
                                              const void* template_file);
__attribute__((dllimport)) int CloseHandle(const void* handle);
__attribute__((dllimport)) int GetFileSizeEx(const void* handle, int64_t_guest* size);
__attribute__((dllimport)) int SetFilePointerEx(const void* handle, int64_t_guest distance,
                                                int64_t_guest* new_position, dword_t move_method);
__attribute__((dllimport)) int SetEndOfFile(const void* handle);
__attribute__((dllimport)) int FlushFileBuffers(const void* handle);
__attribute__((dllimport)) int GetFileAttributesExW(const wchar_t_guest* path, int info_level,
                                                    void* data);
__attribute__((dllimport)) int GetFileTime(const void* handle, void* creation_time,
                                           void* access_time, void* write_time);
__attribute__((dllimport)) int SetFileTime(const void* handle, const void* creation_time,
                                           const void* access_time, const void* write_time);
__attribute__((dllimport)) int GetFileInformationByHandle(const void* handle, void* information);
__attribute__((dllimport)) int GetFileInformationByHandleEx(const void* handle, int info_class,
                                                             void* buffer, dword_t size);
__attribute__((dllimport)) dword_t GetFinalPathNameByHandleW(const void* handle,
                                                             wchar_t_guest* buffer,
                                                             dword_t buffer_length, dword_t flags);
__attribute__((dllimport)) int CreateDirectoryW(const wchar_t_guest* path,
                                                const void* security_attributes);
__attribute__((dllimport)) int RemoveDirectoryW(const wchar_t_guest* path);
__attribute__((dllimport)) dword_t GetFileAttributesW(const wchar_t_guest* path);
__attribute__((dllimport)) int CopyFileW(const wchar_t_guest* from, const wchar_t_guest* to,
                                         int fail_if_exists);
__attribute__((dllimport)) int MoveFileExW(const wchar_t_guest* from, const wchar_t_guest* to,
                                           dword_t flags);
__attribute__((dllimport)) int DeleteFileW(const wchar_t_guest* path);
__attribute__((dllimport)) dword_t GetTempPathW(dword_t buffer_length, wchar_t_guest* buffer);
__attribute__((dllimport)) dword_t GetFullPathNameW(const wchar_t_guest* path,
                                                    dword_t buffer_length,
                                                    wchar_t_guest* buffer,
                                                    wchar_t_guest** file_part);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static void fail(void* output, dword_t* bytes_written, dword_t code) {
    static const char message[] = "FAIL\n";
    (void)WriteFile(output, message, sizeof(message) - 1, bytes_written, (void*)0);
    ExitProcess(code);
}

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t bytes_written = 0;
    static const wchar_t_guest directory[] = {
        '_', 't', 'l', '_', 'w', 'i', 'd', 'e', '_', 'd', 'i', 'r', 0};
    static const wchar_t_guest source[] = {
        '_', 't', 'l', '_', 'w', 'i', 'd', 'e', '_', 'd', 'i', 'r', '/',
        'a', 'r', 'q', 'u', 'i', 'v', 'o', '_', 0x00E9, '.', 't', 'x', 't', 0};
    static const wchar_t_guest copy[] = {
        '_', 't', 'l', '_', 'w', 'i', 'd', 'e', '_', 'd', 'i', 'r', '/',
        'a', 'r', 'q', 'u', 'i', 'v', 'o', '_', 0x00E9, '.', 'c', 'o', 'p', 'y', 0};
    static const wchar_t_guest moved[] = {
        '_', 't', 'l', '_', 'w', 'i', 'd', 'e', '_', 'd', 'i', 'r', '/',
        'a', 'r', 'q', 'u', 'i', 'v', 'o', '_', 0x00E9, '.', 'm', 'o', 'v', 'e', 'd', 0};

    if (!CreateDirectoryW(directory, (void*)0) &&
        (GetFileAttributesW(directory) == 0xFFFFFFFFU ||
         (GetFileAttributesW(directory) & 0x10U) == 0U)) {
        fail(output, &bytes_written, 1U);
    }
    void* handle = CreateFileW(source, 0xC0000000U, 0, (void*)0, 2, 0, (void*)0);
    if (handle == (void*)0) {
        fail(output, &bytes_written, 2U);
    }
    static const char payload[] = "wide\n";
    dword_t written = 0;
    if (!WriteFile(handle, payload, sizeof(payload) - 1, &written, (void*)0) ||
        written != sizeof(payload) - 1) {
        fail(output, &bytes_written, 3U);
    }

    int64_t_guest size = 0;
    int64_t_guest position = 0;
    if (!GetFileSizeEx(handle, &size) || size != 5 ||
        !SetFilePointerEx(handle, 0, &position, 2) || position != 5 ||
        !SetEndOfFile(handle) || !FlushFileBuffers(handle)) {
        fail(output, &bytes_written, 4U);
    }

    unsigned long file_time[2] = {0, 0};
    if (!GetFileTime(handle, file_time, file_time, file_time) ||
        !SetFileTime(handle, file_time, file_time, file_time)) {
        fail(output, &bytes_written, 5U);
    }
    unsigned char by_handle[52] = {0};
    unsigned char by_handle_ex[40] = {0};
    unsigned char attributes[36] = {0};
    if (!GetFileInformationByHandle(handle, by_handle) ||
        !GetFileInformationByHandleEx(handle, 0, by_handle_ex, sizeof(by_handle_ex)) ||
        !GetFileAttributesExW(source, 0, attributes) ||
        *(unsigned long*)(by_handle + 36) != 5U ||
        *(unsigned long*)(attributes + 32) != 5U) {
        fail(output, &bytes_written, 6U);
    }
    wchar_t_guest final_path[260] = {0};
    if (GetFinalPathNameByHandleW(handle, final_path, 260, 0) == 0) {
        fail(output, &bytes_written, 7U);
    }
    CloseHandle(handle);

    wchar_t_guest temp_path[8] = {0};
    wchar_t_guest full_path[512] = {0};
    wchar_t_guest* file_part = (wchar_t_guest*)0;
    if (GetTempPathW(8, temp_path) == 0 ||
        GetFullPathNameW(source, 512, full_path, &file_part) == 0 || file_part == (void*)0 ||
        !CopyFileW(source, copy, 1) || !MoveFileExW(copy, moved, 1) ||
        !DeleteFileW(source) || !DeleteFileW(moved) || !RemoveDirectoryW(directory)) {
        fail(output, &bytes_written, 8U);
    }

    static const char message[] = "files\n";
    if (!WriteFile(output, message, sizeof(message) - 1, &bytes_written, (void*)0)) {
        ExitProcess(9U);
    }
    ExitProcess(0U);
}
