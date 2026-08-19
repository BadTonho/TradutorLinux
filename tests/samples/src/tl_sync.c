typedef unsigned long dword_t;
typedef long signed_count_t;
typedef int bool_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(const void* handle, const void* buffer,
                                             dword_t bytes_to_write, dword_t* bytes_written,
                                             const void* overlapped);
__attribute__((dllimport)) int CloseHandle(const void* handle);
__attribute__((dllimport)) void* CreateEventA(const void* security_attributes, int manual_reset,
                                               int initial_state, const char* name);
__attribute__((dllimport)) int SetEvent(const void* event_handle);
__attribute__((dllimport)) int ResetEvent(const void* event_handle);
__attribute__((dllimport)) void* CreateSemaphoreA(const void* security_attributes,
                                                   signed_count_t initial_count,
                                                   signed_count_t maximum_count,
                                                   const char* name);
__attribute__((dllimport)) int ReleaseSemaphore(const void* semaphore,
                                                signed_count_t release_count,
                                                signed_count_t* previous_count);
__attribute__((dllimport)) void* CreateMutexA(const void* security_attributes, int initial_owner,
                                              const char* name);
__attribute__((dllimport)) int ReleaseMutex(const void* mutex);
__attribute__((dllimport)) dword_t WaitForSingleObject(const void* handle, dword_t milliseconds);
__attribute__((dllimport)) dword_t WaitForMultipleObjects(dword_t count, const void* const* handles,
                                                           int wait_all, dword_t milliseconds);

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

    void* auto_event = CreateEventA((void*)0, 0, 0, (const char*)0);
    if (auto_event == (void*)0 ||
        WaitForSingleObject(auto_event, 0) != 0x102U ||
        !SetEvent(auto_event) || WaitForSingleObject(auto_event, 0) != 0U ||
        WaitForSingleObject(auto_event, 0) != 0x102U) {
        fail(output, &bytes_written, 1U);
    }

    void* manual_event = CreateEventA((void*)0, 1, 1, (const char*)0);
    if (manual_event == (void*)0 || WaitForSingleObject(manual_event, 0) != 0U ||
        WaitForSingleObject(manual_event, 0) != 0U || !ResetEvent(manual_event) ||
        WaitForSingleObject(manual_event, 0) != 0x102U) {
        fail(output, &bytes_written, 2U);
    }

    void* semaphore = CreateSemaphoreA((void*)0, 0, 2, (const char*)0);
    signed_count_t previous = -1;
    if (semaphore == (void*)0 || !ReleaseSemaphore(semaphore, 2, &previous) || previous != 0 ||
        WaitForSingleObject(semaphore, 0) != 0U ||
        WaitForSingleObject(semaphore, 0) != 0U ||
        WaitForSingleObject(semaphore, 0) != 0x102U) {
        fail(output, &bytes_written, 3U);
    }

    void* mutex = CreateMutexA((void*)0, 0, (const char*)0);
    if (mutex == (void*)0 || WaitForSingleObject(mutex, 0) != 0U ||
        WaitForSingleObject(mutex, 0) != 0U || !ReleaseMutex(mutex) || !ReleaseMutex(mutex) ||
        ReleaseMutex(mutex)) {
        /* The third release must fail because this thread no longer owns it. */
        if (mutex != (void*)0) {
            CloseHandle(mutex);
        }
        fail(output, &bytes_written, 4U);
    }

    void* first = CreateEventA((void*)0, 1, 1, (const char*)0);
    void* second = CreateEventA((void*)0, 1, 1, (const char*)0);
    const void* handles[2] = {first, second};
    if (first == (void*)0 || second == (void*)0 ||
        WaitForMultipleObjects(2, handles, 1, 0) != 0U) {
        fail(output, &bytes_written, 5U);
    }

    CloseHandle(auto_event);
    CloseHandle(manual_event);
    CloseHandle(semaphore);
    CloseHandle(mutex);
    CloseHandle(first);
    CloseHandle(second);

    static const char message[] = "sync\n";
    if (!WriteFile(output, message, sizeof(message) - 1, &bytes_written, (void*)0)) {
        ExitProcess(6U);
    }
    ExitProcess(0U);
}
