#pragma once

#include <thread>

#include "../utils/include/tscns.h"
#include "AllocatorGroup.h"

struct local_t
{
	std::thread::id thread_id{};
	AllocatorGroup alloc{};
	std::shared_ptr<CfsScheduler> scheduler{};
	TSCNS clock{};
};

namespace co_ctx
{
	extern bool is_init;
	extern std::shared_ptr<CfsSchedManager> manager;
	extern std::atomic<uint32_t> coroutine_count;
	extern thread_local std::shared_ptr<local_t> loc;
}