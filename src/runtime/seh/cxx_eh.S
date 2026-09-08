.text

.globl tl_cxx_catch_return_trampoline
.type tl_cxx_catch_return_trampoline, @function
tl_cxx_catch_return_trampoline:
    # O funclet devolve em RAX o destino do catchret. Preserve o RSP após o
    # ret e chame o callback System V para validar o destino e restaurar o
    # CONTEXT convidado salvo antes do salto ao funclet.
    movq %rax, %rdi
    movq %rsp, %rsi
    andq $-16, %rsp
    subq $8, %rsp
    callq tl_cxx_catch_return_from_asm
    ud2
.size tl_cxx_catch_return_trampoline, .-tl_cxx_catch_return_trampoline

.globl tl_cxx_cleanup_return_trampoline
.type tl_cxx_cleanup_return_trampoline, @function
tl_cxx_cleanup_return_trampoline:
    # Um cleanupret normalizado pelo LLVM devolve com RET. O callback recebe
    # apenas o RSP após esse RET e restaura o contexto original do frame para
    # que o catch funclet selecionado seja executado.
    movq %rsp, %rdi
    andq $-16, %rsp
    subq $8, %rsp
    callq tl_cxx_cleanup_return_from_asm
    ud2
.size tl_cxx_cleanup_return_trampoline, .-tl_cxx_cleanup_return_trampoline

.section .note.GNU-stack,"",@progbits
