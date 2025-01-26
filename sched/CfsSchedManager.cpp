//
// Created by hzj on 25-1-14.
//

#include <cstdint>
#include <set>

#include "../include/CoPrivate.h"
#include "include/CfsSched.h"

void CfsSchedManager::apply(Co_t * co)
{
	init_lock.lock();
	init_lock.unlock();

	// 协程运行中
	if (co->status == CO_RUNNING) [[unlikely]]
		throw ApplyRunningCoException();
	// 协程在调度器中
	if (co->status == CO_READY && co->scheduler != nullptr) [[unlikely]]
		assert(false);

	int min_scheduler_idx = 0;
	uint64_t min_balance = INF;
	for (size_t i = 0; i < schedulers.size(); i++)
	{
		auto & scheduler = schedulers[i];
		uint64_t balance = scheduler->sum_v_runtime.load(std::memory_order_relaxed);

		//std::cout << "<" << balance << ", " << i << ">" << std::endl;

		if (balance < min_balance)
		{
			min_balance = balance;
			min_scheduler_idx = i;
		}
	}

	co->scheduler = schedulers[min_scheduler_idx];
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

void CfsSchedManager::push_scheduler(const std::shared_ptr<CfsScheduler> & s)
{
	std::lock_guard lock(w_lock);
	schedulers.push_back(s);
	
	/* vector init finish */
	if (schedulers.size() == schedulers.capacity())
		init_lock.unlock();
}
