#pragma once

#include <atomic>
#include <type_traits>

#include "utils.h"

template<typename T, typename Fn>
T atomic_fetch_modify(std::atomic<T> & atomic, Fn && fn, std::memory_order order)
{
    static_assert(std::is_invocable_r_v<T, Fn, T>);

    T cur = atomic.load(std::memory_order_acquire);
    T const & cref = cur;
    while (true) 
    {
        cur = atomic.load(std::memory_order_relaxed);
        if (LIKELY(atomic.compare_exchange_weak(cur, fn(cref), order)))
            break;
    }

    return cur;
}