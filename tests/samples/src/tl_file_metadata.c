typedef unsigned long dword_t;
typedef unsigned short wchar16_t;
typedef int bool_t;

typedef struct file_basic_info_t {
    long long creation_time;
    long long last_access_time;
    long long last_write_time;
    long long change_time;
    dword_t file_attributes;
    dword_t reserved;
} file_basic_info_t;

typedef struct find_data_w_t {
    dword_t attributes;
    dword_t creation_low;
    dword_t creation_high;
    dword_t access_low;
    dword_t access_high;
    dword_t write_low;
    dword_t write_high;
    dword_t size_high;
    dword_t size_low;
    dword_t reserved0;
    dword_t reserved1;
    wchar16_t file_name[260];
    wchar16_t alternate_file_name[14];
} find_data_w_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(void* handle, const void* buffer,
                                            dword_t size, dword_t* written,
                                            void* overlapped);
__attribute__((dllimport)) bool_t CreateDirectoryW(const wchar16_t* path,
                                                   const void* security);
__attribute__((dllimport)) bool_t RemoveDirectoryW(const wchar16_t* path);
__attribute__((dllimport)) void* CreateFileW(const wchar16_t* path,
                                             dword_t desired_access,
                                             dword_t share_mode,
                                             const void* security,
                                             dword_t creation,
                                             dword_t attributes,
                                             const void* template_file);
__attribute__((dllimport)) bool_t CloseHandle(void* handle);
__attribute__((dllimport)) bool_t DeleteFileW(const wchar16_t* path);
__attribute__((dllimport)) dword_t GetFileAttributesW(const wchar16_t* path);
__attribute__((dllimport)) bool_t SetFileAttributesW(const wchar16_t* path,
                                                     dword_t attributes);
__attribute__((dllimport)) void* FindFirstFileExW(const wchar16_t* pattern,
                                                  int info_level,
                                                  find_data_w_t* data,
                                                  int search_operation,
                                                  const void* search_filter,
                                                  dword_t flags);
__attribute__((dllimport)) bool_t FindClose(void* handle);
__attribute__((dllimport)) bool_t SetFileInformationByHandle(void* handle,
                                                             int info_class,
                                                             const void* info,
                                                             dword_t size);

static const wchar16_t directory[] = {
    'C',':','\\','t','l','_','f','i','l','e','_','m','e','t','a','_',0x00E9,0};
static const wchar16_t main_file[] = {
    'C',':','\\','t','l','_','f','i','l','e','_','m','e','t','a','_',0x00E9,
    '\\','M','e','t','a','_','A','.','T','X','T',0};
static const wchar16_t pattern_question[] = {
    'C',':','\\','t','l','_','f','i','l','e','_','m','e','t','a','_',0x00E9,
    '\\','m','e','t','a','_','?','.','t','x','t',0};
static const wchar16_t pattern_all[] = {
    'C',':','\\','t','l','_','f','i','l','e','_','m','e','t','a','_',0x00E9,
    '\\','*','.','*',0};
static const wchar16_t cancel_file[] = {
    'C',':','\\','t','l','_','f','i','l','e','_','m','e','t','a','_',0x00E9,
    '\\','c','a','n','c','e','l','.','t','m','p',0};
static const wchar16_t close_file[] = {
    'C',':','\\','t','l','_','f','i','l','e','_','m','e','t','a','_',0x00E9,
    '\\','c','l','o','s','e','.','t','m','p',0};
static const wchar16_t posix_file[] = {
    'C',':','\\','t','l','_','f','i','l','e','_','m','e','t','a','_',0x00E9,
    '\\','p','o','s','i','x','.','t','m','p',0};

static void* create_file(const wchar16_t* path) {
    return CreateFileW(path, 0xC0000000UL, 0, (void*)0, 2, 0, (void*)0);
}

static int exists(const wchar16_t* path) {
    return GetFileAttributesW(path) != 0xFFFFFFFFUL;
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    static const char output[] = "file-metadata\n";
    static const char payload[] = "data";
    find_data_w_t find_data = {0};
    file_basic_info_t basic = {0};
    dword_t written = 0;
    unsigned char delete_value = 1;
    unsigned char keep_value = 0;
    dword_t disposition_flags = 0x13UL;
    void* invalid = (void*)~0ULL;

    CreateDirectoryW(directory, (void*)0);
    void* file = create_file(main_file);
    if (file == (void*)0) ExitProcess(1);
    if (!WriteFile(file, payload, 4, &written, (void*)0) || written != 4)
        ExitProcess(2);
    if (!SetFileAttributesW(main_file, 0x21UL) ||
        (GetFileAttributesW(main_file) & 1UL) == 0) ExitProcess(3);
    basic.file_attributes = 0x20UL;
    if (!SetFileInformationByHandle(file, 0, &basic, sizeof(basic)) ||
        (GetFileAttributesW(main_file) & 1UL) != 0) ExitProcess(4);

    void* find = FindFirstFileExW(pattern_question, 0, &find_data, 0, (void*)0, 0);
    if (find == invalid || find_data.size_low != 4 || find_data.write_high == 0)
        ExitProcess(5);
    FindClose(find);
    find = FindFirstFileExW(pattern_all, 1, &find_data, 0, (void*)0, 2);
    if (find == invalid) ExitProcess(6);
    FindClose(find);
    if (!CloseHandle(file)) ExitProcess(7);

    file = create_file(cancel_file);
    if (file == (void*)0 ||
        !SetFileInformationByHandle(file, 4, &delete_value, 1) ||
        !SetFileInformationByHandle(file, 4, &keep_value, 1) ||
        !CloseHandle(file) || !exists(cancel_file)) ExitProcess(8);
    DeleteFileW(cancel_file);

    file = create_file(close_file);
    if (file == (void*)0 ||
        !SetFileInformationByHandle(file, 4, &delete_value, 1) ||
        !CloseHandle(file) || exists(close_file)) ExitProcess(9);

    file = create_file(posix_file);
    if (file == (void*)0 || !SetFileAttributesW(posix_file, 0x21UL) ||
        !SetFileInformationByHandle(file, 21, &disposition_flags, 4) ||
        exists(posix_file) || !CloseHandle(file)) ExitProcess(10);

    SetFileAttributesW(main_file, 0x20UL);
    DeleteFileW(main_file);
    RemoveDirectoryW(directory);
    if (!WriteFile(GetStdHandle((dword_t)-11), output, sizeof(output) - 1,
                   &written, (void*)0)) ExitProcess(11);
    ExitProcess(0);
}
