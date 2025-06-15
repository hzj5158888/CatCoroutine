#include "./include/Context.h"

namespace co {
    void switch_context(Context *ctx, int ret) {
        if (LIKELY(!ctx->first_full_save)) {
            _switch_context(ctx->get_jmp_buf(), ret);
        } else {
            ctx->first_full_save = false;
            switch_context_first_full_save(ctx->get_jmp_buf(), ctx->arg_reg.di, ctx->arg_reg.si);
        }
    }

    void make_context_wrap(Context *ctx, void (*wrap)(void (*)(void *), void *)) {
        ctx->jmp_reg.sp -= Context::JMP_REG::REG_COUNT * sizeof(size_t);

        auto stk = reinterpret_cast<size_t *>(ctx->jmp_reg.sp);
        stk[Context::JMP_REG::X87CW] = mm_get_x87cw();
        stk[Context::JMP_REG::MXCSR] = mm_get_mxcsr();
        stk[Context::JMP_REG::SI] = ctx->arg_reg.si; // rsi
        stk[Context::JMP_REG::DI] = ctx->arg_reg.di; // rdi
        stk[Context::JMP_REG::BP] = ctx->jmp_reg.bp; // rbp
        stk[Context::JMP_REG::IP] = reinterpret_cast<size_t>(wrap); // rip
        ctx->first_full_save = false;
    }

    void make_context(Context *ctx, void (*func)(void *)) {
        ctx->jmp_reg.sp -= Context::JMP_REG::REG_COUNT * sizeof(size_t);

        auto stk = reinterpret_cast<size_t *>(ctx->jmp_reg.sp);
        stk[Context::JMP_REG::X87CW] = mm_get_x87cw();
        stk[Context::JMP_REG::MXCSR] = mm_get_mxcsr();
        stk[Context::JMP_REG::DI] = ctx->arg_reg.di; // rdi
        stk[Context::JMP_REG::BP] = ctx->jmp_reg.bp; // rbp
        stk[Context::JMP_REG::IP] = reinterpret_cast<size_t>(func); // rip
        ctx->first_full_save = false;
    }
}