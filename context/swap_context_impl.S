.align 16
.global swap_context_impl
swap_context_impl:
    mov (%rsi), %rcx
    prefetchw (%rcx)
.save:
    push %rbp
    push %rbx
    push %rdi
    push %rsi
    push %r12
    push %r13
    push %r14
    push %r15
    sub $0x18, %rsp
#ifdef __CONTEXT_STACK_PROTECTOR__
    mov %fs:0x28, %rax
    mov %rax, 0x10(%rsp)
#endif
    stmxcsr 0x8(%rsp)
    fnstcw (%rsp)
    mov %rsp, (%rdi)

.switch:
    mov %rcx, %rsp
.fpu:
    cmp $0xffffffffffffffff, (%rsp)  /* -1 => first time swap */
    je .general /* skip fpu */
    fldcw (%rsp)
    ldmxcsr 0x8(%rsp)
.general:
#ifdef __CONTEXT_STACK_PROTECTOR__
    mov 0x10(%rsp), %rcx
    mov %rcx, %fs:0x28
#endif
    add $0x18, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rsi
    pop %rdi
    pop %rbx
    pop %rbp

    mov %rdx, %rax
    ret

.section .note.GNU-stack,"",%progbits