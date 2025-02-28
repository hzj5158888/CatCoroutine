//
// Created by hzj on 25-1-19.
//

#include <cstdint>

#include "../context/include/Context.h"
#include "DynStackPool.h"

namespace co {

    void DynStackPool::alloc_stk(Context *ctx) {
        ctx->stk_dyn_alloc = this;
        ctx->stk_dyn_capacity = co::MAX_STACK_SIZE;
#ifdef __MEM_PMR__
        co_ctx->stk_dyn_mem = dyn_stk_pool.allocate(STACK_SIZE, 64);
#else
        ctx->stk_dyn_mem = dyn_stk_pool.allocate(STACK_SIZE);
#endif
        ctx->stk_dyn = align_stk_ptr(reinterpret_cast<uint8_t *>((uint64_t) ctx->stk_dyn_mem + co::MAX_STACK_SIZE));
        ctx->set_stack_dyn(ctx->stk_dyn);
    }

    void DynStackPool::free_stk(Context *ctx) {
#ifdef __MEM_PMR__
        dyn_stk_pool.deallocate(co_ctx->stk_dyn_mem, STACK_SIZE);
#else
        dyn_stk_pool.deallocate(ctx->stk_dyn_mem);
#endif
        ctx->stk_dyn_mem = nullptr;
        ctx->stk_dyn_capacity = {};
        ctx->stk_dyn = nullptr;
        ctx->stk_dyn_alloc = nullptr;
    }
}