typedef unsigned long dword_t;

__attribute__((dllimport)) int MessageBoxA(const void* owner, const char* text,
                                            const char* caption, dword_t type);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);

void tl_entry(void) {
    if (MessageBoxA((void*)0, "TradutorLinux GUI", "Fase 7", 0) == 0) {
        ExitProcess(1U);
    }
    ExitProcess(0U);
}
