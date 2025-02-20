//
// Created by hzj on 25-1-14.
//

#pragma once

#include "CoCtx.h"
#include "utils.h"
#include <cstdint>
#include <pthread.h>
#include <atomic>
#include <chrono>
#include <sys/types.h>
#include <thread>

class Sleeper 
{
public:
	const std::chrono::nanoseconds delta{};
	uint32_t spinCount{};
	uint8_t backoff_count{};
	const uint8_t max_backoff{};

	static constexpr uint32_t kMaxActiveSpin = 1024;
	static constexpr uint32_t MaxBackOff = 5;
	static constexpr std::chrono::nanoseconds kMinYieldingSleep = std::chrono::microseconds(500);

	Sleeper() noexcept : delta(kMinYieldingSleep), max_backoff(MaxBackOff) { }

	explicit Sleeper(std::chrono::nanoseconds d) noexcept : delta(d), max_backoff(MaxBackOff) { }

	explicit Sleeper(uint8_t max_backoff) noexcept : delta(kMinYieldingSleep), max_backoff(max_backoff) { }

	int get_backoff()
	{
		backoff_count = std::min(backoff_count + 1, (int)max_backoff);
		return 1 << (co_ctx::loc->rand() % backoff_count);
	}

	void wait() noexcept 
	{
		if (LIKELY(spinCount < kMaxActiveSpin))
		{
			int back_off = get_backoff();
			for (int i = 0; i < back_off; i++)
				__builtin_ia32_pause();

			spinCount += back_off;
		} else {
			std::this_thread::sleep_for(delta);
			spinCount = 0;
			backoff_count = 0;
		}
	}

	int wait_for(int max_spin) noexcept 
	{
		if (LIKELY(spinCount < kMaxActiveSpin))
		{
			int back_off = get_backoff();
			for (int i = 0; i < back_off && i < max_spin; i++)
				__builtin_ia32_pause();

			spinCount += back_off;
			return back_off;
		} else {
			std::this_thread::sleep_for(delta);
			spinCount = 0;
			backoff_count = 0;
		}

		return 1;
	}
};

class spin_lock_sleep
{
private:
	static constexpr uint32_t kMaxActiveSpin = 1024;

	bool m_lock{false};
	std::chrono::nanoseconds kMinYieldingSleep = std::chrono::microseconds(500);
public:
	spin_lock_sleep() = default;
	spin_lock_sleep(std::chrono::nanoseconds kMinYieldingSleep) : kMinYieldingSleep(kMinYieldingSleep) {}

	void lock() noexcept
	{
		auto lock = reinterpret_cast<std::atomic<bool> *>(&m_lock);
		Sleeper sleeper{kMinYieldingSleep};
		for (;;)
		{
			// Optimistically assume the lock is free on the first try
			if (!lock->exchange(true, std::memory_order_acquire))
				return;

			// Wait for lock to be released without generating cache misses
			while (lock->load(std::memory_order_relaxed))
				sleeper.wait();
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

	bool try_lock_for(int32_t max_spin)
    {
        auto lock = reinterpret_cast<std::atomic<bool> *>(&m_lock);
		Sleeper sleeper{kMinYieldingSleep};
        while (max_spin > 0)
        {
            // Optimistically assume the m_lock is free on the first try
            if (!lock->exchange(true, std::memory_order_acquire))
                return true;

            // Wait for m_lock to be released without generating cache misses
			while (max_spin > 0 && lock->load(std::memory_order_relaxed))
				max_spin -= sleeper.wait_for(max_spin);
        }

        return false;
    }

	bool try_lock_for_backoff(uint8_t max_backoff = Sleeper::MaxBackOff)
    {
        auto lock = reinterpret_cast<std::atomic<bool> *>(&m_lock);
		Sleeper sleeper{kMinYieldingSleep};
        while (max_backoff > 0)
        {
            // Optimistically assume the m_lock is free on the first try
            if (!lock->exchange(true, std::memory_order_acquire))
                return true;

            // Wait for m_lock to be released without generating cache misses
			while (max_backoff > 0 && lock->load(std::memory_order_relaxed))
			{
				sleeper.wait();
				max_backoff--;
			}
        }

        return false;
    }

	void wait_until_lockable() noexcept
	{
		lock();
		unlock();
	}
};