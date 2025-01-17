.global _switch_context
.type _switch_context, @function
_switch_context:
    mov (%rdi), %rbx
    mov 8(%rdi), %rbp
    mov 16(%rdi), %r12
    mov 24(%rdi), %r13
    mov 32(%rdi), %r14
    mov 40(%rdi), %r15
    pop %rax            /* call instruction push the ret pc to stack, pop it */
    mov 48(%rdi), %rsp
    mov %rsi, %rax
    jmp *56(%rdi)
