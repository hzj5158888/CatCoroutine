#pragma once

#include <functional>
#include <tuple>
#include <type_traits>

#include "utils.h"

namespace co {
    template<typename ... Ts>
    using callable_tuple = std::tuple<typename std::decay<Ts>::type...>;

    struct InvokerBase
    {
		void * allocator{};

        virtual ~InvokerBase() = default;
        virtual void operator() () = 0;
    };

    template<typename Fn, typename ... Args>
    struct Invoker : public InvokerBase
    {
        using Tuple = callable_tuple<Fn, Args...>;
        Tuple func_args_tuple;

        Invoker() = delete;

        explicit Invoker(Fn && fn, Args &&... args) : func_args_tuple(std::forward<Fn>(fn), std::forward<Args>(args)...) {}

        Invoker(Invoker && invoker) noexcept { std::swap(func_args_tuple, invoker.func_args_tuple); }

        template<std::size_t... Idx>
        std::invoke_result_t<Fn, Args...> invoke(std::index_sequence<Idx...>)
        {
            return std::invoke(std::get<Idx>(std::move(func_args_tuple))...);
        }

        void operator() () override
        {
            invoke(std::make_index_sequence<std::tuple_size<Tuple>::value>());
        }
    };
}