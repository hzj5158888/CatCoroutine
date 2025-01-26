//
// Created by hzj on 25-1-19.
//

#include <cstdint>

#include "../context/include/Context.h"
#include "DynStackPool.h"
#include "../utils/include/utils.h"

void DynStackPool::alloc_stk(Context * ctx)
{
    ctx->stk_dyn_alloc = this;
    ctx->stk_dyn_capacity = co::MAX_STACK_SIZE;
    ctx->stk_dyn_mem = dyn_stk_pool.allocate_safe(STACK_SIZE);
    //ctx->stk_dyn_mem = std::malloc(STACK_SIZE);
    ctx->stk_dyn = align_stk_ptr(reinterpret_cast<uint8_t*>((uint64_t)ctx->stk_dyn_mem + co::MAX_STACK_SIZE));
    ctx->set_stack_dyn(ctx->stk_dyn);
}

void DynStackPool::free_stk(Context * ctx)
{
    dyn_stk_pool.free_safe(ctx->stk_dyn_mem);
    //std::free(ctx->stk_dyn_mem);
    ctx->stk_dyn_mem = nullptr;
    ctx->stk_dyn_capacity = {};
    ctx->stk_dyn = nullptr;
    ctx->stk_dyn_alloc = nullptr;
}
