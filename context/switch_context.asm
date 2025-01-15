.global switch_context_first_make
.global _switch_context_first_make
.type switch_context_first_make, @function
.type _switch_context_first_make, @function
_switch_context_first_make:
switch_context_first_make:
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
