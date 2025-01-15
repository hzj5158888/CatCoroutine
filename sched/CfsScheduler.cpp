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

	m_lock.lock();
	co->ready_self = ready.insert(co);
	m_lock.unlock();

	sem_ready.signal();
}

void CfsScheduler::remove_ready(Co_t * co)
{
	[[unlikely]] assert(co->status == CO_READY);

	m_lock.lock();
	auto iter = ready.find(co);
	if (iter == ready.end()) [[unlikely]]
	{
		m_lock.unlock();
		return;
	}

	ready.erase(iter);
	if (!ready.empty()) [[likely]]
		min_v_runtime = (*ready.begin())->sched->v_runtime;
	m_lock.unlock();

	co->ready_self = {};
	sum_v_runtime -= co->sched->v_runtime;
	sem_ready.wait();
}

void CfsScheduler::remove_scheduler(Co_t * co)
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

	m_lock.lock();
	auto ans = *ready.begin();
	ready.erase(ready.begin());
	if (!ready.empty()) [[likely]]
		min_v_runtime = (*ready.begin())->sched->v_runtime;
	m_lock.unlock();

	ans->ready_self = {};
	ans->sched->start_exec();
	return ans;
}

void CfsScheduler::run(Co_t * co) // 会丢失当前上下文，最后执行
{
	co->sched->start_exec();
	if (co->id != co::MAIN_CO_ID)  [[likely]]
	{
		auto stk_ptr = co->ctx.static_stk_pool->alloc_static_stk(co);
		co->ctx.set_stack(stk_ptr);
	}

	running_co = co;
	co->status = CO_RUNNING;
	switch_context(&co->ctx);
}

void CfsScheduler::make_dead()
{
	auto dead_co = running_co;
	[[unlikely]] assert(dead_co != nullptr);
	running_co = nullptr;

	dead_co->status_lock.lock();
	dead_co->status = CO_CLOSING;

	/* wakeup waiting coroutine */
	co_ctx::manager->wakeup_from_await(dead_co);

	/* 释放栈空间 */
	if (dead_co->ctx.static_stk_pool != nullptr) [[likely]]
	{
		auto stk_pool = dead_co->ctx.static_stk_pool;
		stk_pool->release_stack(dead_co);
		stk_pool->free_stack(dead_co);
	}

	dead_co->status = CO_DEAD;
	dead_co->status_lock.unlock();

	/* 等待空间最后回收 */
}

int32_t CfsScheduler::interrupt() // 信号处理回调函数
{
	int res = save_context(&running_co->ctx);
	if (res == CONTEXT_RESTORE)
		return CONTEXT_RESTORE;

	running_co->status_lock.lock();
	running_co->status = CO_READY;
	sum_v_runtime -= running_co->sched->v_runtime;
	running_co->sched->end_exec();
	running_co->sched->up_v_runtime();
	sum_v_runtime += running_co->sched->v_runtime;
	running_co->status_lock.unlock();

	running_co = nullptr;
	return CONTEXT_CONTINUE;
}

[[noreturn]] void CfsScheduler::start()
{
	while (true)
	{
		auto co = pickup_ready();
		run(co);
	}
}

void CfsScheduler::jump_to_exec()
{
	start();
}

