#pragma once

#include <cstdint>
#include <chrono>

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
		auto now = std::chrono::system_clock::now();
		start_exec_timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
	}

	void end_exec()
	{
		auto now = std::chrono::system_clock::now();
		end_exec_timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
	}
};