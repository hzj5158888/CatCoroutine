#include "./include/Context.h"

#include <csetjmp>
#include <ucontext.h>

#include "../include/CoPrivate.h"

void switch_context(Context * ctx)
{
    if (!ctx->first_full_save) [[likely]]
	{
		_switch_context(ctx->get_jmp_buf(), CONTEXT_RESTORE);
	} else {
        ctx->first_full_save = false;
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