//
// Created by hzj on 25-1-13.
//

#pragma once

#include "../allocator/include/StackPool.h"
#include "../allocator/include/MemoryPool.h"
#include "../allocator/include/DynStackPool.h"

#include "utils.h"
#include <memory_resource>

struct AllocatorGroup
{
#ifdef __MEM_PMR__
	std::pmr::synchronized_pool_resource co_pool{get_default_pmr_opt()};
	std::pmr::synchronized_pool_resource sem_pool{get_default_pmr_opt()};
#else
	MemoryPool co_pool{};
	MemoryPool sem_pool{};
#endif
	MemoryPool invoker_pool{};
	StackPool stk_pool{};
	DynStackPool dyn_stk_pool{};
};