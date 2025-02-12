//
// Created by hzj on 25-1-14.
//

#include <cstdint>
#include <memory>
#include <cstring>
#include <cassert>

#include "../include/CoPrivate.h"
#include "BitSetLockFree.h"
#include "utils.h"
#include "include/StackPool.h"

bool stack_data_verify(Context * ctx)
{
	auto * dyn = reinterpret_cast<uint8_t *>(ctx->stk_dyn_mem);
	if (dyn == nullptr)
		return true;

	auto * stk = reinterpret_cast<uint8_t *>(ctx->jmp_reg.sp);
	auto * stk_end = reinterpret_cast<uint8_t *>(ctx->stk_real_bottom);
	for (int i = 0; std::addressof(stk[i]) != stk_end; i++)
	{
		if (stk[i] != dyn[i])
			return false;
	}

	return true;
}

StackPool::StackPool()
{
	/* set all true */
	freed_stack.flip();
}

void StackPool::alloc_dyn_stk_mem(void * &mem_ptr, std::size_t size)
{
#ifdef __MEM_PMR__
	mem_ptr = dyn_stk_saver_pool.allocate(size, 64);
#else
	mem_ptr = dyn_stk_saver_pool.allocate(size + StackInfo::STACK_RESERVE);
#endif
}

void StackPool::write_back(StackInfo * info)
{
	Co_t * co = info->occupy_co;
	if (co->ctx.stk_dyn_mem == nullptr)
	{
		co->ctx.stk_dyn_saver_alloc = &dyn_stk_saver_pool;
		co->ctx.stk_dyn_capacity = 3 * co->ctx.stk_size / 2; // 1.5 * stk_size
		alloc_dyn_stk_mem(co->ctx.stk_dyn_mem, co->ctx.stk_dyn_capacity);
	} else if (co->ctx.stk_dyn_capacity <= co->ctx.stk_size)
	{
#ifdef __MEM_PMR__
		co->ctx.stk_dyn_saver_alloc->deallocate(co->ctx.stk_dyn_mem, co->ctx.stk_dyn_capacity);
#else
		co->ctx.stk_dyn_saver_alloc->deallocate(co->ctx.stk_dyn_mem);
#endif
		co->ctx.stk_dyn_saver_alloc = &dyn_stk_saver_pool;
		co->ctx.stk_dyn_capacity = 3 * co->ctx.stk_size / 2; // 1.5 * stk_size
		alloc_dyn_stk_mem(co->ctx.stk_dyn_mem, co->ctx.stk_dyn_capacity);
	}

	co->ctx.stk_is_static = false;
	co->ctx.stk_dyn_size = co->ctx.stk_size;
	std::memcpy(co->ctx.stk_dyn_mem, reinterpret_cast<void*>(co->ctx.jmp_reg.sp), co->ctx.stk_size);
	DASSERT(stack_data_verify(std::addressof(co->ctx)));
}

void StackPool::unique_assert(int stk_idx)
{
	DASSERT(!running.get(stk_idx) && !freed_stack.get(stk_idx) && !released_co.get(stk_idx));
}

void StackPool::alloc_static_stk(Co_t * co)
{
    auto stk_ptr = stk[0].get_stk_bp_ptr();
	stk[0].occupy_co = co;
    co->ctx.occupy_stack = 0;
    setup_co_static_stk(co, stk_ptr);
    if (co->ctx.stk_size > 0)
		std::memcpy(reinterpret_cast<void*>(co->ctx.jmp_reg.sp), co->ctx.stk_dyn_mem, co->ctx.stk_size);
}

/* 由coroutine自己主动调用 */
/* coroutine stack 一定不在 released 队列 */
void StackPool::destroy_stack(Co_t * co)
{
	int stk_idx = co->ctx.occupy_stack;
	stk[stk_idx].stk_status = StackInfo::FREED;
	stk[stk_idx].occupy_co = nullptr;

	if (co->ctx.stk_dyn_mem != nullptr)
#ifdef __MEM_PMR__
		co->ctx.stk_dyn_saver_alloc->deallocate(co->ctx.stk_dyn_mem, co->ctx.stk_dyn_capacity);
#else
		co->ctx.stk_dyn_saver_alloc->deallocate(co->ctx.stk_dyn_mem);
#endif

	co->ctx.stk_dyn_saver_alloc = nullptr;
	co->ctx.stk_dyn_mem = nullptr;
	co->ctx.stk_dyn_size = 0;
	co->ctx.stk_dyn_capacity = 0;
	co->ctx.stk_dyn = nullptr;
	co->ctx.stk_dyn_real_bottom = nullptr;

	co->ctx.stk_size = 0;
	co->ctx.stk_is_static = false;
	co->ctx.stk_real_bottom = nullptr;
	co->ctx.occupy_stack = BitSetLockFree<>::INVALID_INDEX;
}

/* 由coroutine主动调用 */
/* 前提，coroutine stack空闲 */
void StackPool::release_stack(Co_t * co)
{
    write_back(std::addressof(stk[0]));

	int stk_idx = co->ctx.occupy_stack;
	stk[stk_idx].stk_status = StackInfo::RELEASED;
	DEXPR(running.set(stk_idx, false);)
	//released_co.set(stk_idx, true);
}

void StackPool::setup_co_static_stk(Co_t* co, uint8_t * stk_ptr)
{
	co->ctx.stk_is_static = true;
	co->ctx.static_stk_pool = this;
	co->ctx.set_stack(stk_ptr);
}