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
#include "ListLockFree.h"
#include "include/MemoryPool.h"

StackPool::StackPool()
{
	for (size_t i = 0; i < co::STATIC_STK_NUM; i++)
		freed_stack.push_back(i);
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
	int stk_idx{-1};
	uint8_t * stk_ptr{};
	std::optional<uint16_t> stk_idx_opt;
	/* coroutine在freed co集合 */
	auto iter = co->ctx.freed_co_iter;
	std::unique_lock<spin_lock> stk_guard;
	if (co->ctx.freed_co_iter.valid() && (stk_guard = stk[*iter].tryLock()).owns_lock())
	{
		stk_idx = *iter;
		released_co.try_erase(iter);
		co->ctx.freed_co_iter = {};
		stk_ptr = stk[stk_idx].get_stk_bp_ptr();
		goto exit;
	}
	if (stk_guard.owns_lock())
		stk_guard.unlock();

	if ((stk_idx_opt = freed_stack.try_pop_front()).has_value())
	{
		stk_idx = stk_idx_opt.value();
		stk_ptr = stk[stk_idx].get_stk_bp_ptr();
		if (co->ctx.stk_dyn_size > 0)
		{
			auto dyn_size = co->ctx.stk_dyn_size;
			std::memcpy(stk_ptr - dyn_size, co->ctx.stk_dyn_mem, dyn_size);
		}
	} else {
		while (true)
		{
			for (auto iter = released_co.begin(); iter.valid(); iter++)
			{
				auto iter_guard = iter.tryLock();
				if (!iter_guard.owns_lock())
					continue;

				uint16_t freed_co_idx = *iter;
				auto info = &stk[freed_co_idx];
				auto guard = info->tryLock();
				if (!guard.owns_lock() || !info->occupy_co->stk_active_lock.try_lock())
					continue;

				write_back(info);
				info->occupy_co->ctx.occupy_stack = -1;
				info->occupy_co->ctx.freed_co_iter = {};
				info->occupy_co->stk_active_lock.unlock();
				info->occupy_co = nullptr;

				/* 删除节点 */
				released_co.try_erase(iter);

				stk_idx = freed_co_idx;
				stk_ptr = stk[stk_idx].get_stk_bp_ptr();
				if (co->ctx.stk_dyn_size > 0)
				{
					auto dyn_size = co->ctx.stk_dyn_size;
					std::memcpy(stk_ptr - dyn_size, co->ctx.stk_dyn_mem, dyn_size);
				}

				break;
			}
			if (stk_idx != -1)
				break;
		}
	}

exit:
	stk[stk_idx].occupy_co = co;
	stk[stk_idx].stk_status = StackInfo::ACTIVE;
	co->ctx.occupy_stack = stk_idx;
	setup_co_static_stk(co, stk_ptr);
}

/* 由coroutine自己主动调用 */
/* coroutine stack 一定不在 released 队列 */
void StackPool::destroy_stack(Co_t * co)
{
	int stk_idx = co->ctx.occupy_stack;
	stk[stk_idx].stk_status = StackInfo::FREED;
	stk[stk_idx].occupy_co = nullptr;
	freed_stack.push_back(stk_idx);

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
	co->ctx.occupy_stack = -1;
	co->ctx.freed_co_iter = {};
}

/* 由coroutine自己主动调用 */
/* 前提，coroutine stack空闲 */
void StackPool::release_stack(Co_t * co)
{
	int stk_idx = co->ctx.occupy_stack;
	stk[stk_idx].stk_status = StackInfo::RELEASED;
	co->ctx.freed_co_iter = released_co.push_back(stk_idx);
}

void StackPool::setup_co_static_stk(Co_t* co, uint8_t * stk)
{
	co->ctx.stk_is_static = true;
	co->ctx.static_stk_pool = this;
	co->ctx.set_stack(stk);
}