.text
.globl tl_call_guest_on_stack
.type tl_call_guest_on_stack, @function
tl_call_guest_on_stack:
    # SysV arguments: RDI = guest entry, RSI = top of guest stack.
    # Keep the host stack in the Windows nonvolatile R12 register; a valid
    # Microsoft x64 guest must preserve it across the call.
    pushq %r12
    movq %rsp, %r12
    movq %rsi, %rsp
    andq $-16, %rsp
    # The caller's RSP is 16-byte aligned; reserve the 32-byte Windows
    # shadow space while keeping the entry-point RSP aligned as Windows does.
    subq $32, %rsp
    callq *%rdi
    movq %r12, %rsp
    popq %r12
    retq
.size tl_call_guest_on_stack, .-tl_call_guest_on_stack
