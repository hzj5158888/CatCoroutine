//
// Created by hzj on 25-1-14.
//

#ifndef COROUTINE_UTILS_INCLUDE_SPIN_LOCK_H
#define COROUTINE_UTILS_INCLUDE_SPIN_LOCK_H

#include <memory>
#include <utility>
#include <atomic>

class spin_lock
{
private:
	std::atomic<bool> m_lock{false};
public:
	void lock() noexcept
	{
		for (;;)
		{
			// Optimistically assume the lock is free on the first try
			if (!m_lock.exchange(true, std::memory_order_acquire))
				return;

			// Wait for lock to be released without generating cache misses
			while (m_lock.load(std::memory_order_relaxed))
			{
				// Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
				// hyper-threads
				__builtin_ia32_pause();
			}
		}
	}

	bool try_lock() noexcept
	{
		// First do a relaxed load to check if lock is free in order to prevent
		// unnecessary cache misses if someone does while(!try_lock())
		return !m_lock.load(std::memory_order_relaxed) &&
			!m_lock.exchange(true, std::memory_order_acquire);
	}

	void unlock() noexcept
	{
		m_lock.store(false, std::memory_order_release);
	}
};

#endif //COROUTINE_UTILS_INCLUDE_SPIN_LOCK_H
