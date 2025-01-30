#include <cassert>
#include <execinfo.h>

#include "CfsSched.h"
#include "../../context/include/Context.h"
#include "../../include/CoPrivate.h"
#include "Coroutine.h"
#include "utils.h"

void CfsScheduler::apply_ready(Co_t * co)
{
	if (co->sched.v_runtime == 0)
		co->sched.v_runtime = min_v_runtime;
	sum_v_runtime += co->sched.v_runtime;

	co->status_lock.lock();
	co->status = CO_READY;
	co->status_lock.unlock();

#ifdef __SCHED_SKIP_LIST__
	ready.push(co);
#elif __SCHED_RB_TREE__
	sched_lock.lock();
	ready.insert(co);
	sched_lock.unlock();
#elif __SCHED_HEAP__
	sched_lock.lock();
	ready.push(co);
	sched_lock.unlock();
#endif

	sem_ready.signal();
}

void CfsScheduler::remove_ready(Co_t * co)
{
	/* never used */
	assert(false);
	DASSERT(co->status == CO_READY);

	sem_ready.wait();

#ifdef __SCHED_SKIP_LIST__
	ready.erase(co);
	DASSERT(ready.exists(co) == false);
	if (!ready.empty()) [[likely]]
		min_v_runtime = ready.front()->sched.v_runtime;

#elif __SCHED_RB_TREE__
	sched_lock.lock();
	auto iter = ready.find(co);
	if (iter == ready.end())
	{
		sched_lock.unlock();
		return;
	}
	ready.erase(iter);
	sched_lock.unlock();
#elif __SCHED_HEAP__
	assert(false);
#endif

	sum_v_runtime.fetch_sub(co->sched.v_runtime, std::memory_order_release);
}

void CfsScheduler::remove_from_scheduler(Co_t * co)
{
	if (co->status == CO_READY)
		remove_ready(co);
	else {
		sum_v_runtime -= co->sched.v_runtime;
	}

	co->scheduler = nullptr;
}

Co_t * CfsScheduler::pickup_ready()
{
	sem_ready.wait();

#ifdef __SCHED_SKIP_LIST__
	auto ans = ready.front();
	ready.erase(ans);
	DASSERT(ready.exists(ans) == false);
	if (!ready.empty()) [[likely]]
		min_v_runtime = ready.front()->sched.v_runtime;

#elif __SCHED_RB_TREE__
	sched_lock.lock();
	auto iter = ready.begin();
	auto ans = *iter;
	ready.erase(iter);
	if (!ready.empty()) [[likely]]
		min_v_runtime = (*ready.begin())->sched.v_runtime;

	sched_lock.unlock();
#elif __SCHED_HEAP__
	sched_lock.lock();
	
	auto ans = ready.top();
	ready.pop();
	if (!ready.empty()) [[likely]]
		min_v_runtime = ready.top()->sched.v_runtime;

	sched_lock.unlock();
#endif

	return ans;
}

void CfsScheduler::run(Co_t * co) // 会丢失当前上下文，最后执行
{
	[[unlikely]] assert(co->status == CO_READY);

	co->status_lock.lock();
	co->sched.start_exec();
	co->stk_active_lock.lock(); // 获取栈帧所有权
	if (co->id != co::MAIN_CO_ID)  [[likely]] // 设置 非main coroutine 栈帧
	{
#ifdef __STACK_STATIC__
		/* 设置stack分配器 */
		if (co->ctx.static_stk_pool == nullptr) [[unlikely]]
			co->ctx.static_stk_pool = &co_ctx::loc->alloc.stk_pool;

		/* 分配堆栈 */
		co->ctx.static_stk_pool->alloc_static_stk(co);
#elif __STACK_DYN__
		if (co->ctx.stk_dyn_alloc == nullptr) [[unlikely]]
			co_ctx::loc->alloc.dyn_stk_pool.alloc_stk(&co->ctx);
#endif
	}

	running_co = co;
	co->status = CO_RUNNING;
	co->sched.start_exec();
	co->status_lock.unlock();
	switch_context(&co->ctx);
}

/* Coroutine 中断执行 */
Co_t * CfsScheduler::interrupt(int new_status, bool unlock_exit)
{
	DASSERT(running_co != nullptr);

	running_co->status_lock.lock();
	/* update the status */
	running_co->status = new_status;
	/* update running time */
	sum_v_runtime -= running_co->sched.v_runtime;
	running_co->sched.end_exec();
	running_co->sched.up_v_runtime();
	sum_v_runtime += running_co->sched.v_runtime;
	/* release stack */
	running_co->stk_active_lock.unlock();

	/* unlock before exit */
	if (unlock_exit)
		running_co->status_lock.unlock();

	/* set interrupt coroutine */
	auto ans = running_co;
	running_co = nullptr;

	return ans;
}

void CfsScheduler::coroutine_yield()
{
	/* interrupt coroutine */
	auto yield_co = interrupt(CO_INTERRUPT, false);
	remove_from_scheduler(yield_co);

	/* yield finish */
	yield_co->status_lock.unlock();
	
#ifdef __STACK_STATIC__
	/* release static stack */
	if (yield_co->ctx.static_stk_pool != nullptr) [[likely]]
		yield_co->ctx.static_stk_pool->release_stack(yield_co);
#endif

	/* apply to scheduler */
	manager->apply(yield_co);
}

void CfsScheduler::coroutine_await()
{
	DASSERT(await_callee != nullptr);

	auto caller = interrupt(CO_WAITING, false);

	await_callee->await_lock.lock();
	await_callee->await_caller.push(caller);
	caller->await_callee = await_callee;
	/* release caller */
	caller->status_lock.unlock();
	await_callee->await_lock.unlock();
	await_callee = nullptr;

#ifdef __STACK_STATIC__
	/* release static stack */
	if (caller->ctx.static_stk_pool != nullptr) [[likely]]
		caller->ctx.static_stk_pool->release_stack(caller);
#endif
}

void CfsScheduler::coroutine_dead()
{
	auto dead_co = interrupt(CO_DEAD, false);
	DASSERT(dead_co != nullptr);

	/* wakeup waiting coroutine */
	co_ctx::manager->wakeup_await_co_all(dead_co);

	/* 释放栈空间 */
#ifdef __STACK_DYN__
	if (dead_co->ctx.stk_dyn_mem != nullptr) [[likely]]
	{
		auto stk_pool = dead_co->ctx.stk_dyn_alloc;
		stk_pool->free_stk(&dead_co->ctx);
	}
#elif __STACK_STATIC__
	if (dead_co->id != co::MAIN_CO_ID) [[likely]]
	{
		auto stk_pool = dead_co->ctx.static_stk_pool;
		stk_pool->destroy_stack(dead_co);
	}
#endif

	dead_co->status = CO_DEAD;
	dead_co->status_lock.unlock();
}

void CfsScheduler::process_callback(int arg)
{
	switch (arg)
	{
	case CALL_AWAIT:
		coroutine_await();
		break;
	case CALL_YIELD:
		coroutine_yield();
		break;
	case CALL_DEAD:
		coroutine_dead();
		break;
	case CONTEXT_RESTORE:
		break;
	default:
		assert(false && "unknown arg");
	}
}

[[noreturn]] void CfsScheduler::start()
{
	while (true)
	{
		int res = save_context(sched_ctx.get_jmp_buf(), &sched_ctx.first_full_save);
		if (res != CONTEXT_CONTINUE)
		{
			process_callback(res);
			continue;
		}

		auto co = pickup_ready();
		run(co);
	}
}

void CfsScheduler::jump_to_sched(int arg)
{
	switch_context(&sched_ctx, arg);
}

