//
// Created by hzj on 25-1-13.
//

#pragma once

#include "../allocator/include/StackPool.h"
#include "../allocator/include/MemoryPool.h"

struct AllocatorGroup
{
	MemoryPool co_pool{};
	MemoryPool sem_pool{};
	MemoryPool invoker_pool{};
	StackPool stk_pool{};
};