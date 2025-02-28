#pragma once

#include <atomic>
#include <type_traits>

#include "utils.h"

namespace co {
    template<typename T, typename Fn>
    T atomic_fetch_modify(std::atomic<T> &atomic, Fn &&fn, std::memory_order order = std::memory_order_seq_cst) {
        static_assert(std::is_invocable_r_v<T, Fn, T>);

        T cur = atomic.load(std::memory_order_acquire);
        T &cref = cur;
        while (UNLIKELY(!atomic.compare_exchange_weak(cur, fn(cref), order, std::memory_order_relaxed)));
        return cur;
    }

    template<typename T, typename Fn>
    T atomic_modify_fetch(std::atomic<T> &atomic, Fn &&fn, std::memory_order order = std::memory_order_seq_cst) {
        static_assert(std::is_invocable_r_v<T, Fn, T>);

        T ans{};
        T cur = atomic.load(std::memory_order_acquire);
        T &cref = cur;
        while (UNLIKELY(!atomic.compare_exchange_weak(cur, (ans = fn(cref)), order, std::memory_order_relaxed)));
        return ans;
    }
}