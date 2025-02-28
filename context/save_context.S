.text
.global save_context
.type save_context, @function
/* Mark that we don't need executable stack.  */
save_context:
    /* set first_full_save to false */
    movb    $0x0,   (%rsi)    /* sizeof(bool) = 1 */
    mov     %rbx,   (%rdi)
    mov     %rbp,   8(%rdi)
    mov     %r12,   16(%rdi)
    mov     %r13,   24(%rdi)
    mov     %r14,   32(%rdi)
    mov     %r15,   40(%rdi)
    stmxcsr         64(%rdi)  /* mxcsr寄存器, 4Byte */
    fnstcw          68(%rdi)  /* x87 fpu control word, 4Byte */
#ifdef __CONTEXT_STACK_PROTECTOR__
    mov     %fs:0x28,   %rsi        /* read stack guard from TLS record */
    mov     %rsi,       72(%rdi)    /* save stack guard */
#endif
    pop     %rax
    mov     %rax,   56(%rdi)
    mov     %rsp,   48(%rdi)
    xor     %rax,   %rax
    jmp     *56(%rdi)

.section .note.GNU-stack,"",%progbits
