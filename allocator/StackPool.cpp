//
// Created by hzj on 25-1-14.
//

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <cstring>
#include <cassert>
#include <optional>

#include "../utils/include/spin_lock.h"
#include "../include/CoPrivate.h"
#include "include/StackPool.h"
#include "BitSetLockFree.h"
#include "Coroutine.h"
#include "ListLockFree.h"
#include "include/MemoryPool.h"
#include "utils.h"

StackPool::StackPool()
{
	for (auto i = 0; i < co::STATIC_STK_NUM; i++)
		freed_stack.set(i, true);
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
		co->ctx.stk_dyn_capacity = 4 * co->ctx.stk_size / 3; // 1.25 * stk_size
		alloc_dyn_stk_mem(co->ctx.stk_dyn_mem, co->ctx.stk_size);
	} else if (co->ctx.stk_dyn_capacity <= co->ctx.stk_size)
	{
#ifdef __MEM_PMR__
		co->ctx.stk_dyn_saver_alloc->deallocate(co->ctx.stk_dyn_mem, co->ctx.stk_dyn_capacity);
#else
		co->ctx.stk_dyn_saver_alloc->deallocate(co->ctx.stk_dyn_mem);
#endif
		co->ctx.stk_dyn_saver_alloc = &dyn_stk_saver_pool;
		co->ctx.stk_dyn_capacity = 4 * co->ctx.stk_size / 3; // 1.25 * stk_size
		alloc_dyn_stk_mem(co->ctx.stk_dyn_mem, co->ctx.stk_dyn_capacity);
	}

	co->ctx.stk_is_static = false;
	co->ctx.stk_dyn_size = co->ctx.stk_size;
	std::memcpy(co->ctx.stk_dyn_mem, reinterpret_cast<void*>(co->ctx.jmp_reg.sp), co->ctx.stk_size);
}

void StackPool::alloc_static_stk(Co_t * co)
{
	std::lock_guard lock(m_lock);

	uint8_t * stk_ptr{};
	int32_t stk_idx{BitSetLockFree<>::INVAILD_INDEX};
	/* coroutine在freed co集合 */
	if (co->ctx.occupy_stack == BitSetLockFree<>::INVAILD_INDEX && (stk_idx = freed_stack.change_first_expect(true, false)) != BitSetLockFree<>::INVAILD_INDEX)
	{
		stk_ptr = stk[stk_idx].get_stk_bp_ptr();
		if (co->ctx.stk_dyn_size > 0)
		{
			auto dyn_size = co->ctx.stk_dyn_size;
			std::memcpy(stk_ptr - dyn_size, co->ctx.stk_dyn_mem, dyn_size);
		}
	} else if ((stk_idx = co->ctx.occupy_stack) != BitSetLockFree<>::INVAILD_INDEX && released_co.compare_exchange(co->ctx.occupy_stack, true, false))
	{
		stk_ptr = stk[stk_idx].get_stk_bp_ptr();
	} else {
		/* 从上一个条件跳转而来 */
		/* coroutine占有的stack正在被写回 */
		if (stk_idx != BitSetLockFree<>::INVAILD_INDEX)
			stk[stk_idx].wait_write_back();

		while (true)
		{
			stk_idx = released_co.change_first_expect_then(true, false, 
				[this](int32_t idx) { stk[idx].lock_write_back(); }
			);
			if (stk_idx == BitSetLockFree<>::INVAILD_INDEX)
				continue;

			auto info = std::addressof(stk[stk_idx]);
			write_back(info);
			info->occupy_co->ctx.occupy_stack = BitSetLockFree<>::INVAILD_INDEX;
			info->occupy_co = nullptr;
			info->unlock_write_back();

			stk_ptr = stk[stk_idx].get_stk_bp_ptr();
			if (co->ctx.stk_dyn_size > 0)
			{
				auto dyn_size = co->ctx.stk_dyn_size;
				std::memcpy(stk_ptr - dyn_size, co->ctx.stk_dyn_mem, dyn_size);
			}

			break;
		}
	}

	DASSERT(stk_idx != BitSetLockFree<>::INVAILD_INDEX);
	stk[stk_idx].occupy_co = co;
	stk[stk_idx].stk_status = StackInfo::ACTIVE;
	co->ctx.occupy_stack = stk_idx;
	setup_co_static_stk(co, stk_ptr);
}

/* 由coroutine自己主动调用 */
/* coroutine stack 一定不在 released 队列 */
void StackPool::destroy_stack(Co_t * co)
{
	std::lock_guard lock(m_lock);

	int stk_idx = co->ctx.occupy_stack;
	stk[stk_idx].stk_status = StackInfo::FREED;
	stk[stk_idx].occupy_co = nullptr;
	freed_stack.set(stk_idx, true);

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
	co->ctx.occupy_stack = BitSetLockFree<>::INVAILD_INDEX;
}

/* 由coroutine主动调用 */
/* 前提，coroutine stack空闲 */
void StackPool::release_stack(Co_t * co)
{
	std::lock_guard lock(m_lock);
	
	int stk_idx = co->ctx.occupy_stack;
	stk[stk_idx].stk_status = StackInfo::RELEASED;
	released_co.set(stk_idx, true);
}

void StackPool::setup_co_static_stk(Co_t* co, uint8_t * stk)
{
	co->ctx.stk_is_static = true;
	co->ctx.static_stk_pool = this;
	co->ctx.set_stack(stk);
}