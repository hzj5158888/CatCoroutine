#include <cstdint>
#include <memory>
#include <thread>
#include <cstring>

#include "context/include/Context.h"
#include "include/Coroutine.h"
#include "include/CoPrivate.h"
#include "sched/include/CfsSched.h"
#include "include/AllocatorGroup.h"
#include "allocator/include/MemoryPool.h"

namespace co_ctx {
	bool is_init{false};
	std::shared_ptr<CfsSchedManager> manager{};
	std::atomic<uint32_t> coroutine_count{co::MAIN_CO_ID};
	thread_local local_t * loc{};
}

namespace co {
	void make_dead()
	{
		co_ctx::loc->scheduler->make_dead();
		/* 跳转到调度器 */
		co_ctx::loc->scheduler->jump_to_exec();
	}

	void wrap(void (*func)(void*), void * arg)
	{
		func(arg);
		make_dead();
	}

	static void invoker_wrapper(void * self)
	{
		auto invoker = static_cast<InvokerBase*>(self);
		invoker->operator()();
		if (invoker->allocator == nullptr) [[unlikely]]
		{
			delete invoker;
		} else {
			auto alloc = reinterpret_cast<MemoryPool*>(invoker->allocator);
			invoker->~InvokerBase();
			alloc->free_safe(invoker);
		}
	}

	std::pair<void *, void * (*)(void*, std::size_t)> get_invoker_alloc()
	{
		if (co_ctx::loc->alloc == nullptr) [[unlikely]]
			throw co::CoInitializationException();

		return {
			&co_ctx::loc->alloc->invoker_pool,
			get_member_func_addr<void * (*)(void*, std::size_t)>(&MemoryPool::allocate_safe)
		};
	}

	void init_other() noexcept
	{
		/* init other thread */
		co_ctx::loc = new local_t;
		co_ctx::loc->thread_id = std::this_thread::get_id();
		co_ctx::loc->alloc = std::make_unique<AllocatorGroup>();
		std::cout << "what" << std::endl;
		co_ctx::loc->scheduler = std::make_shared<CfsScheduler>();
		co_ctx::loc->scheduler->manager = co_ctx::manager;
		co_ctx::manager->schedulers.push_back(co_ctx::loc->scheduler);
		co_ctx::loc->scheduler->start();
		std::cout << "what" << std::endl;
	}

	void init()
	{
		/* currency is main thread */
		/* init scheduler manager */
		co_ctx::manager = std::make_shared<CfsSchedManager>();
		co_ctx::manager->schedulers.reserve(CPU_CORE);
		/* init thread local storage */
		co_ctx::loc = new local_t;
		std::cout << &co_ctx::loc << std::endl;
		std::cout << co_ctx::loc << std::endl;
		/* init scheduler and other */
		co_ctx::loc->thread_id = std::this_thread::get_id();
		co_ctx::loc->alloc = std::make_unique<AllocatorGroup>();
		co_ctx::loc->scheduler = std::make_shared<CfsScheduler>();
		co_ctx::loc->scheduler->manager = co_ctx::manager;
		co_ctx::manager->schedulers.push_back(co_ctx::loc->scheduler);

		/* init other thread */

		std::thread{init_other}.detach();

		/* init finish */
		co_ctx::is_init = true;

		/* create main coroutine */
		Co_t * co = new Co_t{};
		co->id = co_ctx::coroutine_count++;
		co->scheduler = co_ctx::loc->scheduler;
		co->sched->nice = PRIORITY_NORMAL;
		/* doesn't need to alloc stack */
		/* just save context */
		int res = save_context(&co->ctx);
		if (res == CONTEXT_RESTORE) // Restore the exec
			return;

		co->status = CO_READY;
		/* apply main coroutine */
		co_ctx::manager->apply(co);
		/* scheduler loop */
		co_ctx::loc->scheduler->start();
	}

    void destroy(void * handle)
	{
		printf("destroy handle %p\n", handle);
		auto co = static_cast<Co_t*>(handle);
		co->status_lock.lock();
		if (co->status != CO_DEAD) [[unlikely]]
		{
			co->status_lock.unlock();
			throw DestroyBeforeCloseException();
		}

		co->status_lock.unlock();
		/* do not delete Main co */
		if (co->id != MAIN_CO_ID)
			delete co;
	}

    void * create(void (*func)(void *), void * arg, int nice)
    {
		if (!co_ctx::is_init) [[unlikely]]
			return nullptr;

		Co_t * co = static_cast<Co_t*>(co_ctx::loc->alloc->co_pool.allocate_safe(sizeof(Co_t)));
		if (co == nullptr) [[unlikely]]
			return nullptr;

		co = new (co) Co_t{}; // construct
		co->id = co_ctx::coroutine_count++;
		co->allocator = &co_ctx::loc->alloc->co_pool;
		co->scheduler = co_ctx::loc->scheduler;
		co->sched->nice = nice;
		auto stk = co_ctx::loc->alloc->stk_pool.alloc_static_stk(co);
		if (stk == nullptr) [[unlikely]]
			return nullptr;

		make_context(&co->ctx, stk, &wrap, func, arg);
		co->status = CO_READY;
		co_ctx::manager->apply(co);
        return co;
    }

	void * create(void * invoker_self, int nice)
	{
		return create(&invoker_wrapper, invoker_self, nice);
	}

	void await(void * handle)
	{
		if (handle == nullptr) [[unlikely]]
			throw Co_UnInitialization_Exception();

		/* 获取 当前coroutine 状态锁 */
		auto cur_co = reinterpret_cast<Co_t*>(handle);
		cur_co->status_lock.lock();

		if (cur_co->status == CO_DEAD)
		{
			cur_co->status_lock.unlock();
			return;
		}

		/* 获取 运行中coroutine 状态锁 */
		auto running_co = co_ctx::loc->scheduler->running_co;
		[[unlikely]] assert(running_co != nullptr);
		running_co->status_lock.lock();

		co_ctx::loc->scheduler->running_co = nullptr;

		/* save caller context */
		int res = save_context(&running_co->ctx);
		if (res == CONTEXT_RESTORE)
			return;

		/* set caller to waiting */
		running_co->status = CO_WAITING;
		running_co->await_callee = cur_co;
		if (running_co->id != co::MAIN_CO_ID)
			running_co->ctx.static_stk_pool->release_stack(running_co);
		running_co->sched->end_exec();
		/* release running co status lock */
		running_co->status_lock.unlock();

		/* set callee */
		cur_co->m_lock.lock();
		cur_co->await_caller.push(running_co);
		cur_co->m_lock.unlock();
		/* release cur co status lock */
		cur_co->status_lock.unlock();

		/* go to work loop */
		co_ctx::loc->scheduler->jump_to_exec();
	}

    void suspend() {}
}