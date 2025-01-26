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

#include "utils/include/utils.h"

namespace co_ctx {
	bool is_init{false};
	std::shared_ptr<CfsSchedManager> manager{};
	std::atomic<uint32_t> coroutine_count{co::MAIN_CO_ID};
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
		if ( invoker->allocator == nullptr) [[unlikely]]
		{
			delete invoker;
		} else {
			auto alloc = static_cast<MemoryPool*>(invoker->allocator);
			invoker->~InvokerBase();
			alloc->free_safe(invoker);
		}
	}

	std::pair<void *, void * (*)(void*, std::size_t)> get_invoker_alloc()
	{
		if (co_ctx::loc == nullptr) [[unlikely]]
			throw co::CoInitializationException();

		return {
			&co_ctx::loc->alloc.invoker_pool,
			get_member_func_addr<void * (*)(void*, std::size_t)>(&MemoryPool::allocate_safe)
		};
	}

	void init_other(int thread_idx) noexcept
	{
		/* init other thread */
		co_ctx::loc = std::make_shared<local_t>();
		co_ctx::loc->thread_id = std::this_thread::get_id();
		co_ctx::loc->scheduler = std::make_shared<CfsScheduler>();
		co_ctx::loc->scheduler->manager = co_ctx::manager;
		co_ctx::manager->push_scheduler(co_ctx::loc->scheduler);
		/* create scheduler loop context */
		auto ctx = &co_ctx::loc->scheduler->sched_ctx;
		auto start_ptr = get_member_func_addr<void(*)(void*)>(&CfsScheduler::start);
		make_context(ctx, start_ptr, co_ctx::loc->scheduler.get());
		co_ctx::loc->alloc.dyn_stk_pool.alloc_stk(ctx);
		if (thread_idx > 0)
			co_ctx::loc->scheduler->jump_to_sched();
	}

	void init()
	{
		/* currency is main thread */
		/* init scheduler manager */
		co_ctx::manager = std::make_shared<CfsSchedManager>();
		co_ctx::manager->schedulers.reserve(CPU_CORE);
		co_ctx::manager->init_lock.lock();
		/* init thread local storage */
		init_other(0);

		/* init other thread */
		for (int i = 0; i < CPU_CORE - 1; i++)
			std::thread{init_other, i + 1}.detach();

		/* init finish */
		co_ctx::is_init = true;

		/* create main coroutine */
		Co_t * co = new Co_t{};
		/* co->id == 1 */
		co->id = co_ctx::coroutine_count++;
		co->sched.nice = PRIORITY_NORMAL;
		/* doesn't need to alloc stack */
		/* just save context */
		int res = save_context(co->ctx.get_jmp_buf(), &co->ctx.first_full_save);
		if (res == CONTEXT_RESTORE) // Restore Main Coroutine exec
			return;

		/* main coroutine doesn't need to up stack size */
		/* apply main coroutine */
		co_ctx::manager->apply(co);
		/* scheduler loop */
		co_ctx::loc->scheduler->jump_to_sched();
	}

    void destroy(void * handle)
	{
		auto co = static_cast<Co_t*>(handle);
		/* 等待状态更新 */
		co->status_lock.lock();
		if (co->status != CO_DEAD) [[unlikely]]
		{
			co->status_lock.unlock();
			throw DestroyBeforeCloseException();
		}
		co->status_lock.unlock();

		/* do not delete Main co */
		if (co->id != MAIN_CO_ID) [[likely]]
			delete co;
	}

    void * create(void (*func)(void *), void * arg, int nice)
    {
		if (!co_ctx::is_init) [[unlikely]]
			return nullptr;

		Co_t * co = static_cast<Co_t*>(co_ctx::loc->alloc.co_pool.allocate_safe(sizeof(Co_t)));
		if (co == nullptr) [[unlikely]]
			return nullptr;

		co = new (co) Co_t{}; // construct
		co->id = co_ctx::coroutine_count++;
		co->allocator = &co_ctx::loc->alloc.co_pool;
		co->sched.nice = nice;
		make_context_wrap(&co->ctx, &wrap, func, arg);
		// 由 scheduler 负责分配 stack
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

		/* 获取 callee coroutine 状态锁 */
		auto callee = static_cast<Co_t*>(handle);
		callee->status_lock.lock();
		/* callee Co 已经消亡 */
		if (callee->status == CO_DEAD)
		{
			callee->status_lock.unlock();
			return;
		}
		callee->status_lock.unlock();

		/* 打断当前 Coroutine */
		auto running_co = co_ctx::loc->scheduler->running_co;
		int res = save_context(running_co->ctx.get_jmp_buf(), &running_co->ctx.first_full_save);
		if (res == CONTEXT_RESTORE)
			return;

#ifdef __STACK_STATIC__
		/* update stack size after save context */
		if (running_co->ctx.static_stk_pool != nullptr) [[likely]]
			running_co->ctx.set_stk_size();
#elif __STACK_DYN__
		/* update stack size after save context */
		running_co->ctx.set_stk_dyn_size();
#endif

		co_ctx::loc->scheduler->await_callee = callee;
		co_ctx::loc->scheduler->jump_to_sched(CALL_AWAIT);
	}

    void yield()
	{
		[[unlikely]] DASSERT(co_ctx::loc->scheduler->running_co != nullptr);

		auto running_co = co_ctx::loc->scheduler->running_co;
		int res = save_context(running_co->ctx.get_jmp_buf(), &running_co->ctx.first_full_save);
		if (res == CONTEXT_RESTORE)
			return;

#ifdef __STACK_STATIC__
		/* update stack size after save context */
		if (running_co->id != MAIN_CO_ID) [[likely]]
			running_co->ctx.set_stk_size();
#elif __STACK_DYN__
		/* update stack size after save context */
		running_co->ctx.set_stk_dyn_size();
#endif

		co_ctx::loc->scheduler->jump_to_sched(CALL_YIELD);
	}
}