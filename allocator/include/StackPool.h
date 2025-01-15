//
// Created by hzj on 25-1-14.
//

#pragma once

#include <cstdint>
#include <queue>
#include <memory>
#include <unordered_set>
#include <unordered_map>

#include "StackPoolDef.h"
#include "../../include/Coroutine.h"
#include "../../utils/include/spin_lock.h"
#include "../../include/CoDef.h"

struct StackInfo
{
	constexpr static std::size_t STACK_ALIGN = 64;
	constexpr static std::size_t STACK_RESERVE = 2 * STACK_ALIGN;
	constexpr static std::size_t STACK_SIZE = co::MAX_STACK_SIZE + STACK_RESERVE;

	uint8_t stk[STACK_SIZE];
	Co_t * co{};

	[[nodiscard]] uint8_t * get_stk_ptr() const
	{
		void * ptr = (void*)&stk[co::MAX_STACK_SIZE];
		std::size_t size_remain{STACK_RESERVE};
		return (uint8_t *)std::align(STACK_ALIGN, STACK_ALIGN, ptr, size_remain);
	}
};

struct StackPool
{
	StackInfo stk[co::STATIC_STK_NUM];
	MemoryPool dyn_stk_pool{};

	spin_lock m_lock{};
	std::queue<uint16_t> freed_stack{};
	std::unordered_map<Co_t*, int> freed_co{};
	std::unordered_map<Co_t*, int> running_co{};

	StackPool();
	void alloc_dyn_stk_mem(void * &mem_ptr, uint8_t * &stk_ptr, std::size_t size);
	void write_back(StackInfo * info);
	static void setup_co_static_stk(Co_t * co, bool stk_is_static, StackPool * static_pool);
	void free_stack(Co_t * co);
	void release_stack(Co_t * co);
	uint8_t * alloc_static_stk(Co_t * co);
	static void copy_stk(uint8_t * dest, uint8_t * src, std::size_t size);
};
