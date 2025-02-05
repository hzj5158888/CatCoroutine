.global save_context
.type save_context, @function
save_context:
    /* set first_full_save to false */
    movb $0x0, (%rsi) /* sizeof(bool) = 1 */
    mov %rbx, (%rdi)
    mov %rbp, 8(%rdi)
    mov %r12, 16(%rdi)
    mov %r13, 24(%rdi)
    mov %r14, 32(%rdi)
    mov %r15, 40(%rdi)
    stmxcsr   64(%rdi) /* mxcsr寄存器, **4Byte** */
    fnstcw    68(%rdi) /* x87 fpu control word, **2Byte** */
    /* call instruction push rip into stack, which will effort stack save */
    /* stack top, pc of save_context() */
    pop %rax            /* pop it */
    mov %rax, 56(%rdi)  /* save rip */
    mov %rsp, 48(%rdi)  /* save rsp */
    xor %rax, %rax      /* set return value */
    jmp *56(%rdi)       /* ret without pop stack */

