//
// Created by hzj on 25-1-19.
//

#pragma once

#include "MemoryPool.h"
#include "../../context/include/Context.h"
#include "../../include/Coroutine.h"

class DynStackPool {
public:
    constexpr static std::size_t STACK_ALIGN = 16;
    constexpr static std::size_t STACK_RESERVE = 8 * STACK_ALIGN;
    constexpr static std::size_t STACK_SIZE = co::MAX_STACK_SIZE + STACK_RESERVE;

    MemoryPool dyn_stk_pool{std::max((co::CPU_CORE + 1), 8) * co::MAX_STACK_SIZE, false};

    void alloc_stk(Context * ctx);
    void free_stk(Context * ctx);
};