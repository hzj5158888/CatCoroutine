//
// Created by hzj on 25-1-9.
//

#pragma once

#include <bits/types/struct_timeval.h>
#include <cstddef>
#include <cstdio>
#include <execinfo.h>
#include <cmath>
#include <cxxabi.h>
#include <cassert>
#include <cstdint>
#include <type_traits>
#include <sys/time.h>
#include <memory_resource>

#define LIKELY(x) (__builtin_expect((x), 1))
#define UNLIKELY(x) (__builtin_expect((x), 0))

#ifdef __DEBUG__
#define DASSERT(expr) (assert(UNLIKELY(expr)))
#define DEXPR(expr) expr
#else
#define DASSERT(expr)
#define DEXPR(expr)
#endif

namespace co {
    template<typename T>
    constexpr bool has_virtual_function() {
        struct tmp : T {
        };
        return sizeof(tmp) != sizeof(T);
    }

    template<typename T, typename AddressType, typename FuncPtrType>
    AddressType get_member_func_addr(FuncPtrType func_ptr) // 不能在继承类使用
    {
        static_assert(!has_virtual_function<T>());

        union {
            FuncPtrType f;
            AddressType d;
        } u;
        u.f = func_ptr;
        return u.d;
    }

    template<typename T> constexpr bool is_tuple_v = false;
    template<typename... T> constexpr bool is_tuple_v<std::tuple<T...>> = true;

    template<typename T>
    struct is_lambda :
            std::integral_constant<bool, std::is_class_v<T> && !std::is_same_v<T, std::decay_t<T>>> {
    };

    template<typename T>
    constexpr bool is_lambda_v = is_lambda<T>::value;

    template<typename T>
    constexpr int countl_zero(T x) {
        if constexpr (sizeof(T) < sizeof(unsigned))
            return x != 0 ? __builtin_clz(x) : sizeof(T) * 8; // fixup, __builtin_clz(0) == sizeof(T) * 8 - 1
        else if constexpr (sizeof(T) == sizeof(unsigned))
            return x != 0 ? __builtin_clzl(x) : sizeof(T) * 8; // fixup, __builtin_clzl(0) == sizeof(T) * 8 - 1
        else if constexpr (sizeof(T) == sizeof(unsigned long long))
            return x != 0 ? __builtin_clzll(x) : sizeof(T) * 8; // fixup, __builtin_clzll(0) == sizeof(T) * 8 - 1
        else
            static_assert(false);
    }

    template<typename T>
    constexpr int countl_one(T x) {
        return countl_zero(~x);
    }

    constexpr std::size_t ceil_pow_2(std::size_t x) {
        uint8_t idx{};
        while (x) {
            idx++;
            x >>= 1;
        }

        return std::size_t(1) << idx;
    }

    template<typename T>
    constexpr T align_stk_ptr(T ptr) {
        size_t mask = (size_t) (-1) - 15;
        return (T) (((size_t) ptr & mask) - 8);
    }

    inline void print_trace() {
        constexpr auto MAX_SIZE = 1024;

        int i, size;
        void *array[MAX_SIZE];
        size = backtrace(array, MAX_SIZE);
        char **strings = backtrace_symbols(array, size);
        for (i = 0; i < size; i++)
            printf("%d# %s\n", i, strings[i]);
        free(strings);

        printf("\n\n");
    }

    inline constexpr std::pmr::pool_options get_default_pmr_opt() {
        return std::pmr::pool_options{
                48,
                1024 * 1024 * 4
        };
    }

    template<typename T>
    inline constexpr bool is_pow_of_2(T x) {
        static_assert(std::is_unsigned_v<T>);
        return (x & (x - 1)) == 0;
    }

    inline uint32_t xor_shift_32(uint32_t state) {
        /* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    inline void spin_wait(int32_t total_spin) {
        while (total_spin--) { __builtin_ia32_pause(); }
    }

    template<typename Fn>
    inline int32_t spin_wait(int32_t total_spin, Fn &&fn) {
        if constexpr (std::is_invocable_r_v<bool, Fn>) {
            for (int32_t i = 0; i < total_spin; i++) {
                if (fn())
                    return i;
            }
        } else if constexpr (std::is_invocable_r_v<bool, Fn, int32_t>) {
            for (int32_t i = 0; i < total_spin; i++) {
                if (fn(i))
                    return i;
            }
        } else {
            static_assert(false);
        }

        return -1;
    }
}