#pragma once

#include <cstdint>
#include <cxxabi.h>
#include <exception>

#include "../utils/include/Invoker.h"
#include "../sched/include/CfsSched.h"

namespace co {
	constexpr static uint64_t INVALID_CO_ID = 0;
	constexpr static uint64_t MAIN_CO_ID = 1;
	constexpr static uint64_t MAX_STACK_SIZE = 1024 * 1024 * 2; // 1 MB
	constexpr static uint16_t CPU_CORE = __CPU_CORE__;
	constexpr static uint64_t STATIC_STACK_SIZE = 1024 * 1024 * 8; // 8MB
	constexpr static uint64_t STATIC_STK_NUM = 256;
	static_assert(CPU_CORE > 0);

	class Co_UnInitialization_Exception : public std::exception
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
	void await(void * handle);
    void yield();
	void init();

	template<int NICE = PRIORITY_NORMAL>
    class Co
    {
    private:
		void * handle{};
		static_assert(NICE >= -20 && NICE <= 19);
    public:

		Co() = default;
        Co(const Co & oth) = delete;
        template<typename Fn, typename... Args> explicit
        Co(Fn && fn, Args &&... args);
        Co(Co && co) noexcept;
        
        ~Co();
    
        void await();

		void swap(Co && co);
    };

	template<int NICE>
    template<typename Fn, typename... Args>
    Co<NICE>::Co(Fn && fn, Args &&... args)
    {
        using Invoker = Invoker<Fn, Args...>;
		auto [alloc_self, alloc_func] = get_invoker_alloc();
		auto invoker_memory = alloc_func(alloc_self, sizeof(Invoker));
		auto invoker = new (invoker_memory) Invoker(std::forward<Fn>(fn), std::forward<Args>(args)...);
		invoker->allocator = alloc_self;
		this->handle = create(invoker, NICE);
        if (this->handle == nullptr)
            throw CoCreateException();
    }

	template<int NICE>
	Co<NICE>::~Co()
	{
		if (handle != nullptr)
			co::destroy(handle);
	}

;	template<int NICE>
	void Co<NICE>::await() { co::await(handle); }

	template<int NICE>
	void Co<NICE>::swap(Co<NICE> && co) { std::swap(handle, co.handle); }

	template<int NICE>
	Co<NICE>::Co(Co<NICE> && co) noexcept { swap(std::move(co)); }
}