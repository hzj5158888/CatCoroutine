#include "./include/Context.h"

#include <csetjmp>
#include <ucontext.h>

int aware_switch_context_impl(Context * from, Context * to, int ret)
{
    int res{};
    /* change to stack sp ptr */
    to = reinterpret_cast<Context*>(std::addressof(to->jmp_reg.sp));
    if (LIKELY(from != nullptr))
    {
        /* change to stack sp ptr */
        from->first_full_save = false;
        from = reinterpret_cast<Context*>(std::addressof(from->jmp_reg.sp));
        asm volatile (
                "push %%rbp;"
                "sub $0x8, %%rsp;"
                "mov %%rsp, (%%rdi);" // store rsp to arg 1
                "lea (%%rip), %%r12;" // store rip reg
                "mov %%r12, (%%rsp);"
                "jmp *%%rsi;"
                "mov (%%rsi), %%rsp;" // restore rsp from arg 2
                "pop %%rax;"
                "pop %%rbp;" // restore rbp
                "mov %%rdx, %%rax;" // pass "ret"
                : "=m"(res)
                :: "rbx","r12","r13","r14","r15","memory"
        );
    } else {
        asm volatile ("jmp *%%rdi" ::: "memory");
    }
    /* all argument variable including <from, to, ret> are now invalid */

    return res;
}

int aware_switch_context_first_save_impl(Context * from, Context * to, int ret)
{
    int res = 0;

    /* change to stack sp ptr */
    to = reinterpret_cast<Context*>(std::addressof(to->jmp_reg.sp));
    if (LIKELY(from != nullptr))
    {
        /* change to stack sp ptr */
        from->first_full_save = false;
        from = reinterpret_cast<Context*>(std::addressof(from->jmp_reg.sp));
        asm volatile (
                "push %%rbp;"
                "sub $0x8, %%rsp;"
                "mov %%rsp, (%%rdi);" // store rsp to arg 1
                "lea .exit, %%r12;" // store rip reg
                "mov %%r12, (%%rsp);"
                "mov (%%rsi), %%rsp;" // load rsp from arg 2
                "pop %%r12;" // load func addr
                "pop %%rsi;" // load arg 2
                "pop %%rdi;" // load arg 1
                "pop %%rbp;" // load rbp
                "mov %%rdx, %%rax;" // pass "ret"
                "jmp *%%r12;"
                ".exit:"
                : "=m"(res)
                :: "rbx","r12","r13","r14","r15","memory"
        );
    } else {
        asm volatile (
                "mov (%%rsi), %%rsp;" // load rsp from arg 2
                "pop %%r12;" // load func addr
                "pop %%rsi;" // load arg 2
                "pop %%rdi;" // load arg 1
                "pop %%rbp;" // load rbp
                "mov %%rdx, %%rax;" // pass "ret"
                "jmp *%%r12;"
                : "=m"(res)
                :: "memory"
        );
    }
    /* all argument variable including <from, to, ret> are now invalid */

    return res;
}

int aware_switch_context(Context * from, Context * to, int ret)
{
    if (LIKELY(!to->first_full_save))
        return aware_switch_context_impl(from, to, ret);

    return aware_switch_context_first_save_impl(from, to, ret);
}

/* after allocated stack */
void aware_make_context(Context * ctx, void (*func)(void*), void * arg)
{
    constexpr auto elem_count = 4;

    ctx->jmp_reg.sp -= elem_count * sizeof(void *);
    auto stk = reinterpret_cast<void**>(ctx->jmp_reg.sp);
    stk[3] = reinterpret_cast<void *>(func); // func addr
    stk[2] = nullptr; // arg 2
    stk[1] = arg; // arg 1
    stk[0] = reinterpret_cast<void*>(ctx->jmp_reg.bp); // rbp
}

/* after allocated stack */
void aware_make_context_wrap(
        Context * ctx,
        void (*wrap)(void (*)(void*), void *),
        void (*func)(void*),
        void * arg)
{
    constexpr auto elem_count = 4;

    ctx->jmp_reg.sp -= elem_count * sizeof(void *);
    auto stk = reinterpret_cast<void**>(ctx->jmp_reg.sp);
    stk[3] = reinterpret_cast<void *>(wrap); // func addr
    stk[2] = arg; // arg 2
    stk[1] = reinterpret_cast<void*>(func); // arg 1
    stk[0] = reinterpret_cast<void*>(ctx->jmp_reg.bp); // rbp
}

void switch_context(Context * ctx, int ret)
{
    if (!ctx->first_full_save) [[likely]]
	{
        ctx->assert_stack();
		_switch_context(ctx->get_jmp_buf(), ret);
	} else {
        ctx->first_full_save = false;
        ctx->assert_stack();
		switch_context_first_full_save(ctx->get_jmp_buf(), ctx->arg_reg.di, ctx->arg_reg.si);
    }
}

#if defined  __x86_64__
void make_context_wrap(
        Context * ctx,
        void (*wrap)(void (*)(void*), void *),
        void (*func)(void *),
        void * arg)
{
    ctx->jmp_reg.bp = 0;
    ctx->jmp_reg.sp = 0;
    ctx->jmp_reg.ip = reinterpret_cast<uint64_t>(wrap);

    ctx->arg_reg.di = reinterpret_cast<uint64_t>(func); // Arg1 func
    ctx->arg_reg.si = reinterpret_cast<uint64_t>(arg); // Arg2 arg
}

void make_context(
		Context * ctx,
		void (*func)(void*),
		void * arg)
{
	ctx->jmp_reg.bp = 0;
	ctx->jmp_reg.sp = 0;
	ctx->jmp_reg.ip = reinterpret_cast<uint64_t>(func);

	ctx->arg_reg.di = reinterpret_cast<uint64_t>(arg);
}

#else
void make_context(Context * ctx, void * bp, void (*func)(void *), void * arg)
{
    ctx->jmp_reg.bp = bp;
    ctx->jmp_reg.sp = bp;
    ctx->jmp_reg.ip = reinterpret_cast<void*>(func);
    ctx->jmp_reg.bx = arg;
}
#endif