//
// Created by hzj on 25-1-14.
//

#pragma once

#include <cstdint>
#include <memory_resource>
#include <queue>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "MemoryPool.h"
#include "StackPoolDef.h"
#include "../../include/Coroutine.h"
#include "../../utils/include/spin_lock.h"
#include "../../include/CoDef.h"
#include "../data_structure/include/ListLockFree.h"
#include "utils.h"

struct StackInfo
{
	constexpr static std::size_t STACK_ALIGN = 16;
	constexpr static std::size_t STACK_RESERVE = 4 * STACK_ALIGN;
	constexpr static std::size_t STACK_SIZE = co::STATIC_STACK_SIZE + STACK_RESERVE;

	enum STACK_STATUS
	{
		FREED = 1,
		RELEASED,
		ACTIVE
	};

	uint8_t * stk{};
	Co_t * occupy_co{};
	uint8_t stk_status{FREED};
	spin_lock m_lock;

	StackInfo() { stk = static_cast<uint8_t *>(std::malloc(STACK_SIZE)); }
	~StackInfo() { std::free(stk); }

	std::unique_lock<spin_lock> tryLock() { return std::unique_lock(m_lock, std::try_to_lock); }

	[[nodiscard]] uint8_t * get_stk_bp_ptr() const
	{
		auto * ptr = &stk[co::MAX_STACK_SIZE];
		return align_stk_ptr(ptr);
	}
};

struct StackPool
{
	std::array<StackInfo, co::STATIC_STK_NUM> stk{};

#ifdef __MEM_PMR__
	std::pmr::synchronized_pool_resource dyn_stk_saver_pool{get_default_pmr_opt()};
#else
	MemoryPool dyn_stk_saver_pool{co::MAX_STACK_SIZE * 2, false};
#endif

	spin_lock m_lock{};
	ListLockFree<uint16_t> freed_stack{};
	ListLockFree<uint16_t> released_co{};

	StackPool();
	void alloc_dyn_stk_mem(void * &mem_ptr, std::size_t size);
	void write_back(StackInfo * info);
	void setup_co_static_stk(Co_t* co, uint8_t * stk);
	void destroy_stack(Co_t * co);
	void release_stack(Co_t * co);
	void alloc_static_stk(Co_t * co);
};