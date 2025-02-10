//
// Created by hzj on 25-1-19.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/mman.h>
#include "MemoryPool.h"
#include "../../context/include/Context.h"
#include "../../include/Coroutine.h"
#ifdef __MEM_PMR__
#include <memory_resource>
#endif

class DynStackPool {
public:
    constexpr static std::size_t STACK_ALIGN = 16;
    constexpr static std::size_t STACK_RESERVE = 8 * STACK_ALIGN;
    constexpr static std::size_t STACK_SIZE = co::MAX_STACK_SIZE + STACK_RESERVE;
    constexpr static std::size_t POOL_BLOCK_COUNT = std::max((co::CPU_CORE * 8), 64);

#ifdef __MEM_PMR__
    std::pmr::synchronized_pool_resource dyn_stk_pool{
        std::pmr::pool_options{
            POOL_BLOCK_COUNT,
            STACK_SIZE
        }
    };
#elif __STACK_DYN_MMAP__
    MemoryPool dyn_stk_pool
    {
        POOL_BLOCK_COUNT * STACK_SIZE, 
        false,
        true,
        MAP_GROWSDOWN | MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK
    };
#else
    MemoryPool dyn_stk_pool{POOL_BLOCK_COUNT * STACK_SIZE, false};
#endif

    void alloc_stk(Context * ctx);
    void free_stk(Context * ctx);
};