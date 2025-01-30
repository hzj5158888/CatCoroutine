//
// Created by hzj on 25-1-19.
//

#pragma once

#include "MemoryPool.h"
#include "../../context/include/Context.h"
#include "../../include/Coroutine.h"
#include <memory_resource>

class DynStackPool {
public:
    constexpr static std::size_t STACK_ALIGN = 16;
    constexpr static std::size_t STACK_RESERVE = 8 * STACK_ALIGN;
    constexpr static std::size_t STACK_SIZE = co::MAX_STACK_SIZE + STACK_RESERVE;

#ifdef __MEM_PMR__
    std::pmr::synchronized_pool_resource dyn_stk_pool{
        std::pmr::pool_options{
            std::max((co::CPU_CORE * 8), 64),
            STACK_SIZE
        }
    };
#else
    MemoryPool dyn_stk_pool{std::max((co::CPU_CORE * 8), 64) * STACK_SIZE, false};
#endif

    void alloc_stk(Context * ctx);
    void free_stk(Context * ctx);
};