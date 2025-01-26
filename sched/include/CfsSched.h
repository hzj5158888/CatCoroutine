//
// Created by hzj on 25-1-11.
//

#pragma once

#include <set>
#include <vector>
#include <queue>
#include <exception>
#include <vector>

#include "CfsSchedDef.h"
#include "SchedulerDef.h"
#include "Scheduler.h"
#include "../../utils/include/sem_utils.h"
#include "../../include/Coroutine.h"
#include "../../include/CoPrivate.h"
#include "../../utils/include/spin_lock_sleep.h"
#include "../../data_structure/include/SkipListLockFree.h"
#include "CfsSchedEntity.h"

class ApplyRunningCoException : public std::exception
{
public:
	[[nodiscard]] const char * what() const noexcept override { return "Apply Ready Coroutine Exception"; }
};

struct CoPtrLessCmp
{
	bool operator () (const Co_t * a, const Co_t * b) const 
	{ 
		return *a < *b; 
	}
};

struct CoPtrGreaterCmp
{
	bool operator () (const Co_t * a, const Co_t * b) const 
	{ 
		a->sched.prefetch_v_runtime();
		b->sched.prefetch_v_runtime();
		asm volatile("" ::: "memory");
		return *a > *b; 
	}
};

class CfsScheduler : public Scheduler
{
public:
	std::atomic<uint64_t> min_v_runtime{};
	std::atomic<uint64_t> sum_v_runtime{};

#if defined(__SCHED_RB_TREE__) || defined(__SCHED_HEAP__)
	/* lockness data structure */
	spin_lock_sleep sched_lock{};
#ifdef __SCHED_RB_TREE__
	std::multiset<Co_t *, CoPtrLessCmp> ready{};
#elif __SCHED_HEAP__
	std::priority_queue<Co_t*, std::vector<Co_t*>, CoPtrGreaterCmp> ready{};
#endif
#elif __SCHED_SKIP_LIST__
	SkipListLockFree<Co_t*, CoPtrLessCmp> ready{};
#endif
	counting_semaphore sem_ready{};
	std::shared_ptr<CfsSchedManager> manager{};
	Co_t * running_co{};
	Co_t * await_callee{};
	Context sched_ctx{};

	void apply_ready(Co_t * co);
	void remove_ready(Co_t * co);
	void remove_from_scheduler(Co_t * co);
	[[nodiscard]] Co_t * pickup_ready();
	void run(Co_t * co) override;
	void coroutine_yield();
	void coroutine_await();
	void coroutine_dead();
	void jump_to_sched(int arg = CONTEXT_RESTORE);
	Co_t * interrupt(int new_status = CO_INTERRUPT, bool unlock_exit = true);
	[[noreturn]] void start();
	void process_callback(int arg);
};

class CfsSchedManager
{
public:
	using CoReadyIter = std::multiset<Co_t*, CoPtrLessCmp>::iterator;

	constexpr static uint64_t INF = 0x3f3f3f3f3f3f3f3f;
	std::vector<std::shared_ptr<CfsScheduler>> schedulers;
	spin_lock init_lock{};
	spin_lock w_lock{};

	void apply(Co_t * co);
	void wakeup_await_co_all(Co_t * await_callee);
	void push_scheduler(const std::shared_ptr<CfsScheduler> & s);
};
