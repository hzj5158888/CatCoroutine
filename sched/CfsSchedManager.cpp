//
// Created by hzj on 25-1-14.
//

#include <set>

#include "../include/CoPrivate.h"
#include "include/CfsSched.h"

void CfsSchedManager::apply(Co_t * co)
{
	// 协程运行中
	if (co->status == CO_RUNNING) [[unlikely]]
		throw ApplyReadyCoException();
	// 协程在调度器中
	if (co->status == CO_READY && co->ready_self != CoReadyIter{}) [[unlikely]]
		return;

	int min_scheduler_idx = 0;
	uint64_t min_v_runtime = INF;
	for (int i = 0; i < schedulers.size(); i++)
	{
		if (schedulers[i]->sum_v_runtime < min_v_runtime)
		{
			min_v_runtime = schedulers[i]->sum_v_runtime;
			min_scheduler_idx = i;
		}
	}

	co->scheduler = schedulers[min_scheduler_idx];
	schedulers[min_scheduler_idx]->apply_ready(co);
}

void CfsSchedManager::wakeup_from_await(Co_t * co)
{
	if (co == nullptr) [[unlikely]]
		return;

	std::lock_guard<spin_lock> lock(co->m_lock);
	auto caller_q = &co->await_caller;
	while (!caller_q->empty())
	{
		auto cur = caller_q->front();
		caller_q->pop();

		cur->status_lock.lock();
		cur->status = CO_READY;
		cur->await_callee = nullptr;
		cur->status_lock.unlock();
		apply(cur);
	}
}
