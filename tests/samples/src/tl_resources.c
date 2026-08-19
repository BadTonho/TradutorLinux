typedef unsigned short word_t;
typedef unsigned long dword_t;
typedef unsigned long long size_t_guest;
typedef int bool_t;

__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) void* GetModuleHandleW(const word_t* module_name);
__attribute__((dllimport)) void* FindResourceW(const void* module, const word_t* name,
                                                const word_t* type);
__attribute__((dllimport)) void* LoadResource(const void* module, const void* resource);
__attribute__((dllimport)) void* LockResource(const void* resource);
__attribute__((dllimport)) dword_t SizeofResource(const void* module, const void* resource);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    const word_t* const resource_name = (const word_t*)(size_t_guest)101U;
    const word_t* const resource_type = (const word_t*)(size_t_guest)10U;
    void* const module = GetModuleHandleW((const word_t*)0);
    void* const resource = FindResourceW(module, resource_name, resource_type);
    if (resource == (void*)0 || LoadResource(module, resource) == (void*)0) {
        ExitProcess(1U);
    }
    const char* const data = (const char*)LockResource(resource);
    const dword_t size = SizeofResource(module, resource);
    if (data == (const char*)0 || size != 20U) {
        ExitProcess(2U);
    }
    dword_t written = 0;
    void* const output = GetStdHandle((dword_t)-11);
    if (!WriteFile(output, data, size, &written, (const void*)0) || written != size) {
        ExitProcess(3U);
    }
    ExitProcess(0U);
}
