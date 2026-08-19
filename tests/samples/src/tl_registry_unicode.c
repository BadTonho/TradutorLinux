typedef unsigned long dword_t;
typedef unsigned short wchar_t_guest;
typedef int bool_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) dword_t RegCreateKeyExW(const void* key, const wchar_t_guest* subkey,
                                                   dword_t reserved, wchar_t_guest* class_name,
                                                   dword_t options, dword_t access,
                                                   const void* security_attributes, void** result,
                                                   dword_t* disposition);
__attribute__((dllimport)) dword_t RegOpenKeyExW(const void* key, const wchar_t_guest* subkey,
                                                 dword_t options, dword_t access, void** result);
__attribute__((dllimport)) dword_t RegCloseKey(const void* key);
__attribute__((dllimport)) dword_t RegSetValueExW(const void* key, const wchar_t_guest* value_name,
                                                  dword_t reserved, dword_t type,
                                                  const unsigned char* data, dword_t data_size);
__attribute__((dllimport)) dword_t RegQueryValueExW(const void* key,
                                                    const wchar_t_guest* value_name,
                                                    dword_t* reserved, dword_t* type,
                                                    unsigned char* data, dword_t* data_size);
__attribute__((dllimport)) dword_t RegDeleteValueW(const void* key,
                                                   const wchar_t_guest* value_name);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static void fail(void* output, dword_t* written, dword_t code) {
    static const char message[] = "FAIL\n";
    (void)WriteFile(output, message, sizeof(message) - 1, written, (void*)0);
    ExitProcess(code);
}

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    dword_t written = 0;
    const void* current_user = (const void*)0x80000001ULL;
    static const wchar_t_guest subkey[] = {
        'S', 'o', 'f', 't', 'w', 'a', 'r', 'e', '\\', 'T', 'L', '\\', 'U', 'n', 'i', 'c', 'o', 'd', 'e', 0};
    static const wchar_t_guest value_name[] = {'N', 'o', 'm', 'e', 0x00E9, 0};
    static const wchar_t_guest value_data[] = {'v', 'a', 'l', 'o', 'r', ' ', 0x00E9, 0};
    void* key = (void*)0;
    dword_t disposition = 0;
    const dword_t value_size = sizeof(value_data);
    if (RegCreateKeyExW(current_user, subkey, 0, (wchar_t_guest*)0, 0, 0, (void*)0, &key,
                        &disposition) != 0 || key == (void*)0 || disposition != 1U ||
        RegSetValueExW(key, value_name, 0, 1U, (const unsigned char*)value_data, value_size) != 0 ||
        RegCloseKey(key) != 0) {
        fail(output, &written, 1U);
    }

    key = (void*)0;
    if (RegOpenKeyExW(current_user, subkey, 0, 0, &key) != 0 || key == (void*)0) {
        fail(output, &written, 2U);
    }
    dword_t type = 0;
    dword_t required = 0;
    if (RegQueryValueExW(key, value_name, (dword_t*)0, &type, (unsigned char*)0, &required) != 0 ||
        type != 1U || required != value_size) {
        fail(output, &written, 3U);
    }
    unsigned char buffer[sizeof(value_data)] = {0};
    if (RegQueryValueExW(key, value_name, (dword_t*)0, &type, buffer, &required) != 0 ||
        required != value_size) {
        fail(output, &written, 4U);
    }
    for (dword_t index = 0; index < value_size; ++index) {
        if (buffer[index] != ((const unsigned char*)value_data)[index]) {
            fail(output, &written, 5U);
        }
    }
    if (RegDeleteValueW(key, value_name) != 0 || RegCloseKey(key) != 0) {
        fail(output, &written, 6U);
    }

    static const char message[] = "registry\n";
    if (!WriteFile(output, message, sizeof(message) - 1, &written, (void*)0)) {
        ExitProcess(7U);
    }
    ExitProcess(0U);
}
