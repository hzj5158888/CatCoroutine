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
	std::vector<Co_t*> co_vec{};
	thread_local std::shared_ptr<local_t> loc{};
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

	void init_other(int thread_idx) noexcept
	{
		/* init other thread */
		co_ctx::loc = std::make_shared<local_t>();
		co_ctx::loc->thread_id = std::this_thread::get_id();
		co_ctx::loc->alloc = std::make_unique<AllocatorGroup>();
		co_ctx::loc->scheduler = std::make_shared<CfsScheduler>();
		co_ctx::loc->scheduler->manager = co_ctx::manager;
		co_ctx::manager->schedulers.push_back(co_ctx::loc->scheduler);
		/* create scheduler loop context */
		auto ctx = &co_ctx::loc->scheduler->sched_ctx;
		auto start_ptr = get_member_func_addr<void(*)(void*)>(&CfsScheduler::start);
		make_context(ctx, start_ptr, co_ctx::loc->scheduler.get());
		ctx->stk_dyn_capacity = 1024 * 1024;
		co_ctx::loc->alloc->stk_pool.alloc_dyn_stk_mem(ctx->stk_dyn_mem, ctx->stk_dyn, ctx->stk_dyn_capacity);
		ctx->stk_dyn_alloc = &co_ctx::loc->alloc->stk_pool.dyn_stk_pool;
		ctx->set_stack_dyn(ctx->stk_dyn);

		if (thread_idx > 0)
			co_ctx::loc->scheduler->jump_to_exec();
	}

	void init()
	{
		/* currency is main thread */
		/* init scheduler manager */
		co_ctx::manager = std::make_shared<CfsSchedManager>();
		co_ctx::manager->schedulers.reserve(CPU_CORE);
		/* init thread local storage */
		init_other(0);

		/* init other thread */
		for (int i = 0; i < CPU_CORE - 1; i++)
			std::thread{init_other, i + 1}.detach();

		/* init finish */
		co_ctx::is_init = true;

		/* create main coroutine */
		Co_t * co = new Co_t{};
		co->id = co_ctx::coroutine_count++;
		co->sched->nice = PRIORITY_NORMAL;
		/* doesn't need to alloc stack */
		/* just save context */
		int res = save_context(co->ctx.get_jmp_buf(), &co->ctx.first_full_save);
		if (res == CONTEXT_RESTORE) // Restore the exec
			return;

		co->status = CO_READY;
		/* apply main coroutine */
		co_ctx::manager->apply(co);
		co_ctx::co_vec.push_back(co);
		/* scheduler loop */
		co_ctx::loc->scheduler->jump_to_exec();
	}

    void destroy(void * handle)
	{
		//printf("destroy handle %p\n", handle);
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
		co->sched->nice = nice;
		// 由 scheduler 负责分配 stack
		make_context_wrap(&co->ctx, &wrap, func, arg);
		co->status = CO_READY;
		co_ctx::manager->apply(co);
		co_ctx::co_vec.push_back(co);
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
		auto cur_co = reinterpret_cast<Co_t*>(handle);
		cur_co->status_lock.lock();

		/* callee Co 已经消亡 */
		if (cur_co->status == CO_DEAD)
		{
			cur_co->status_lock.unlock();
			return;
		}

		/* 打断当前 Coroutine */
		auto running_co = co_ctx::loc->scheduler->interrupt();
		co_ctx::loc->scheduler->remove_from_scheduler(running_co);

		/* save caller context */
		int res = save_context(running_co->ctx.get_jmp_buf(), &running_co->ctx.first_full_save);
		if (res == CONTEXT_RESTORE)
			return;

		/* update stack size */
		running_co->ctx.set_stk_size();

		/* set caller to waiting */
		running_co->status = CO_WAITING;
		running_co->await_callee = cur_co;

		/* set callee */
		cur_co->await_lock.lock();
		cur_co->await_caller.push(running_co);
		cur_co->await_lock.unlock();
		/* release cur co status lock */
		assert(cur_co->scheduler != nullptr);
		if (cur_co->status == CO_READY)
			assert(!cur_co->scheduler->ready.empty());

		cur_co->status_lock.unlock();

		/* go to work loop */
		co_ctx::loc->scheduler->jump_to_exec();
	}

    void yield()
	{
		[[unlikely]] assert(co_ctx::loc->scheduler->running_co != nullptr);

		auto cur_co = co_ctx::loc->scheduler->running_co;
		co_ctx::manager->coroutine_yield(cur_co);
	}
}