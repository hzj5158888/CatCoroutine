//
// Created by hzj on 25-1-11.
//

#pragma once

#include <cstdint>
#ifdef __SCHED_RB_TREE__
#include <set>
#endif
#include <cstddef>
#include <vector>
#include <exception>
#include <vector>

#include "CfsSchedDef.h"
#include "SchedulerDef.h"
#include "Scheduler.h"
#include "../../utils/include/sem_utils.h"
#include "../../include/CoPrivate.h"
#include "../../data_structure/include/QuaternaryHeap.h"

class ApplyRunningCoException : public std::exception
{
public:
	[[nodiscard]] const char * what() const noexcept override { return "Apply Ready Coroutine Exception"; }
};

struct CoPtrLessCmp
{
	bool operator () (const Co_t * a, const Co_t * b) const 
	{ 
		if (a == nullptr)
			return false;
		if (b == nullptr)
			return true;

		return *a < *b; 
	}
};

struct CoPtrGreaterCmp
{
	bool operator () (const Co_t * a, const Co_t * b) const 
	{ 
		return *a > *b; 
	}
};

class CfsScheduler : public Scheduler
{
public:
	int this_thread_id{};	

	std::atomic<uint64_t> min_v_runtime{};
	std::atomic<uint64_t> sum_v_runtime{};
	std::atomic<size_t> ready_count{};

#if defined(__SCHED_RB_TREE__) || defined(__SCHED_HEAP__)

	/* lockness data structure */
	spin_lock sched_lock{};
#ifdef __SCHED_RB_TREE__
	std::multiset<Co_t *, CoPtrLessCmp> ready{};
#elif __SCHED_HEAP__
	QuaternaryHeap<Co_t*, CoPtrLessCmp> ready{};
	QuaternaryHeap<Co_t*, CoPtrLessCmp> ready_fixed{};
#endif

#elif __SCHED_SKIP_LIST__
	SkipListLockFree<Co_t*, CoPtrLessCmp> ready{};
#endif

	counting_semaphore sem_ready{};
	Co_t * running_co{};
	Co_t * await_callee{};
	Context sched_ctx{};

	void push_to_ready(Co_t * co, bool);
	void apply_ready(Co_t * co);
	void apply_ready_all(const std::vector<Co_t *> & co_vec);
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
	void pull_half_co(std::vector<Co_t*> &);
	uint64_t get_load();
};

class CfsSchedManager
{
public:
	constexpr static uint64_t INF = 0x3f3f3f3f3f3f3f3f;
	std::vector<std::shared_ptr<CfsScheduler>> schedulers{};
	spin_lock init_lock{};
	spin_lock w_lock{};

	void apply(Co_t * co);
	void wakeup_await_co_all(Co_t * await_callee);
	std::vector<Co_t*> stealing_work(int);
	void push_scheduler(const std::shared_ptr<CfsScheduler> & s);
};
