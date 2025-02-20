//
// Created by hzj on 25-1-14.
//

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../include/CoPrivate.h"
#include "include/Scheduler.h"

void SchedManager::apply_impl(Co_t * co, int flag)
{
	// 协程运行中
	if (UNLIKELY(co->status == CO_RUNNING))
		throw ApplyRunningCoException();
	// 协程在调度器中
	if (UNLIKELY(co->status == CO_READY && co->scheduler != nullptr))
		assert(false);

	/* back to origin thread */
	if (co->sched.occupy_thread != -1)
	{
		if (flag == APPLY_NORMAL)
			schedulers.at(co->sched.occupy_thread)->apply_ready(co);
		else if (flag == APPLY_EAGER)
			schedulers.at(co->sched.occupy_thread)->apply_ready_eager(co);
		else
			schedulers.at(co->sched.occupy_thread)->apply_ready_lazy(co);

		return;
	}

	int min_scheduler_idx = 0;
	uint64_t min_balance = INF;
	for (size_t i = 0; i < schedulers.size(); i++)
	{
		auto balance = schedulers[i]->get_load();
		//std::cout << "<" << balance << ", " << i << ">" << std::endl;
		if (balance < min_balance)
		{
			min_balance = balance;
			min_scheduler_idx = i;
		}
	}

	if (flag == APPLY_NORMAL)
		schedulers[min_scheduler_idx]->apply_ready(co);
	else if (flag == APPLY_LAZY)
		schedulers[min_scheduler_idx]->apply_ready_lazy(co);
	else 
		schedulers[min_scheduler_idx]->apply_ready_eager(co);
}

void SchedManager::apply(Co_t * co)
{
	apply_impl(co, APPLY_NORMAL);
}

void SchedManager::apply_eager(Co_t * co)
{
	apply_impl(co, APPLY_EAGER);
}

void SchedManager::apply_lazy(Co_t * co)
{
	apply_impl(co, APPLY_LAZY);
}

void SchedManager::wakeup_await_co_all(Co_t * await_callee)
{
    DASSERT(await_callee != nullptr);
    std::lock_guard lock(await_callee->await_caller_lock);
	auto caller_q = &await_callee->await_caller;
	while (!caller_q->empty())
	{
		auto cur = caller_q->front();
        caller_q->pop();

		cur->await_callee = nullptr;
		apply_eager(cur);
	}
}

void SchedManager::stealing_work(int thread_from, std::vector<Co_t*> & res)
{
	DASSERT(thread_from >= 0 && (size_t)thread_from < schedulers.size());
#ifdef __STACK_STATIC__
	return;
#endif
	for (size_t i = 0; i < schedulers.size(); i++)
	{
		if ((int)i == thread_from)
			continue;

		schedulers[i]->pull_half_co(res);
	}
}

std::vector<Co_t*> SchedManager::stealing_work(int thread_from)
{
	DASSERT(thread_from >= 0 && (size_t)thread_from < schedulers.size());
#ifdef __STACK_STATIC__
	return std::vector<Co_t*>{};
#endif
	std::vector<Co_t *> res{};
	for (size_t i = 0; i < schedulers.size(); i++)
	{
		if ((int)i == thread_from)
			continue;

		schedulers[i]->pull_half_co(res);
	}

	return res;
}

void SchedManager::push_scheduler(const std::shared_ptr<Scheduler> & s, int idx)
{
	std::lock_guard lock(w_lock);
	schedulers.at(idx) = s;
    scheduler_count++;
	
	/* vector init finish */
	if (scheduler_count == schedulers.size())
		init_lock.unlock();
}
