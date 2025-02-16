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

template<typename T>
struct is_lambda :
	std::integral_constant<bool, std::is_class_v<T> && !std::is_same_v<T, std::decay_t<T>>> {};

template<typename T>
constexpr bool is_lambda_v = is_lambda<T>::value;

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

template<typename T>
constexpr int countl_zero(T x)
{
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
constexpr int countl_one(T x)
{
	return countl_zero(~x);
}

inline constexpr std::pmr::pool_options get_default_pmr_opt()
{
    return std::pmr::pool_options {
        48,
        1024 * 1024 * 4
    };
}

inline void spin_wait(uint32_t spin_count)
{
    while (spin_count--) { __builtin_ia32_pause(); }
}

template<typename Fn>
inline bool spin_wait(int32_t spin_count, Fn && fn)
{
    static_assert(std::is_invocable_r_v<bool, Fn>);

    bool ans{false};
    while (spin_count-- > 0 && !(ans = fn())) { __builtin_ia32_pause(); }
    return ans;
}