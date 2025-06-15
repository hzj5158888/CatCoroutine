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
        using Ret = std::invoke_result_t<Fn, Args...>;
        Tuple func_args_tuple;
        void * buf{};

        Invoker() = default;
        explicit Invoker(Fn && fn, Args &&... args) : func_args_tuple(std::forward<Fn>(fn), std::forward<Args>(args)...) {}
        Invoker(Invoker && oth) noexcept { swap(oth); }

        template<std::size_t... Idx>
        Ret invoke(std::index_sequence<Idx...>)
        {
            return std::invoke(std::get<Idx>(std::move(func_args_tuple))...);
        }

        void operator() () override
        {
            if constexpr (!std::is_same_v<void, Ret>)
                new (buf) Ret{std::forward<Ret>(invoke(std::make_index_sequence<std::tuple_size<Tuple>::value>()))};
            else
                invoke(std::make_index_sequence<std::tuple_size<Tuple>::value>());
        }
    };
}