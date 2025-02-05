//
// Created by hzj on 25-1-9.
//

#pragma once

#include <iostream>
#include <execinfo.h>
#include <cmath>
#include <cxxabi.h>
#include <cassert>
#include <cstdint>
#include <memory_resource>
#include <type_traits>

#define LIKELY(x) (__builtin_expect((x), 1))
#define UNLIKELY(x) (__builtin_expect((x), 0))

#ifdef __DEBUG__
#define DASSERT(expr) (assert(UNLIKELY(expr)))
#else
#define DASSERT(expr)
#endif

template<typename AddressType, typename FuncPtrType>
AddressType get_member_func_addr(FuncPtrType func_ptr) // 不能在继承类使用
{
    union
    {
        FuncPtrType f;
        AddressType d;
    } u;
    u.f = func_ptr;
    return u.d;
}

template<typename T> constexpr bool is_tuple_v = false;
template<typename... T> constexpr bool is_tuple_v<std::tuple<T...>> = true;

constexpr std::size_t ceil_pow_2(std::size_t x)
{
	uint8_t idx{};
	while (x)
	{
		idx++;
		x >>= 1;
	}

	return std::size_t(1) << idx;
}

template<typename T>
constexpr T * align_stk_ptr(T * ptr)
{
	unsigned long mask = (unsigned long)(-1) - 15;
	return (T*)(((unsigned long)ptr & mask) + 8);
}

inline void print_trace()
{
	constexpr auto MAX_SIZE = 1024;

    int i, size;
    void *array[MAX_SIZE];
    size = backtrace(array, MAX_SIZE);
   	char **strings = backtrace_symbols(array, size);
    for (i = 0; i < size; i++)
        printf("%d# %s\n",i, strings[i]);
    free(strings);

	printf("\n\n");
}

#ifdef __MEM_PMR__
    inline std::pmr::pool_options get_default_pmr_opt()
    {
        return std::pmr::pool_options {
            48,
            1024 * 1024 * 4
        };
    }
#endif