//
// Created by hzj on 25-1-14.
//

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
	for (int i = 0; i < co::STATIC_STK_NUM; i++)
		freed_stack.push(i);
}

void StackPool::alloc_dyn_stk_mem(void * &mem_ptr, uint8_t * &stk_ptr, std::size_t size)
{
	mem_ptr = reinterpret_cast<void*>(dyn_stk_pool.allocate_safe(size + StackInfo::STACK_RESERVE));

	std::size_t tmp{StackInfo::STACK_RESERVE};
	void * stk_ptr_void = reinterpret_cast<void *>((unsigned long)mem_ptr + size);
	stk_ptr_void = std::align(StackInfo::STACK_ALIGN, StackInfo::STACK_ALIGN, stk_ptr_void, tmp);
	stk_ptr = static_cast<uint8_t *>(stk_ptr_void);
}

void StackPool::write_back(StackInfo * info)
{
	if (info->co == nullptr) [[unlikely]]
		return;

	[[unlikely]] assert(info->co->status != CO_RUNNING);

	auto ctx = &info->co->ctx;
	ctx->stk_is_static = false;
	ctx->static_stk_pool = nullptr;
	if (ctx->stk_dyn == nullptr)
	{
		std::size_t stk_size = ctx->stk_size();
		alloc_dyn_stk_mem(ctx->stk_dyn_mem, ctx->stk_dyn, stk_size);
		ctx->stk_dyn_capacity = stk_size;
		ctx->stk_dyn_alloc = &dyn_stk_pool;
		copy_stk(ctx->stk_dyn, info->get_stk_ptr(), stk_size);
	} else if (ctx->stk_dyn_capacity >= ctx->stk_size())
	{
		copy_stk(ctx->stk_dyn, info->get_stk_ptr(), ctx->stk_size());
	} else {
		ctx->stk_dyn_alloc->free_safe(ctx->stk_dyn_mem);
		ctx->stk_dyn_mem = nullptr;
		ctx->stk_dyn = nullptr;

		ctx->stk_dyn_alloc = &dyn_stk_pool;
		ctx->stk_dyn_capacity = 3 * ctx->stk_size() / 2; // 1.5 * ctx->stk_size()
		alloc_dyn_stk_mem(ctx->stk_dyn_mem, ctx->stk_dyn, ctx->stk_dyn_capacity);
		copy_stk(ctx->stk_dyn, info->get_stk_ptr(), ctx->stk_size());
	}
}

void StackPool::setup_co_static_stk(Co_t * co, bool stk_is_static, StackPool * static_pool)
{
	co->ctx.stk_is_static = stk_is_static;
	co->ctx.static_stk_pool = static_pool;
}

uint8_t * StackPool::alloc_static_stk(Co_t * co)
{
	[[unlikely]] assert(co->status != CO_RUNNING);

	std::lock_guard<spin_lock> lock(m_lock);

	auto iter = freed_co.find(co);
	if (iter != freed_co.end()) // Coroutine using static stack
	{
		int stk_idx = iter->second;
		freed_co.erase(iter);
		running_co.insert({co, stk_idx});
		return stk[stk_idx].get_stk_ptr();
	}

	if (!freed_stack.empty())
	{
		int stk_idx = freed_stack.front();
		freed_stack.pop();

		stk[stk_idx].co = co;
		running_co.insert({co, stk_idx});
		if (co->ctx.stk_size() > 0 && co->ctx.stk_dyn != nullptr)
			copy_stk(stk[stk_idx].get_stk_ptr(), co->ctx.stk_dyn, co->ctx.stk_size());

		setup_co_static_stk(co, true, this);
		return stk[stk_idx].get_stk_ptr();
	}

	if (freed_co.empty()) [[unlikely]]
		return nullptr;

	int stk_idx = freed_co.begin()->second;
	auto co_stack_info = &stk[stk_idx];
	// write back to old stack
	write_back(co_stack_info);
	freed_co.erase(co_stack_info->co);
	/* switch to new stack */
	co_stack_info->co = co;
	running_co.insert({co, stk_idx});
	// copy stack data
	if (co->ctx.stk_size() > 0)
		copy_stk(co_stack_info->get_stk_ptr(), co->ctx.stk_dyn, co->ctx.stk_size());

	setup_co_static_stk(co, true, this);
	return co_stack_info->get_stk_ptr();
}

void StackPool::free_stack(Co_t * co)
{
	[[unlikely]] assert(co->status != CO_RUNNING);

	std::lock_guard<spin_lock> lock(m_lock);

	[[unlikely]] assert(running_co.find(co) == running_co.end());

	auto iter = freed_co.find(co);
	[[unlikely]] assert(iter != freed_co.end());
	int stk_idx = iter->second;
	stk[stk_idx].co = nullptr;
	freed_stack.push(stk_idx);

	co->ctx.stk_is_static = false;
	co->ctx.stk_dyn = nullptr;
	if (co->ctx.stk_dyn_mem != nullptr) [[likely]]
		co->ctx.stk_dyn_alloc->free_safe(co->ctx.stk_dyn_mem);
	co->ctx.stk_dyn_mem = nullptr;
	co->ctx.stk_dyn_alloc = nullptr;
	co->ctx.stk_dyn_capacity = {};
}

void StackPool::release_stack(Co_t *co)
{
	[[unlikely]] assert(co->status != CO_RUNNING);

	std::lock_guard<spin_lock> lock(m_lock);

	auto iter = running_co.find(co);
	[[unlikely]] assert(iter != running_co.end());
	running_co.erase(iter);
	int stk_idx = iter->second;
	freed_co.insert({co, stk_idx});
}

void StackPool::copy_stk(uint8_t *dest, uint8_t *src, std::size_t size)
{
	std::memcpy(dest - size, src - size, size);
}
