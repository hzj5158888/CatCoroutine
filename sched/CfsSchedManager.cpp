//
// Created by hzj on 25-1-14.
//

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../include/CoPrivate.h"
#include "include/CfsSched.h"
#include "utils.h"

void CfsSchedManager::apply(Co_t * co)
{
	// 协程运行中
	if (co->status == CO_RUNNING) [[unlikely]]
		throw ApplyRunningCoException();
	// 协程在调度器中
	if (co->status == CO_READY && co->scheduler != nullptr) [[unlikely]]
		assert(false);

	/* back to origin coroutine */
	if (co->sched.occupy_thread != -1)
	{
		schedulers[co->sched.occupy_thread]->apply_ready(co);
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

	co->sched.occupy_thread = min_scheduler_idx;
	co->scheduler = schedulers[min_scheduler_idx].get();
	schedulers[min_scheduler_idx]->apply_ready(co);
}

void CfsSchedManager::wakeup_await_co_all(Co_t * await_callee)
{
	if (await_callee == nullptr) [[unlikely]]
		return;

	std::lock_guard lock(await_callee->await_lock);
	auto caller_q = &await_callee->await_caller;
	while (!caller_q->empty())
	{
		auto cur = caller_q->front();
		caller_q->pop();

		cur->await_callee = nullptr;
		apply(cur);
	}
}

std::vector<Co_t*> CfsSchedManager::stealing_work(int thread_from)
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

void CfsSchedManager::push_scheduler(const std::shared_ptr<CfsScheduler> & s)
{
	std::lock_guard lock(w_lock);
	schedulers.push_back(s);
	
	/* vector init finish */
	if (schedulers.size() == schedulers.capacity())
		init_lock.unlock();
}
