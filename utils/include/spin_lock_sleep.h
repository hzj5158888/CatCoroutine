//
// Created by hzj on 25-1-14.
//

#pragma once

#include <memory>
#include <utility>
#include <atomic>
#include <chrono>
#include <thread>

class Sleeper 
{
	const std::chrono::nanoseconds delta{};
	uint32_t spinCount{};

	static constexpr uint32_t kMaxActiveSpin = 8000;

	public:
	static constexpr std::chrono::nanoseconds kMinYieldingSleep = std::chrono::microseconds(500);

	constexpr Sleeper() noexcept : delta(kMinYieldingSleep), spinCount(0) {}

	explicit Sleeper(std::chrono::nanoseconds d) noexcept : delta(d), spinCount(0) {}

	void wait() noexcept 
	{
		if (spinCount < kMaxActiveSpin) [[likely]]
		{
			++spinCount;
			__builtin_ia32_pause();
		} else {
			std::this_thread::sleep_for(delta);
		}
	}
};

class spin_lock_sleep
{
private:
	bool m_lock{false};
	std::chrono::nanoseconds kMinYieldingSleep = std::chrono::microseconds(500);
public:
	spin_lock_sleep() = default;
	spin_lock_sleep(std::chrono::nanoseconds kMinYieldingSleep) : kMinYieldingSleep(kMinYieldingSleep) {}

	void lock() noexcept
	{
		Sleeper sleeper{kMinYieldingSleep};
		auto lock = reinterpret_cast<std::atomic<bool> *>(&m_lock);
		for (;;)
		{
			// Optimistically assume the lock is free on the first try
			if (!lock->exchange(true, std::memory_order_acquire))
				return;

			// Wait for lock to be released without generating cache misses
			while (lock->load(std::memory_order_relaxed))
			{
				// Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
				// hyper-threads
				sleeper.wait();
			}
		}
	}

	bool try_lock() noexcept
	{
		auto lock = reinterpret_cast<std::atomic<bool> *>(&m_lock);
		// First do a relaxed load to check if lock is free in order to prevent
		// unnecessary cache misses if someone does while(!try_lock())
		return !lock->load(std::memory_order_relaxed) &&
			!lock->exchange(true, std::memory_order_acquire);
	}

	void unlock() noexcept
	{
		auto lock = reinterpret_cast<std::atomic<bool> *>(&m_lock);
		lock->store(false, std::memory_order_release);
	}
};
