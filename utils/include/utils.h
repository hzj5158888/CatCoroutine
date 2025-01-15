//
// Created by hzj on 25-1-9.
//

#ifndef COROUTINE_UTILS_H
#define COROUTINE_UTILS_H

#include <iostream>
#include <cmath>
#include <cxxabi.h>

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

#endif //COROUTINE_UTILS_H
