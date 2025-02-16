.align 16
.global switch_context_first_full_save
.type switch_context_first_full_save, @function
switch_context_first_full_save:
	xor %rax,%rax
	mov 8(%rdi),%rbp
	mov 48(%rdi),%rsp
    mov 56(%rdi),%r12
    mov %rsi,%rdi
    mov %rdx,%rsi
	jmp *%r12
