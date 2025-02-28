//
// Created by hzj on 25-1-14.
//

#ifndef COROUTINE_UTILS_INCLUDE_SPIN_LOCK_H
#define COROUTINE_UTILS_INCLUDE_SPIN_LOCK_H

#include <atomic>

namespace co {
    class spin_lock {
    private:
        bool m_lock{false};
    public:
        void lock() noexcept {
            auto lock = reinterpret_cast<std::atomic<bool> *>(&m_lock);
            for (;;) {
                // Optimistically assume the m_lock is free on the first try
                if (!lock->exchange(true, std::memory_order_acquire))
                    return;

                // Wait for m_lock to be released without generating cache misses
                while (lock->load(std::memory_order_relaxed)) {
                    // Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
                    // hyper-threads
                    __builtin_ia32_pause();
                }
            }
        }

        bool try_lock_for(int32_t max_spin) {
            auto lock = reinterpret_cast<std::atomic<bool> *>(&m_lock);
            while (max_spin > 0) {
                // Optimistically assume the m_lock is free on the first try
                if (!lock->exchange(true, std::memory_order_acquire))
                    return true;

                // Wait for m_lock to be released without generating cache misses
                while (max_spin > 0 && lock->load(std::memory_order_relaxed))
                    max_spin--;
            }

            return false;
        }

        bool try_lock() noexcept {
            auto lock = reinterpret_cast<std::atomic<bool> *>(&m_lock);
            // First do a relaxed load to check if m_lock is free in order to prevent
            // unnecessary cache misses if someone does while(!try_lock())
            return !lock->load(std::memory_order_relaxed) &&
                   !lock->exchange(true, std::memory_order_acquire);
        }

        void unlock() noexcept {
            auto lock = reinterpret_cast<std::atomic<bool> *>(&m_lock);
            lock->store(false, std::memory_order_release);
        }

        bool lockable() noexcept {
            auto lock = reinterpret_cast<std::atomic<bool> *>(&m_lock);
            return !lock->load(std::memory_order_acquire);
        }

        void wait_until_lockable() noexcept {
            lock();
            unlock();
        }
    };
}

#endif //COROUTINE_UTILS_INCLUDE_SPIN_LOCK_H