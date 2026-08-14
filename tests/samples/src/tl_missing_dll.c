typedef void* handle_t;
typedef unsigned int uint_t;

__attribute__((dllimport)) int MessageBoxA(handle_t hwnd, const char* text, const char* caption,
                                           uint_t type);

void tl_entry(void) {
    MessageBoxA((handle_t)0, (const char*)0, (const char*)0, 0U);
}
