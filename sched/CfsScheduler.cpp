//
// Created by hzj on 25-1-11.
//

#include "CfsSched.h"
#include "../../context/include/Context.h"
#include "../../include/CoPrivate.h"

void CfsScheduler::apply_ready(Co_t * co)
{
	if (co->sched->v_runtime == 0)
		co->sched->v_runtime = min_v_runtime;

	sum_v_runtime += co->sched->v_runtime;

	sched_lock.lock();
	co->ready_self = ready.insert(co);
	sched_lock.unlock();

	sem_ready.signal();
}

void CfsScheduler::remove_ready(Co_t * co)
{
	[[unlikely]] assert(co->status == CO_READY);

	sched_lock.lock();
	auto iter = ready.find(co);
	if (iter == ready.end()) [[unlikely]]
	{
		sched_lock.unlock();
		return;
	}

	ready.erase(iter);
	if (!ready.empty()) [[likely]]
		min_v_runtime = (*ready.begin())->sched->v_runtime;
	sched_lock.unlock();

	co->ready_self = {};
	sum_v_runtime -= co->sched->v_runtime;
	sem_ready.wait();
}

void CfsScheduler::remove_from_scheduler(Co_t * co)
{
	if (co->status == CO_READY)
		remove_ready(co);
	else
		sum_v_runtime -= co->sched->v_runtime;

	co->scheduler = nullptr;
}

Co_t * CfsScheduler::pickup_ready()
{
	sem_ready.wait();

	co_ctx::co_vec;

	sched_lock.lock();
	auto ans = *ready.begin();
	ready.erase(ready.begin());
	if (!ready.empty()) [[likely]]
		min_v_runtime = (*ready.begin())->sched->v_runtime;
	sched_lock.unlock();

	ans->ready_self = {};
	ans->sched->start_exec();
	return ans;
}

void CfsScheduler::run(Co_t * co) // 会丢失当前上下文，最后执行
{
	[[unlikely]] assert(co->status == CO_READY);

	co->sched->start_exec();
	if (co->id != co::MAIN_CO_ID)  [[likely]] // 设置 非main coroutine 栈帧
	{
		/* 设置stack分配器 */
		if (co->ctx.static_stk_pool == nullptr) [[unlikely]]
			co->ctx.static_stk_pool = &co_ctx::loc->alloc->stk_pool;

		/* 分配堆栈 */
		auto stk_ptr = co->ctx.static_stk_pool->alloc_static_stk(co);
		co->ctx.set_stack(stk_ptr);
	}

	running_co = co;
	co->status = CO_RUNNING;
	co->stk_active_lock.lock(); // 获取栈帧所有权
	switch_context(&co->ctx);
}

void CfsScheduler::make_dead()
{
	auto die_co = running_co;
	[[unlikely]] assert(die_co != nullptr);
	running_co = nullptr;

	die_co->status_lock.lock();
	die_co->status = CO_CLOSING;

	/* wakeup waiting coroutine */
	co_ctx::manager->wakeup_await_co_all(die_co);

	/* 等待空间最后回收 */
	dead_co = die_co;
}

/* Coroutine 中断执行 */
Co_t * CfsScheduler::interrupt()
{
	[[unlikely]] assert(running_co != nullptr);

	running_co->status_lock.lock();
	/* update the status */
	running_co->status = CO_READY;
	/* update running time */
	sum_v_runtime -= running_co->sched->v_runtime;
	running_co->sched->end_exec();
	running_co->sched->up_v_runtime();
	sum_v_runtime += running_co->sched->v_runtime;
	running_co->status_lock.unlock();

	/* set interrupt coroutine */
	interrupt_co = running_co;
	running_co = nullptr;

	return interrupt_co;
}

void CfsScheduler::process_co_trans()
{
	if (interrupt_co != nullptr)
	{
		/* 释放栈空间 */
		if (interrupt_co->ctx.static_stk_pool != nullptr) [[likely]]
		{
			auto stk_pool = interrupt_co->ctx.static_stk_pool;
			stk_pool->release_stack(interrupt_co);
		}

		co_ctx::co_vec;
		interrupt_co->stk_active_lock.unlock(); // stk_active = false
		interrupt_co = nullptr;
	}

	/* 释放栈空间 */
	if (dead_co != nullptr)
	{
		/* dead_co 完全执行完成 */
		dead_co->stk_active_lock.unlock(); // stk_active = false

		/* 释放栈空间 */
		if (dead_co->ctx.static_stk_pool != nullptr) [[likely]]
		{
			auto stk_pool = dead_co->ctx.static_stk_pool;
			stk_pool->release_stack(dead_co);
			stk_pool->free_stack(dead_co);
		}
		/* stack回收完成 */

		dead_co->status = CO_DEAD;
		dead_co->status_lock.unlock();
		dead_co = nullptr;
	}
}

[[noreturn]] void CfsScheduler::start()
{
	while (true)
	{
		int res = save_context(sched_ctx.get_jmp_buf(), &sched_ctx.first_full_save);
		if (res == CONTEXT_RESTORE)
			continue;

		process_co_trans();

		auto co = pickup_ready();
		run(co);
	}
}

void CfsScheduler::jump_to_exec()
{
	switch_context(&sched_ctx);
}

