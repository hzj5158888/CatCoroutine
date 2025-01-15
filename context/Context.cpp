#include "./include/Context.h"

#include <csetjmp>
#include <ucontext.h>

int save_context(Context * ctx)
{
    ctx->first_make = false;
    return sigsetjmp(ctx->get_jmp_buf(), SAVE_SIG_MASK);
}

void switch_context(Context * ctx)
{
    if (!ctx->first_make) [[likely]]
        longjmp(ctx->get_jmp_buf(), CONTEXT_RESTORE);
    else {
        ctx->first_make = false;
        switch_context_first_make(ctx->get_jmp_buf(), ctx->arg_reg.di, ctx->arg_reg.si);
    }
}

#if defined  __x86_64__
void make_context(
        Context * ctx,
        void * stk,
        void (*wrap)(void (*)(void*), void *),
        void (*func)(void *),
        void * arg)
{
    auto u64_bp = reinterpret_cast<uint64_t>(stk);
    auto u64_sp = u64_bp;

    ctx->jmp_reg.bp = u64_bp;
    ctx->jmp_reg.sp = u64_sp;
    ctx->jmp_reg.ip = reinterpret_cast<uint64_t>(wrap);

    ctx->arg_reg.di = reinterpret_cast<uint64_t>(func); // Arg1 func
    ctx->arg_reg.si = reinterpret_cast<uint64_t>(arg); // Arg2 arg
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