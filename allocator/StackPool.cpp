//
// Created by hzj on 25-1-14.
//

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <cstring>
#include <cassert>

#include "../utils/include/spin_lock.h"
#include "../include/CoPrivate.h"
#include "include/StackPool.h"
#include "include/MemoryPool.h"

StackPool::StackPool()
{
	for (size_t i = 0; i < co::STATIC_STK_NUM; i++)
		freed_stack.push_back(i);
}

void StackPool::alloc_dyn_stk_mem(void * &mem_ptr, std::size_t size)
{
	mem_ptr = dyn_stk_pool.allocate_safe(size + StackInfo::STACK_RESERVE);
}

void StackPool::write_back(StackInfo * info)
{
	auto co = info->occupy_co;
	if (co->ctx.stk_dyn_mem == nullptr)
	{
		co->ctx.stk_dyn_saver_alloc = &dyn_stk_pool;
		co->ctx.stk_dyn_capacity = 4 * co->ctx.stk_size / 3; // 1.25 * stk_size
		alloc_dyn_stk_mem(co->ctx.stk_dyn_mem, co->ctx.stk_size);
	} else if (co->ctx.stk_dyn_capacity <= co->ctx.stk_size)
	{
		co->ctx.stk_dyn_saver_alloc->free_safe(co->ctx.stk_dyn_mem);
		co->ctx.stk_dyn_saver_alloc = &dyn_stk_pool;
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

	int stk_idx{};
	uint8_t * stk_ptr{};
	auto iter = freed_co.find(co);
	if (iter != freed_co.end())
	{
		stk_idx = iter->second;
		freed_co.erase(co);
		stk_ptr = stk[stk_idx].get_stk_bp_ptr();
	} else if (!freed_stack.empty())
	{
		stk_idx = freed_stack.back();
		freed_stack.pop_back();

		stk_ptr = stk[stk_idx].get_stk_bp_ptr();
		if (co->ctx.stk_dyn_size > 0)
		{
			auto dyn_size = co->ctx.stk_dyn_size;
			std::memcpy(stk_ptr - dyn_size, co->ctx.stk_dyn_mem, dyn_size);
		}
	} else {
		DASSERT(!freed_co.empty());

		auto free_co_idx = freed_co.begin()->second;
		freed_co.erase(freed_co.begin());

		write_back(&stk[free_co_idx]);

		stk_idx = free_co_idx;
		stk_ptr = stk[stk_idx].get_stk_bp_ptr();
		if (co->ctx.stk_dyn_size > 0)
		{
			auto dyn_size = co->ctx.stk_dyn_size;
			std::memcpy(stk_ptr - dyn_size, co->ctx.stk_dyn_mem, dyn_size);
		}
	}

	stk[stk_idx].occupy_co = co;
	running_co.insert({co, stk_idx});
	setup_co_static_stk(co, stk_ptr);
}

void StackPool::destroy_stack(Co_t * co)
{
	m_lock.lock();

	int stk_idx = -1;
	auto iter = running_co.find(co);
	if (iter != running_co.end())
	{
		stk_idx = iter->second;
		running_co.erase(iter);
	}

	iter = freed_co.find(co);
	if (iter != freed_co.end())
	{
		stk_idx = iter->second;
		freed_co.erase(iter);
	}

	if (stk_idx >= 0)
		freed_stack.push_back(stk_idx);

	m_lock.unlock();

	if (co->ctx.stk_dyn_mem != nullptr)
		co->ctx.stk_dyn_saver_alloc->free_safe(co->ctx.stk_dyn_mem);

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
}

void StackPool::release_stack(Co_t * co)
{
	std::lock_guard lock(m_lock);

	auto iter = running_co.find(co);
	DASSERT(iter != running_co.end());
	int stk_idx = iter->second;
	running_co.erase(iter);
	freed_co.insert({co, stk_idx});
}

void StackPool::setup_co_static_stk(Co_t* co, uint8_t * stk)
{
	co->ctx.stk_is_static = true;
	co->ctx.static_stk_pool = this;
	co->ctx.set_stack(stk);
}