.text
.global _switch_context
.type _switch_context, @function
_switch_context:
    mov     (%rdi),     %rbx
    mov     8(%rdi),    %rbp
    mov     16(%rdi),   %r12
    mov     24(%rdi),   %r13
    mov     32(%rdi),   %r14
    mov     40(%rdi),   %r15
    ldmxcsr 64(%rdi)    /* sse2 control word */
    fldcw   68(%rdi)    /* x87 fpu control word */
#ifdef __CONTEXT_STACK_PROTECTOR__
    mov     72(%rdi),   %rdx
    mov     %rdx,       %fs:0x28
#endif
    mov     48(%rdi),   %rsp
    prefetcht0          (%rsp)
    mov     %rsi,       %rax
    jmp     *56(%rdi)

.section .note.GNU-stack,"",%progbits
