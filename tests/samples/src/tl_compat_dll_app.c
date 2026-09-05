typedef unsigned long dword_t;

__attribute__((dllimport)) void* LoadLibraryA(const char* file_name);
__attribute__((dllimport)) void* GetProcAddress(void* module, const char* proc_name);
__attribute__((dllimport)) int FreeLibrary(void* module);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);

void tl_entry(void);
void (*volatile tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    typedef int (*compat_entry_t)(void);
    static const char module_name[] = "compat.dll";
    static const char symbol_name[] = "CompatEntry";
    if (tl_relocation_anchor == (void (*)(void))0) ExitProcess(5U);
    void* const module = LoadLibraryA(module_name);
    if (module == (void*)0) ExitProcess(1U);
    compat_entry_t const entry = (compat_entry_t)GetProcAddress(module, symbol_name);
    if (entry == (compat_entry_t)0) ExitProcess(2U);
    const int result = entry();
    if (!FreeLibrary(module)) ExitProcess(3U);
    ExitProcess(result == 1 ? 0U : 4U);
}
