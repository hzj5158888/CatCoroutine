#include "./include/Context.h"

void switch_context(Context * ctx, int ret)
{
    if (LIKELY(!ctx->first_full_save))
	{
        ctx->assert_stack();
		_switch_context(ctx->get_jmp_buf(), ret);
	} else {
        ctx->first_full_save = false;
        ctx->assert_stack();
		switch_context_first_full_save(ctx->get_jmp_buf(), ctx->arg_reg.di, ctx->arg_reg.si);
    }
}

int swap_context(Context * from, Context * to, int ret)
{
    assert(to->jmp_reg.sp != 0);
    if (LIKELY(from != nullptr))
        return swap_context_impl(std::addressof(from->jmp_reg.sp), std::addressof(to->jmp_reg.sp), ret);

    return swap_context_impl((uint64_t*)std::addressof(from), std::addressof(to->jmp_reg.sp), ret);
}

#if defined  __x86_64__
void make_context_wrap(Context * ctx, void (*wrap)(void (*)(void*), void *))
{

    ctx->jmp_reg.sp -= Context::JMP_REG::REG_COUNT * sizeof(uint64_t);

    auto stk = reinterpret_cast<uint64_t*>(ctx->jmp_reg.sp);
    stk[Context::JMP_REG::X87CW] = ~static_cast<uint64_t>(0); // mark for first swap
    stk[Context::JMP_REG::SI] = ctx->arg_reg.si; // rsi
    stk[Context::JMP_REG::DI] = ctx->arg_reg.di; // rdi
    stk[Context::JMP_REG::BP] = ctx->jmp_reg.bp; // rbp
    stk[Context::JMP_REG::IP] = reinterpret_cast<uint64_t>(wrap); // rip
    ctx->first_full_save = false;
}

void make_context(Context * ctx, void (*func)(void*))
{
    ctx->jmp_reg.sp -= Context::JMP_REG::REG_COUNT * sizeof(uint64_t);

    auto stk = reinterpret_cast<uint64_t*>(ctx->jmp_reg.sp);
    stk[Context::JMP_REG::X87CW] = ~static_cast<uint64_t>(0); // mark for first swap
    stk[Context::JMP_REG::DI] = ctx->arg_reg.di; // rdi
    stk[Context::JMP_REG::BP] = ctx->jmp_reg.bp; // rbp
    stk[Context::JMP_REG::IP] = reinterpret_cast<uint64_t>(func); // rip
    ctx->first_full_save = false;
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