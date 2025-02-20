#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <random>
#include <thread>
#include <cstring>
#include <memory_resource>

#include "context/include/Context.h"
#include "include/Coroutine.h"
#include "include/CoPrivate.h"
#include "include/CoCtx.h"
#include "sched/include/Scheduler.h"
#include "allocator/include/MemoryPool.h"

#include "utils/include/co_utils.h"
#include "utils/include/utils.h"
#include "utils/include/tscns.h"
#include "xor_shift_rand.h"

namespace co_ctx {
	bool is_init{false};
	std::shared_ptr<SchedManager> manager{};
	std::atomic<uint32_t> coroutine_count{co::MAIN_CO_ID};
#ifdef __DEBUG_SCHED__
    std::unordered_set<Co_t*> co_vec{};
    std::unordered_multiset<Co_t*> running_co{};
    spin_lock m_lock{};
#endif
	thread_local std::shared_ptr<local_t> loc{};
}

namespace co {
	void wrap(void (*func)(void*), void * arg)
	{
		func(arg);
		co_ctx::loc->scheduler->jump_to_sched(CALL_DEAD);
	}

	static void invoker_wrapper(void * self)
	{
		auto invoker = static_cast<InvokerBase*>(self);
		invoker->operator()();
		if (UNLIKELY(invoker->allocator == nullptr))
		{
			delete invoker;
		} else {
			auto alloc = static_cast<MemoryPool*>(invoker->allocator);
			invoker->~InvokerBase();
			alloc->deallocate(invoker);
		}
	}

	std::pair<void *, void * (*)(void*, std::size_t)> get_invoker_alloc()
	{
		if (UNLIKELY(co_ctx::loc == nullptr))
			throw co::CoInitializationException();

		return {
			&co_ctx::loc->alloc.invoker_pool,
			get_member_func_addr<void * (*)(void*, std::size_t)>(&MemoryPool::allocate)
		};
	}

	void init_other(int thread_idx)
	{
		/* init other thread */
		co_ctx::loc = std::make_shared<local_t>();
		co_ctx::loc->thread_id = std::this_thread::get_id();
		co_ctx::loc->scheduler = std::make_shared<Scheduler>();
		co_ctx::loc->scheduler->this_thread_id = thread_idx;
#ifdef __SCHED_CFS__
		/* init system clock calibrate thread */
		auto clock_calibrate_fn = [](TSCNS * clock)
		{
			while (true)
			{
				clock->calibrate();
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
		};
		co_ctx::loc->clock.init();
		co_ctx::loc->clock.calibrate();
		std::thread{clock_calibrate_fn, std::addressof(co_ctx::loc->clock)}.detach();
#endif
		/* init rand */
		co_ctx::loc->rand = xor_shift_32_rand(co_ctx::loc->clock.rdns());
		/* init scheduler */
		co_ctx::manager->push_scheduler(co_ctx::loc->scheduler, thread_idx);
		/* create scheduler loop context */
		auto ctx = &co_ctx::loc->scheduler->sched_ctx;
		auto start_ptr = get_member_func_addr<void(*)(void*)>(&Scheduler::start);
        co_ctx::loc->alloc.dyn_stk_pool.alloc_stk(ctx);
        ctx->arg_reg.di = reinterpret_cast<uint64_t>(co_ctx::loc->scheduler.get());
		make_context(ctx, start_ptr);
		if (thread_idx > 0)
		{
			co_ctx::manager->init_lock.wait_until_lockable();
			co_ctx::loc->scheduler->jump_to_sched();
		}
	}

	void init()
	{
		/* currency is main thread */
		/* init scheduler manager */
		co_ctx::manager = std::make_shared<SchedManager>(CPU_CORE);
		co_ctx::manager->init_lock.lock();
		/* init thread local storage */
		init_other(0);

		/* init other thread */
		for (int i = 0; i < CPU_CORE - 1; i++)
			std::thread{init_other, i + 1}.detach();

		/* wait for init finish */
		co_ctx::manager->init_lock.wait_until_lockable();

		/* init finished */
		co_ctx::is_init = true;

		/* create main coroutine */
		Co_t * co = new Co_t{};
		/* co->id == 1 */
		co->id = co_ctx::coroutine_count++;
        DEXPR(std::cout << "main coroutine, addr: " << co << std::endl;);
#ifdef __SCHED_CFS__
		co->sched.nice = PRIORITY_NORMAL;
#endif
		/* doesn't need to alloc stack */
		/* main coroutine doesn't need to up stack size */

        /* set scheduler occupy_thread */
        /* occupy thread of main coroutine must be this thread */
        co->sched.occupy_thread = 0;
		/* apply main coroutine */
		co_ctx::manager->apply(co);
		/* goto scheduler */
        auto scheduler = co_ctx::loc->scheduler.get();
        swap_context(std::addressof(co->ctx), std::addressof(scheduler->sched_ctx));
	}

    void destroy(void * handle)
	{
		auto co = static_cast<Co_t*>(handle);
		/* 等待状态更新 */
		co->status_lock.wait_until_lockable();
		if (UNLIKELY(co->status != CO_DEAD))
			throw DestroyBeforeCloseException();

		/* do not delete Main co */
		if (LIKELY(co->id != MAIN_CO_ID))
		{
			auto alloc = co->allocator;
			co->~Co_t();
			if (LIKELY(alloc != nullptr))
#ifdef __MEM_PMR__
				alloc->deallocate(co, sizeof(Co_t));
#else
				alloc->deallocate(co);
#endif
			else
				std::free(co);
		}
	}

    void * create(void (*func)(void *), void * arg, int nice)
    {
		if (UNLIKELY(!co_ctx::is_init))
			return nullptr;

		Co_t * co = static_cast<Co_t*>(co_ctx::loc->alloc.co_pool.allocate(sizeof(Co_t)));
		if (UNLIKELY(co == nullptr))
			return nullptr;

		co = new (co) Co_t{}; // construct
		co->id = co_ctx::coroutine_count++;
		co->allocator = &co_ctx::loc->alloc.co_pool;
#ifdef __SCHED_CFS__
		co->sched.nice = nice;
#endif
        co->ctx.arg_reg.di = reinterpret_cast<uint64_t>(func);
        co->ctx.arg_reg.si = reinterpret_cast<uint64_t>(arg);
		co_ctx::manager->apply(co);
        return co;
    }

	void * create(void * invoker_self, int nice)
	{
		return create(&invoker_wrapper, invoker_self, nice);
	}

	void await(void * handle)
	{
		if (UNLIKELY(handle == nullptr))
			throw Co_UnInitialization_Exception();

		/* 获取 callee coroutine 状态锁 */
		auto callee = static_cast<Co_t*>(handle);
		callee->status_lock.lock();
		/* callee Co 已经消亡 */
		if (callee->status == CO_DEAD)
		{
			callee->status_lock.unlock();
			return;
		}

		co_ctx::loc->scheduler->await_callee = callee;
		co_ctx::loc->scheduler->jump_to_sched(CALL_AWAIT);
	}

    void yield()
	{
		DASSERT(co_ctx::loc->scheduler->running_co != nullptr);
		co_ctx::loc->scheduler->jump_to_sched(CALL_YIELD);
	}
}