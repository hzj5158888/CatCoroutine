.global save_context
.type save_context, @function
save_context:
    /* set first_full_save to false */
    movl $0x0, (%rsi) /* sizeof(bool) = 4 */
    mov %rbx, (%rdi)
    mov %rbp, 8(%rdi)
    mov %r12, 16(%rdi)
    mov %r13, 24(%rdi)
    mov %r14, 32(%rdi)
    mov %r15, 40(%rdi)
    /* call instruction push rip into stack, which effort stack save */
    /* stack 0, the pc of save_context() */
    pop %rax            /* pop and throw it */
    mov %rax, 56(%rdi)  /* save rip */
    mov %rsp, 48(%rdi)
    xor %rax, %rax      /* set return value */
    jmp *56(%rdi)       /* ret without pop stack */

