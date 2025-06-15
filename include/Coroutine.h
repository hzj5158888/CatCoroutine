#pragma once

#include <cstdint>
#include <cxxabi.h>
#include <exception>

#include "../utils/include/Invoker.h"
#include "../sched/include/CfsSched.h"

namespace co {
	constexpr static uint64_t MAX_STACK_SIZE = 1024 * 1024 * 2; // 1 MB
	constexpr static uint16_t CPU_CORE = __CPU_CORE__;
	constexpr static uint64_t STATIC_STACK_SIZE = 1024 * 1024 * 8; // 8MB
	constexpr static uint64_t STATIC_STK_NUM = 256;
	static_assert(CPU_CORE > 0);

	class CoUnInitializationException : public std::exception
	{
	public:
		[[nodiscard]] const char * what() const noexcept override { return "Co UnInitialization"; }
	};

	class CoInitializationException : public std::exception
	{
	public:
		[[nodiscard]] const char * what() const noexcept override { return "Co Initialization Failed"; }
	};

	class CoCreateException : public std::exception
	{
	public:
		[[nodiscard]] const char * what() const noexcept override { return "Co Create Failed"; }
	};

	class DestroyBeforeCloseException : public std::exception
	{
	public:
		[[nodiscard]] const char * what() const noexcept override { return "Co Destroy Before Close"; }
	};

	std::pair<void *, void * (*)(void*, std::size_t)> get_invoker_alloc();
	void * create(void * invoker, int nice);
    void destroy(void * handle);
	void await_impl(void * handle);
    void yield();
    void sleep(std::chrono::microseconds duration);
    void sleep_until(std::chrono::microseconds end_time);
	void init();

    template<class Fn, class ... Args>
    void * construct(int nice, bool is_await, void * buf, Fn && fn, Args &&... args)
    {
        static_assert(std::is_invocable_v<Fn, Args...>);
        using Ret = std::invoke_result_t<Fn, Args...>;
        assert(cfs_nice_in_range(nice));

        using Invoker = Invoker<Fn, Args...>;
        void * handle{};
        if (!is_await)
        {
            auto [alloc_self, alloc_func] = get_invoker_alloc();
            auto invoker_memory = alloc_func(alloc_self, sizeof(Invoker));
            auto invoker = new (invoker_memory) Invoker(std::forward<Fn>(fn), std::forward<Args>(args)...);
            invoker->allocator = alloc_self;
            if constexpr (!std::is_same_v<void, Ret>)
                invoker->buf = buf;

            handle = create(invoker, nice);
            if (handle == nullptr)
                throw CoCreateException();
        } else {
            Invoker invoker{std::forward<Fn>(fn), std::forward<Args>(args)...};
            if constexpr (!std::is_same_v<void, Ret>)
                invoker.buf = buf;

            handle = create(std::addressof(invoker), nice);
            if (UNLIKELY(handle == nullptr))
                throw CoCreateException();

            await_impl(handle);
        }

        return handle;
    }

    template<typename Ret>
    class Co
    {
    private:
		void * handle{};
        uint8_t buf[sizeof(Ret)];
    public:
		Co() = default;
        Co(const Co & oth) = delete;

		template<typename Fn, typename ... Args>
        Co(int nice, Fn && fn, Args &&... args)
		{
            static_assert(std::is_invocable_v<Fn, Args...>);
            static_assert(std::is_same_v<Ret, std::invoke_result_t<Fn, Args...>>);
            handle = construct(nice, false, reinterpret_cast<void*>(buf), std::forward<Fn>(fn), std::forward<Args>(args)...);
		}

        template<typename Fn, typename ... Args>
        explicit Co(Fn && fn, Args &&... args)
        {
            static_assert(std::is_invocable_v<Fn, Args...>);
            static_assert(std::is_same_v<Ret, std::invoke_result_t<Fn, Args...>>);
            handle = construct(PRIORITY_NORMAL, false, reinterpret_cast<void*>(buf), std::forward<Fn>(fn), std::forward<Args>(args)...);
        }

        Co(Co && co) noexcept { swap(std::move(co)); }
        
        ~Co()
		{
			if (handle != nullptr)
				co::destroy(handle);
		}
    
        Ret await()
        {
            await_impl(handle);

            auto res = std::move(*reinterpret_cast<Ret*>(buf));
            if constexpr (!std::is_trivially_destructible_v<Ret>)
                reinterpret_cast<Ret*>(buf)->~Ret();

            return res;
        }

		void swap(Co && co) { std::swap(handle, co.handle); }
    };

    template<>
    class Co<void>
    {
    private:
        void * handle{};
    public:
        Co() = default;
        Co(const Co & oth) = delete;

        template<typename Fn, typename ... Args>
        Co(int nice, Fn && fn, Args &&... args)
        {
            handle = construct(nice, false, nullptr, std::forward<Fn>(fn), std::forward<Args>(args)...);
        }

        template<typename Fn, typename ... Args>
        explicit Co(Fn && fn, Args &&... args)
        {
            handle = construct(PRIORITY_NORMAL, false, nullptr, std::forward<Fn>(fn), std::forward<Args>(args)...);
        }

        Co(Co && co) noexcept { swap(std::move(co)); }

        ~Co()
        {
            if (handle != nullptr)
                co::destroy(handle);
        }

        void await()
        {
            await_impl(handle);
        }

        void swap(Co && co) { std::swap(handle, co.handle); }
    };

    template<typename Fn, typename ... Args, typename Ret = std::invoke_result_t<Fn, Args...>>
    Ret await(Fn && fn, Args &&... args)
    {
        static_assert(std::is_invocable_v<Fn, Args...>);

        constexpr int nice = PRIORITY_NORMAL;
        if constexpr (std::is_same_v<void, Ret>)
        {
            auto handle = construct(
                    nice,
                    true,
                    nullptr,
                    std::forward<Fn>(fn),
                    std::forward<Args>(args)...
            );
            co::destroy(handle);
            return;
        }

        uint8_t buf[sizeof(Ret)];
        auto handle = construct(
                nice,
                true,
                reinterpret_cast<Ret*>(buf),
                std::forward<Fn>(fn),
                std::forward<Args>(args)...
        );
        co::destroy(handle);

        auto res = std::move(*reinterpret_cast<Ret*>(buf));
        if constexpr (!std::is_trivially_destructible_v<Ret>)
            reinterpret_cast<Ret*>(buf)->~Ret();

        return res;
    }

    template<typename Fn, typename ... Args, typename Ret = std::invoke_result_t<Fn, Args...>>
    Ret await(int nice, Fn && fn, Args &&... args)
    {
        static_assert(std::is_invocable_v<Fn, Args...>);
        if constexpr (std::is_same_v<void, Ret>)
        {
            auto handle = construct(
                    nice,
                    true,
                    nullptr,
                    std::forward<Fn>(fn),
                    std::forward<Args>(args)...
            );
            co::destroy(handle);
            return;
        }

        uint8_t buf[sizeof(Ret)];
        auto handle = construct(
                nice,
                true,
                reinterpret_cast<Ret*>(buf),
                std::forward<Fn>(fn),
                std::forward<Args>(args)...
        );
        co::destroy(handle);

        auto res = std::move(*reinterpret_cast<Ret*>(buf));
        if constexpr (!std::is_trivially_destructible_v<Ret>)
            reinterpret_cast<Ret*>(buf)->~Ret();

        return res;
    }
}