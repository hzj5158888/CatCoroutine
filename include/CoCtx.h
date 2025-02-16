#pragma once

#include <thread>
#include <unordered_set>

#include "../utils/include/tscns.h"
#include "../sched/include/SchedulerDef.h"
#include "AllocatorGroup.h"

struct local_t
{
	std::thread::id thread_id{};
	AllocatorGroup alloc{};
	std::shared_ptr<Scheduler> scheduler{};
	TSCNS clock{};
};

namespace co_ctx
{
	extern bool is_init;
	extern std::shared_ptr<SchedManager> manager;
	extern std::atomic<uint32_t> coroutine_count;
#ifdef __DEBUG_SCHED__
    extern std::unordered_set<Co_t*> co_vec;
    extern spin_lock m_lock;
#endif
	extern thread_local std::shared_ptr<local_t> loc;
}