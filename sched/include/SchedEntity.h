#pragma once

#include <cstdint>
#include <chrono>

#include "../../include/CoCtx.h"
#include "../../utils/include/utils.h"

class SchedEntity
{
public:
	bool can_migration{true};
	int occupy_thread{-1};

	uint64_t real_runtime{};
	uint64_t start_exec_timestamp{};
	uint64_t end_exec_timestamp{};

	virtual ~SchedEntity() = default;
	SchedEntity() 
	{ 
#ifdef __STACK_STATIC__
		can_migration = false;
#endif
	}

	void up_real_runtime()
	{
		real_runtime += end_exec_timestamp - start_exec_timestamp;
		end_exec_timestamp = start_exec_timestamp = 0;
	}

	void start_exec()
	{
		//auto now = std::chrono::system_clock::now();
		//start_exec_timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
		start_exec_timestamp = co_ctx::loc->clock.rdns();
	}

	void end_exec()
	{
		//auto now = std::chrono::system_clock::now();
		//end_exec_timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
		end_exec_timestamp = co_ctx::loc->clock.rdns();
	}
};