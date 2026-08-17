typedef unsigned long dword_t;
typedef int bool_t;
typedef unsigned long long size_t_guest;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport, noreturn)) void ExitThread(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(void* handle, const void* buffer,
                                            dword_t bytes_to_write,
                                            dword_t* bytes_written, void* overlapped);
__attribute__((dllimport)) void* CreateThread(void* security_attributes,
                                              size_t_guest stack_size,
                                              void* start_address,
                                              void* parameter,
                                              dword_t creation_flags,
                                              dword_t* thread_id);
__attribute__((dllimport)) dword_t WaitForSingleObject(void* handle,
                                                       dword_t milliseconds);
__attribute__((dllimport)) bool_t CloseHandle(void* handle);

void thread_func(void* param) {
    void* stdout_handle = GetStdHandle((dword_t)-11);
    dword_t bytes_written = 0;
    static const char msg[] = "Thread done\r\n";
    (void)param;
    WriteFile(stdout_handle, msg, sizeof(msg) - 1, &bytes_written, (void*)0);
    ExitThread(0U);
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    void* stdout_handle = GetStdHandle((dword_t)-11);
    dword_t bytes_written = 0;

    /* Thread 1: write and finish. */
    void* t1 = CreateThread((void*)0, 0, (void*)thread_func, (void*)0, 0, (void*)0);
    if (t1 == (void*)0) {
        static const char err[] = "FAIL";
        WriteFile(stdout_handle, err, 4, &bytes_written, (void*)0);
        ExitProcess(1U);
    }
    WaitForSingleObject(t1, 0xFFFFFFFF);
    CloseHandle(t1);

    /* Thread 2: write and finish. */
    void* t2 = CreateThread((void*)0, 0, (void*)thread_func, (void*)0, 0, (void*)0);
    if (t2 == (void*)0) {
        static const char err[] = "FAIL";
        WriteFile(stdout_handle, err, 4, &bytes_written, (void*)0);
        ExitProcess(1U);
    }
    WaitForSingleObject(t2, 0xFFFFFFFF);
    CloseHandle(t2);

    static const char main_msg[] = "Main done\r\n";
    WriteFile(stdout_handle, main_msg, sizeof(main_msg) - 1, &bytes_written, (void*)0);

    ExitProcess(0U);
}
