.global switch_context_first_full_save
.type switch_context_first_full_save, @function
switch_context_first_full_save:
	xor %eax,%eax
	cmp $1,%esi             /* CF = val ? 0 : 1 */
	adc %esi,%eax           /* eax = val + !val */
	mov (%rdi),%rbx         /* rdi is the jmp_buf, restore regs from it */
	mov 8(%rdi),%rbp
	mov 48(%rdi),%rsp
    mov 56(%rdi),%r12
    mov %rsi,%rdi
    mov %rdx,%rsi
	jmp *%r12          /* goto saved address without altering rsp */
