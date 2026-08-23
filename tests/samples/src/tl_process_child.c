typedef unsigned long dword_t;
typedef int bool_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) dword_t GetEnvironmentVariableW(const unsigned short* name,
                                                            unsigned short* value, dword_t size);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t written = 0;
    static const unsigned short environment_name[] = {
        'T','L','_','P','R','O','C','E','S','S','_','E','N','V',0};
    static const unsigned short expected_value[] = {'c','h','i','l','d',0};
    unsigned short environment_value[16] = {0};
    if (GetEnvironmentVariableW(environment_name, environment_value, 16U) != 5U ||
        environment_value[0] != expected_value[0] || environment_value[1] != expected_value[1] ||
        environment_value[2] != expected_value[2] || environment_value[3] != expected_value[3] ||
        environment_value[4] != expected_value[4] || environment_value[5] != 0) {
        ExitProcess(9U);
    }
    static const char message[] = "child\n";
    if (!WriteFile(output, message, sizeof(message) - 1, &written, (void*)0)) {
        ExitProcess(8U);
    }
    ExitProcess(7U);
}
