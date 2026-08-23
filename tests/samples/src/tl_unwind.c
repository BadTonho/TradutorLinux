#include <stdint.h>

#ifndef TL_UNWIND_MESSAGE
#define TL_UNWIND_MESSAGE "unwind\n"
#endif

typedef struct __attribute__((aligned(16))) {
    uint64_t low;
    int64_t high;
} m128a_t;

typedef struct __attribute__((aligned(16))) {
    uint64_t home[6];
    uint32_t context_flags;
    uint32_t mx_csr;
    uint16_t segments[6];
    uint32_t e_flags;
    uint64_t debug[6];
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rbx;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    m128a_t floating[32];
    m128a_t vector_register[26];
    uint64_t vector_control;
    uint64_t debug_control;
    uint64_t last_branch_to_rip;
    uint64_t last_branch_from_rip;
    uint64_t last_exception_to_rip;
    uint64_t last_exception_from_rip;
} context_amd64_t;

typedef struct {
    uint32_t begin_address;
    uint32_t end_address;
    uint32_t unwind_data;
} runtime_function_t;

__attribute__((dllimport)) void RtlCaptureContext(context_amd64_t* context);
__attribute__((dllimport)) runtime_function_t* RtlLookupFunctionEntry(uint64_t control_pc,
                                                                        uint64_t* image_base,
                                                                        void* history_table);
__attribute__((dllimport)) void* RtlVirtualUnwind(uint32_t handler_type, uint64_t image_base,
                                                   uint64_t control_pc, runtime_function_t* function_entry,
                                                   context_amd64_t* context, void** handler_data,
                                                   uint64_t* establisher_frame, void* context_pointers);
__attribute__((dllimport)) void* RtlPcToFileHeader(void* pc, void** image_base);
__attribute__((dllimport)) void* GetStdHandle(uint32_t standard_handle);
__attribute__((dllimport)) int WriteFile(void* handle, const void* buffer, uint32_t bytes,
                                         uint32_t* written, void* overlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(uint32_t code);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static volatile int g_unwound;

__attribute__((noinline)) static void unwind_probe(void) {
    context_amd64_t context = {0};
    uint64_t image_base = 0;
    uint64_t establisher = 0;
    void* file_header = 0;
    RtlCaptureContext(&context);
    runtime_function_t* const entry = RtlLookupFunctionEntry(context.rip, &image_base, 0);
    if (entry == 0 || image_base == 0 || RtlPcToFileHeader((void*)(uintptr_t)context.rip, &file_header) !=
        (void*)(uintptr_t)image_base || file_header == 0) {
        return;
    }
    (void)RtlVirtualUnwind(0, image_base, context.rip, entry, &context, 0, &establisher, 0);
    if (context.rip != 0 && context.rsp != 0 && establisher != 0) {
        g_unwound = 1;
    }
}

void tl_entry(void) {
    unwind_probe();
    if (g_unwound == 0) {
        ExitProcess(2);
    }
    static const char message[] = TL_UNWIND_MESSAGE;
    uint32_t written = 0;
    if (!WriteFile(GetStdHandle((uint32_t)-11), message, sizeof(message) - 1, &written, 0) ||
        written != sizeof(message) - 1) {
        ExitProcess(3);
    }
    ExitProcess(0);
}
