#pragma once

#include <atomic>
#include <type_traits>

#include "utils.h"

template<typename T, typename Fn>
T atomic_fetch_modify(std::atomic<T> & atomic, Fn && fn, std::memory_order order = std::memory_order_seq_cst)
{
    static_assert(std::is_invocable_r_v<T, Fn, T>);

    T cur = atomic.load(std::memory_order_acquire);
    T & cref = cur;
    while (UNLIKELY(!atomic.compare_exchange_weak(cur, fn(cref), order)));
    return cur;
}