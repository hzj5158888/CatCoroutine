//
// Created by hzj on 25-1-11.
//

#pragma once

#include <set>
#include <vector>
#include <exception>
#include <utility>

#include "CfsSchedDef.h"
#include "SchedulerDef.h"
#include "Scheduler.h"
#include "../../utils/include/sem_utils.h"
#include "../../include/Coroutine.h"
#include "../../utils/include/sem_utils.h"
#include "../../include/CoPrivate.h"

/* include/linux/sched.h */
constexpr int nice_to_weigh[] = {
	/* -20 */     88761,     71755,     56483,     46273,     36291,
	/* -15 */     29154,     23254,     18705,     14949,     11916,
	/* -10 */     9548,     7620,     6100,     4904,     3906,
	/*  -5 */     3121,     2501,     1991,     1586,     1277,
	/*   0 */     1024,     820,      655,      526,      423,
	/*   5 */     335,      272,      215,      172,      137,
	/*  10 */     110,      87,       70,       56,       45,
	/*  15 */     36,       29,       23,       18,       15,
};

class ApplyReadyCoException : public std::exception
{
public:
	[[nodiscard]] const char * what() const noexcept override { return "Apply Ready Coroutine Exception"; }
};

class CfsSchedManager;
class CfsSchedEntity : public SchedEntity
{
public:
	constexpr static int nice_offset = 20;

	uint16_t nice{};
	uint64_t v_runtime{};

	[[nodiscard]] uint64_t priority() const override { return v_runtime; }

	void up_v_runtime()
	{
		up_real_runtime();
		v_runtime += (real_runtime << 10) / nice_to_weigh[nice + nice_offset];
	}
};

class CfsScheduler : public Scheduler
{
public:
	uint64_t min_v_runtime{};
	std::atomic<uint64_t> sum_v_runtime{};

	spin_lock sched_lock{};
	std::counting_semaphore sem_ready{};
	std::multiset<Co_t *, CoPtrLessCmp> ready{};
	std::shared_ptr<CfsSchedManager> manager{};
	Co_t * running_co{};
	Co_t * interrupt_co{};
	Co_t * dead_co{};

	Context sched_ctx{};

	void apply_ready(Co_t * co);
	void remove_ready(Co_t * co);
	void remove_from_scheduler(Co_t * co);
	[[nodiscard]] Co_t * pickup_ready();
	void run(Co_t * co) override;
	void make_dead();
	void jump_to_exec();
	Co_t * interrupt() override;
	[[noreturn]] void start();
	void process_co_trans();
};

class CfsSchedManager
{
public:
	using CoReadyIter = std::multiset<Co_t*, CoPtrLessCmp>::iterator;

	constexpr static uint64_t INF = 0x3f3f3f3f3f3f3f3f;
	std::vector<std::shared_ptr<CfsScheduler>> schedulers;

	void apply(Co_t * co);
	void wakeup_await_co_all(Co_t * await_callee);
	void coroutine_yield(Co_t * yield_co);
};
