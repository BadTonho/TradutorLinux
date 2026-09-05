typedef unsigned long dword_t;

__attribute__((dllimport)) int CompatEntry(void);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);

void tl_entry(void) {
    ExitProcess(CompatEntry() ? 0U : 1U);
}
