#pragma once

#include <cstdint>
#include <thread>

#include "../utils/include/tscns.h"
#include "../sched/include/SchedulerDef.h"
#include "AllocatorGroup.h"
#include "xor_shift_rand.h"

struct local_t
{
	std::thread::id thread_id{};
	AllocatorGroup alloc{};
	std::shared_ptr<Scheduler> scheduler{};
	TSCNS clock{};
	xor_shift_32_rand rand{};
};

namespace co_ctx
{
	extern bool is_init;
	extern std::shared_ptr<SchedManager> manager;
	extern std::atomic<uint32_t> coroutine_count;
#ifdef __DEBUG_SCHED__
    extern std::unordered_set<Co_t*> co_vec;
    extern std::unordered_multiset<Co_t*> running_co;
    extern spin_lock m_lock;
#endif
	extern thread_local std::shared_ptr<local_t> loc;
}