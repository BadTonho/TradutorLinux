#include <stdint.h>

typedef struct {
    uint32_t code;
    uint32_t flags;
    void* nested_record;
    void* address;
    uint32_t parameter_count;
    uint32_t reserved;
    uint64_t parameters[15];
} exception_record_t;

typedef struct {
    exception_record_t* exception_record;
    void* context_record;
} exception_pointers_t;

void seh_unwind_target(void);

__attribute__((dllimport, noreturn)) void RaiseException(uint32_t code, uint32_t flags,
                                                         uint32_t parameter_count,
                                                         const uint64_t* parameters);
__attribute__((dllimport)) void* AddVectoredExceptionHandler(uint32_t first, void* handler);
__attribute__((dllimport)) uint32_t RemoveVectoredExceptionHandler(void* handle);
__attribute__((dllimport)) void RtlUnwindEx(void* target_frame, void* target_ip,
                                             exception_record_t* record, void* return_value,
                                             void* context, void* history);
__attribute__((dllimport)) void RtlCaptureContext(void* context);
typedef uint32_t (*thread_start_t)(void* parameter);
__attribute__((dllimport)) void* CreateThread(void* attributes, uintptr_t stack_size,
                                               thread_start_t start, void* parameter,
                                               uint32_t flags, uint32_t* thread_id);
__attribute__((dllimport)) uint32_t WaitForSingleObject(void* handle, uint32_t milliseconds);
__attribute__((dllimport)) int CloseHandle(void* handle);
__attribute__((dllimport)) void* GetStdHandle(uint32_t standard_handle);
__attribute__((dllimport)) int WriteFile(void* handle, const void* buffer, uint32_t bytes,
                                         uint32_t* written, void* overlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(uint32_t code);

static volatile uint32_t g_first_veh;
static volatile uint32_t g_last_veh;
static volatile uint32_t g_caught;
static volatile uint64_t g_unwind_return;
static volatile uint32_t g_unwind_intermediate_called;
static volatile uint32_t g_unwind_handler_called;
static volatile uint32_t g_unwind_callback_called;

static uint64_t seh_consolidate_callback(exception_record_t* record) {
    if (record != 0 && record->code == 0x80000029U && (record->flags & 2U) != 0U) {
        g_unwind_callback_called = 1;
    }
    return (uint64_t)(uintptr_t)&seh_unwind_target;
}

static exception_record_t g_unwind_record = {
    .code = 0x80000029U,
    .parameter_count = 1,
    .parameters = {(uint64_t)(uintptr_t)&seh_consolidate_callback},
};

static int seh_first_veh(exception_pointers_t* pointers) {
    if (pointers != 0 && pointers->exception_record != 0 &&
        pointers->exception_record->code == 0xE0424242U) {
        g_first_veh = 1;
    }
    return -1;  /* EXCEPTION_CONTINUE_SEARCH */
}

static int seh_last_veh(exception_pointers_t* pointers) {
    if (pointers != 0 && pointers->exception_record != 0 &&
        pointers->exception_record->code == 0xE0424242U) {
        g_last_veh = g_first_veh != 0 ? 1U : 2U;
    }
    return -1;  /* EXCEPTION_CONTINUE_SEARCH */
}

static int seh_filter(exception_pointers_t* pointers) {
    return pointers != 0 && pointers->exception_record != 0 &&
                   pointers->exception_record->code == 0xE0424242U
               ? 1  /* EXCEPTION_EXECUTE_HANDLER */
               : 0; /* EXCEPTION_CONTINUE_SEARCH */
}

static int seh_intermediate_unwind_handler(exception_record_t* record, void* establisher_frame,
                                           void* context, void* dispatcher) {
    (void)establisher_frame;
    (void)context;
    (void)dispatcher;
    if (record != 0 && (record->flags & 2U) != 0U && (record->flags & 0x20U) == 0U) {
        g_unwind_intermediate_called = 1;
    }
    return 1; /* ExceptionContinueSearch */
}

static int seh_unwind_handler(exception_record_t* record, void* establisher_frame,
                              void* context, void* dispatcher) {
    (void)establisher_frame;
    (void)context;
    (void)dispatcher;
    if (record != 0 && (record->flags & 2U) != 0U) {
        g_unwind_handler_called = 1;
    }
    return 1; /* ExceptionContinueSearch */
}

void seh_caught(void) {
    g_caught = 1;
}

void seh_probe(void);
void seh_unwind_probe(void);
void seh_unwind_intermediate(void* target_frame, void* target_ip);
void seh_unwind_inner(void* target_frame, void* target_ip);

static uint32_t seh_thread(void* parameter) {
    (void)parameter;
    seh_probe();
    return 0;
}

// Função em assembly GNU com .pdata/.xdata reais. O prólogo tem três códigos
// para que o promotor V2 possa ocupar o padding sem deslocar handler data.
__asm__(
    ".text\n"
    ".globl seh_probe\n"
    "seh_probe:\n"
    ".seh_proc seh_probe\n"
    "pushq %rbx\n"
    ".seh_pushreg %rbx\n"
    "pushq %rbp\n"
    ".seh_pushreg %rbp\n"
    "subq $40, %rsp\n"
    ".seh_stackalloc 40\n"
    ".seh_endprologue\n"
    ".Ltl_seh_try_begin:\n"
    "movl $0xe0424242, %ecx\n"
    "xorl %edx, %edx\n"
    "xorl %r8d, %r8d\n"
    "xorl %r9d, %r9d\n"
    "call RaiseException\n"
    "ud2\n"
    ".Ltl_seh_try_end:\n"
    ".Ltl_seh_caught:\n"
    "addq $40, %rsp\n"
    "popq %rbp\n"
    "popq %rbx\n"
    "call seh_caught\n"
    "ret\n"
    ".seh_handler __C_specific_handler, @except\n"
    ".seh_handlerdata\n"
    ".long 1\n"
    ".rva .Ltl_seh_try_begin, .Ltl_seh_try_end, seh_filter, .Ltl_seh_caught\n"
    ".text\n"
    ".seh_endproc\n"
    ".globl seh_unwind_probe\n"
    "seh_unwind_probe:\n"
    ".seh_proc seh_unwind_probe\n"
    "pushq %rbx\n"
    ".seh_pushreg %rbx\n"
    "pushq %rbp\n"
    ".seh_pushreg %rbp\n"
    "subq $56, %rsp\n"
    ".seh_stackalloc 56\n"
    ".seh_endprologue\n"
    "leaq 72(%rsp), %rcx\n"
    "leaq seh_unwind_target(%rip), %rdx\n"
    "call seh_unwind_intermediate\n"
    "ud2\n"
    ".globl seh_unwind_target\n"
    "seh_unwind_target:\n"
    "movq %rax, g_unwind_return(%rip)\n"
    "addq $56, %rsp\n"
    "popq %rbp\n"
    "popq %rbx\n"
    "ret\n"
    ".seh_handler seh_unwind_handler, @unwind\n"
    ".seh_handlerdata\n"
    ".text\n"
    ".seh_endproc\n"
    ".globl seh_unwind_intermediate\n"
    "seh_unwind_intermediate:\n"
    ".seh_proc seh_unwind_intermediate\n"
    "pushq %rbx\n"
    ".seh_pushreg %rbx\n"
    "pushq %rbp\n"
    ".seh_pushreg %rbp\n"
    "subq $56, %rsp\n"
    ".seh_stackalloc 56\n"
    ".seh_endprologue\n"
    "call seh_unwind_inner\n"
    "ud2\n"
    "addq $56, %rsp\n"
    "popq %rbp\n"
    "popq %rbx\n"
    "ret\n"
    ".seh_handler seh_intermediate_unwind_handler, @unwind\n"
    ".seh_handlerdata\n"
    ".text\n"
    ".seh_endproc\n"
    ".globl seh_unwind_inner\n"
    "seh_unwind_inner:\n"
    ".seh_proc seh_unwind_inner\n"
    "pushq %rbx\n"
    ".seh_pushreg %rbx\n"
    "pushq %rbp\n"
    ".seh_pushreg %rbp\n"
    "subq $56, %rsp\n"
    ".seh_stackalloc 56\n"
    ".seh_endprologue\n"
    "movq %rcx, %rbx\n"
    "movq %rdx, %rbp\n"
    "leaq .Ltl_seh_context(%rip), %rcx\n"
    "call RtlCaptureContext\n"
    "movq %rbx, %rcx\n"
    "movq %rbp, %rdx\n"
    "leaq g_unwind_record(%rip), %r8\n"
    "movabsq $0x1122334455667788, %r9\n"
    "leaq .Ltl_seh_context(%rip), %rax\n"
    "movq %rax, 32(%rsp)\n"
    "movq $0, 40(%rsp)\n"
    "call RtlUnwindEx\n"
    "ud2\n"
    "addq $56, %rsp\n"
    "popq %rbp\n"
    "popq %rbx\n"
    "ret\n"
    ".text\n"
    ".seh_endproc\n"
    ".bss\n"
    ".p2align 4\n"
    ".Ltl_seh_context:\n"
    ".zero 1232\n"
    ".text\n"
);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;
__attribute__((used, section(".rdata"))) void (*const tl_unwindex_anchor)(void) =
    (void (*)(void))RtlUnwindEx;

void tl_entry(void) {
    void* const last = AddVectoredExceptionHandler(0, (void*)seh_last_veh);
    void* const first = AddVectoredExceptionHandler(1, (void*)seh_first_veh);
    if (last == 0 || first == 0 || RemoveVectoredExceptionHandler(first) == 0) {
        ExitProcess(2);
    }
    // Reinstala o primeiro para provar que o token é opaco e removível.
    void* const first_again = AddVectoredExceptionHandler(1, (void*)seh_first_veh);
    if (first_again == 0) {
        ExitProcess(3);
    }
    seh_unwind_probe();
    if (g_unwind_return != 0x1122334455667788ULL) {
        ExitProcess(4);
    }
    if (g_unwind_intermediate_called == 0) {
        ExitProcess(10);
    }
    if (g_unwind_handler_called == 0) {
        ExitProcess(8);
    }
    if (g_unwind_callback_called == 0) {
        ExitProcess(9);
    }
    void* const thread = CreateThread(0, 0, seh_thread, 0, 0, 0);
    if (thread == 0 || WaitForSingleObject(thread, 0xffffffffU) != 0 || CloseHandle(thread) == 0) {
        ExitProcess(5);
    }
    if (g_first_veh == 0 || g_last_veh != 1 || g_caught == 0) {
        ExitProcess(6);
    }
    static const char message[] = "seh\n";
    uint32_t written = 0;
    if (!WriteFile(GetStdHandle((uint32_t)-11), message, sizeof(message) - 1, &written, 0) ||
        written != sizeof(message) - 1) {
        ExitProcess(7);
    }
    ExitProcess(0);
}
