.text

# Os dois pontos de entrada preservam uma fotografia do chamador Microsoft x64
# antes de qualquer prólogo do host. O CONTEXT começa em RSP e ocupa 1232
# bytes; reservamos 1240 para manter o alinhamento de 16 bytes antes da call
# System V seguinte.
.macro capture_context
    subq $1240, %rsp
    movq %r11, 208(%rsp)
    movq %rax, 120(%rsp)
    movq %rcx, 128(%rsp)
    movq %rdx, 136(%rsp)
    movq %rbx, 144(%rsp)
    leaq 1248(%rsp), %rax
    movq %rax, 152(%rsp)
    movq %rbp, 160(%rsp)
    movq %rsi, 168(%rsp)
    movq %rdi, 176(%rsp)
    movq %r8, 184(%rsp)
    movq %r9, 192(%rsp)
    movq %r10, 200(%rsp)
    movq %r12, 216(%rsp)
    movq %r13, 224(%rsp)
    movq %r14, 232(%rsp)
    movq %r15, 240(%rsp)
    movq 1240(%rsp), %rax
    movq %rax, 248(%rsp)
    movl $0x0010000b, 48(%rsp)
    stmxcsr 52(%rsp)
    pushfq
    popq %rax
    movl %eax, 68(%rsp)
    movdqu %xmm0, 416(%rsp)
    movdqu %xmm1, 432(%rsp)
    movdqu %xmm2, 448(%rsp)
    movdqu %xmm3, 464(%rsp)
    movdqu %xmm4, 480(%rsp)
    movdqu %xmm5, 496(%rsp)
    movdqu %xmm6, 512(%rsp)
    movdqu %xmm7, 528(%rsp)
    movdqu %xmm8, 544(%rsp)
    movdqu %xmm9, 560(%rsp)
    movdqu %xmm10, 576(%rsp)
    movdqu %xmm11, 592(%rsp)
    movdqu %xmm12, 608(%rsp)
    movdqu %xmm13, 624(%rsp)
    movdqu %xmm14, 640(%rsp)
    movdqu %xmm15, 656(%rsp)
.endm

.globl tl_RaiseException
.type tl_RaiseException, @function
tl_RaiseException:
    capture_context
    movq %rsp, %rdi
    movq 128(%rsp), %rsi
    movq 136(%rsp), %rdx
    movq 184(%rsp), %rcx
    movq 192(%rsp), %r8
    callq tl_dispatch_raised_exception_from_asm
    ud2
.size tl_RaiseException, .-tl_RaiseException

.globl tl_RtlUnwind
.type tl_RtlUnwind, @function
tl_RtlUnwind:
    capture_context
    movq %rsp, %rdi
    movq 128(%rsp), %rsi
    movq 136(%rsp), %rdx
    movq 184(%rsp), %rcx
    movq 192(%rsp), %r8
    callq tl_dispatch_rtl_unwind_from_asm
    ud2
.size tl_RtlUnwind, .-tl_RtlUnwind

.globl tl_restore_guest_context_and_jump
.type tl_restore_guest_context_and_jump, @function
tl_restore_guest_context_and_jump:
    # System V: RDI = ContextAmd64*. RAX mantém a base até o último load.
    movq %rdi, %rax
    movdqu 416(%rax), %xmm0
    movdqu 432(%rax), %xmm1
    movdqu 448(%rax), %xmm2
    movdqu 464(%rax), %xmm3
    movdqu 480(%rax), %xmm4
    movdqu 496(%rax), %xmm5
    movdqu 512(%rax), %xmm6
    movdqu 528(%rax), %xmm7
    movdqu 544(%rax), %xmm8
    movdqu 560(%rax), %xmm9
    movdqu 576(%rax), %xmm10
    movdqu 592(%rax), %xmm11
    movdqu 608(%rax), %xmm12
    movdqu 624(%rax), %xmm13
    movdqu 640(%rax), %xmm14
    movdqu 656(%rax), %xmm15
    ldmxcsr 52(%rax)
    pushq 68(%rax)
    popfq
    movq 128(%rax), %rcx
    movq 136(%rax), %rdx
    movq 144(%rax), %rbx
    movq 160(%rax), %rbp
    movq 168(%rax), %rsi
    movq 176(%rax), %rdi
    movq 184(%rax), %r8
    movq 192(%rax), %r9
    movq 200(%rax), %r10
    movq 208(%rax), %r11
    movq 216(%rax), %r12
    movq 224(%rax), %r13
    movq 232(%rax), %r14
    movq 240(%rax), %r15
    movq 152(%rax), %rsp
    pushq 248(%rax)
    movq 120(%rax), %rax
    retq
.size tl_restore_guest_context_and_jump, .-tl_restore_guest_context_and_jump

.section .note.GNU-stack,"",@progbits
