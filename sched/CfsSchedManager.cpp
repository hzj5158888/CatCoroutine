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
	if (co->status == CO_READY && co->scheduler != nullptr) [[unlikely]]
		assert(false);

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

void CfsSchedManager::coroutine_yield(Co_t * yield_co)
{
	int res = save_context(yield_co->ctx.get_jmp_buf(), &yield_co->ctx.first_full_save);
	if (res == CONTEXT_RESTORE)
		return;

	/* update stack size */
	yield_co->ctx.set_stk_size();

	/* process coroutine */
	yield_co->scheduler->interrupt();
	yield_co->scheduler->remove_from_scheduler(yield_co);
	apply(yield_co);
	co_ctx::loc->scheduler->jump_to_exec();
	// never arrive
}

void CfsSchedManager::wakeup_await_co_all(Co_t * await_callee)
{
	if (await_callee == nullptr) [[unlikely]]
		return;

	std::lock_guard<spin_lock> lock(await_callee->await_lock);
	auto caller_q = &await_callee->await_caller;
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
