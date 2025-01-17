//
// Created by hzj on 25-1-11.
//

#ifndef COROUTINE_SCHEDULER_H
#define COROUTINE_SCHEDULER_H

#include <chrono>

#include "../../include/CoPrivate.h"


class SchedEntity
{
public:
	uint64_t real_runtime{};
	uint64_t start_exec_timestamp{};
	uint64_t end_exec_timestamp{};

	virtual ~SchedEntity() = default;

	void up_real_runtime()
	{
		real_runtime += end_exec_timestamp - start_exec_timestamp;
		end_exec_timestamp = start_exec_timestamp = 0;
	}

	void start_exec()
	{
		auto now = std::chrono::high_resolution_clock::now();
		start_exec_timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
	}

	void end_exec()
	{
		auto now = std::chrono::high_resolution_clock::now();
		end_exec_timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
	}

	[[nodiscard]] virtual uint64_t priority() const { return 0; }
};

class Scheduler
{
public:
	virtual ~Scheduler() = default;

	virtual void run(Co_t * co) {}
	virtual Co_t * interrupt() { return nullptr; }
};

#endif //COROUTINE_SCHEDULER_H
