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

#include "atomic_utils.h"

namespace co {

    class SpinSleeper {
    public:
        const std::chrono::nanoseconds delta{};
        uint32_t spinCount{};
        uint8_t backoff_count{};
        const uint8_t max_backoff{};

        static constexpr uint32_t kMaxActiveSpin = 1024;
        static constexpr uint32_t MaxBackOff = 6;
        static constexpr std::chrono::nanoseconds kMinYieldingSleep = std::chrono::microseconds(500);

        SpinSleeper() noexcept: delta(kMinYieldingSleep), max_backoff(MaxBackOff) {}

        explicit SpinSleeper(std::chrono::nanoseconds d) noexcept: delta(d), max_backoff(MaxBackOff) {}

        explicit SpinSleeper(uint8_t max_backoff) noexcept: delta(kMinYieldingSleep), max_backoff(max_backoff) {}

        int get_backoff() {
            backoff_count = std::min(backoff_count + 1, (int) max_backoff);
            return 1 << (co_ctx::loc->rand() % backoff_count);
        }

        void wait() noexcept {
            if (LIKELY(spinCount < kMaxActiveSpin)) {
                int back_off = get_backoff();
                for (int i = 0; i < back_off; i++)
                    __builtin_ia32_pause();

                spinCount += back_off;
            } else {
                //std::this_thread::sleep_for(delta);
                std::this_thread::yield();
                spinCount = 0;
                backoff_count = 0;
            }
        }

        int wait_for(int max_spin) noexcept {
            if (LIKELY(spinCount < kMaxActiveSpin)) {
                int back_off = get_backoff();
                for (int i = 0; i < back_off && i < max_spin; i++)
                    __builtin_ia32_pause();

                spinCount += back_off;
                return back_off;
            } else {
                //std::this_thread::sleep_for(delta);
                std::this_thread::yield();
                spinCount = 0;
                backoff_count = 0;
            }

            return 1;
        }
    };

    class spin_lock_sleep {
    private:
        bool m_lock{false};
    public:
        std::chrono::nanoseconds kMinYieldingSleep = std::chrono::microseconds(500);

        spin_lock_sleep() = default;

        spin_lock_sleep(std::chrono::nanoseconds kMinYieldingSleep) : kMinYieldingSleep(kMinYieldingSleep) {}

        void lock() noexcept
        {
            auto lock = reinterpret_cast<std::atomic<bool> *>(&m_lock);
            SpinSleeper sleeper{kMinYieldingSleep};
            for (;;) {
                // Optimistically assume the lock is free on the first try
                if (!lock->exchange(true, std::memory_order_acquire))
                    return;

                // Wait for lock to be released without generating cache misses
                while (lock->load(std::memory_order_relaxed))
                    sleeper.wait();
            }
        }

        bool try_lock() noexcept {
            auto lock = reinterpret_cast<std::atomic<bool> *>(&m_lock);
            // First do a relaxed load to check if lock is free in order to prevent
            // unnecessary cache misses if someone does while(!try_lock())
            return !lock->load(std::memory_order_relaxed) &&
                   !lock->exchange(true, std::memory_order_acquire);
        }

        void unlock() noexcept {
            auto lock = reinterpret_cast<std::atomic<bool> *>(&m_lock);
            lock->store(false, std::memory_order_release);
        }

        bool try_lock_for(int32_t max_spin) {
            auto lock = reinterpret_cast<std::atomic<bool> *>(&m_lock);
            SpinSleeper sleeper{kMinYieldingSleep};
            while (max_spin > 0) {
                // Optimistically assume the m_lock is free on the first try
                if (!lock->exchange(true, std::memory_order_acquire))
                    return true;

                // Wait for m_lock to be released without generating cache misses
                while (max_spin > 0 && lock->load(std::memory_order_relaxed))
                    max_spin -= sleeper.wait_for(max_spin);
            }

            return false;
        }

        bool try_lock_for_backoff(uint8_t max_backoff = SpinSleeper::MaxBackOff) {
            auto lock = reinterpret_cast<std::atomic<bool> *>(&m_lock);
            SpinSleeper sleeper{kMinYieldingSleep};
            while (max_backoff > 0) {
                // Optimistically assume the m_lock is free on the first try
                if (!lock->exchange(true, std::memory_order_acquire))
                    return true;

                // Wait for m_lock to be released without generating cache misses
                while (max_backoff > 0 && lock->load(std::memory_order_relaxed)) {
                    sleeper.wait();
                    max_backoff--;
                }
            }

            return false;
        }

        void wait_until_lockable() noexcept {
            lock();
            unlock();
        }
    };

    namespace spin_rw_lock_sleep {
        constexpr static auto READER_SHIFT = sizeof(unsigned);
        constexpr static auto WRITER_MASK = (static_cast<size_t>(1) << READER_SHIFT) - 1;

        class reader;
        class writer;

        class locker
        {
        public:
            size_t m_value{};
            spin_lock_sleep w_lock{};

            std::atomic<size_t> * get_value() { return reinterpret_cast<std::atomic<size_t>*>(&m_value); }

            locker() = default;
            locker(std::chrono::nanoseconds kMinYieldingSleep)
            {
                this->w_lock = spin_lock_sleep(w_lock);
            }

            reader & get_reader() { return *reinterpret_cast<reader*>(this); }
            writer & get_writer() { return *reinterpret_cast<writer*>(this); }
        };

        class reader : public locker
        {
        public:
            void lock() noexcept
            {
                auto value = get_value();
                SpinSleeper sleeper{w_lock.kMinYieldingSleep};
                while (true)
                {
                    auto res = atomic_fetch_modify(*value, [](size_t cur_value) -> size_t
                    {
                        if ((cur_value & WRITER_MASK) == 0)
                            return cur_value + (static_cast<size_t>(1) << READER_SHIFT);
                        else
                            return cur_value;
                    });
                    if ((res & WRITER_MASK) == 0)
                        break;

                    while ((value->load(std::memory_order_relaxed) & WRITER_MASK) > 0)
                        sleeper.wait();
                }
            }

            bool try_lock() noexcept
            {
                auto value = get_value();
                auto res = atomic_fetch_modify(*value, [](size_t cur_value) -> size_t
                {
                    if ((cur_value & WRITER_MASK) == 0)
                        return cur_value + (static_cast<size_t>(1) << READER_SHIFT);
                    else
                        return cur_value;
                });

                return (res & WRITER_MASK) == 0;
            }

            void unlock() noexcept
            {
                auto value = get_value();
                atomic_fetch_modify(*value, [](size_t cur_value) -> size_t
                {
                    if ((cur_value >> READER_SHIFT) > 0)
                        return cur_value - (static_cast<size_t>(1) << READER_SHIFT);
                    else
                        return cur_value;
                });
            }

            bool try_lock_for(int32_t max_spin)
            {
                auto value = get_value();
                SpinSleeper sleeper{w_lock.kMinYieldingSleep};
                while (max_spin > 0)
                {
                    auto res = atomic_fetch_modify(*value, [](size_t cur_value) -> size_t
                    {
                        if ((cur_value & WRITER_MASK) == 0)
                            return cur_value + (static_cast<size_t>(1) << READER_SHIFT);
                        else
                            return cur_value;
                    });
                    if ((res & WRITER_MASK) == 0)
                        return true;

                    while ((value->load(std::memory_order_relaxed) & WRITER_MASK) > 0)
                        max_spin -= sleeper.wait_for(max_spin);
                }

                return false;
            }

            bool try_lock_for_backoff(uint8_t max_backoff = SpinSleeper::MaxBackOff)
            {
                auto value = get_value();
                SpinSleeper sleeper{w_lock.kMinYieldingSleep};
                while (max_backoff > 0)
                {
                    auto res = atomic_fetch_modify(*value, [](size_t cur_value) -> size_t
                    {
                        if ((cur_value & WRITER_MASK) == 0)
                            return cur_value + (static_cast<size_t>(1) << READER_SHIFT);
                        else
                            return cur_value;
                    });
                    if ((res & WRITER_MASK) == 0)
                        return true;

                    while ((value->load(std::memory_order_relaxed) & WRITER_MASK) > 0)
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
        class writer : public locker
        {
        public:
            static size_t dec_writer(size_t cur_value)
            {
                if ((cur_value & WRITER_MASK) > 0)
                    return cur_value - 1;
                else
                    return cur_value;
            }

            void lock() noexcept
            {
                auto value = get_value();
                SpinSleeper sleeper{w_lock.kMinYieldingSleep};

                /* increment writer */
                (*value)++;

                /* wait until reader == 0 */
                while ((value->load(std::memory_order_relaxed) >> READER_SHIFT) > 0)
                    sleeper.wait();

                /* wait lock */
                w_lock.lock();
            }

            bool try_lock() noexcept
            {
                auto value = get_value();

                bool ans{};
                atomic_fetch_modify(*value, [this, &ans](size_t cur_value) -> size_t
                {
                    if ((cur_value >> READER_SHIFT) == 0 && w_lock.try_lock())
                    {
                        ans = true;
                        return cur_value + 1;
                    } else {
                        ans = false;
                        return cur_value;
                    }
                });

                return ans;
            }

            bool try_lock_backoff(uint8_t max_backoff = SpinSleeper::MaxBackOff) noexcept
            {
                auto value = get_value();
                SpinSleeper sleeper{w_lock.kMinYieldingSleep};

                bool w_lock_res{};
                while (true)
                {
                    auto ans = atomic_fetch_modify(*value, [](size_t cur_value) -> size_t
                    {
                        if ((cur_value >> READER_SHIFT) == 0)
                            return cur_value + 1;
                        else
                            return cur_value;
                    });

                    if ((ans >> READER_SHIFT) == 0)
                    {
                        w_lock_res = w_lock.try_lock_for_backoff(max_backoff);
                        if (!w_lock_res)
                            atomic_fetch_modify(*value, dec_writer);

                        break;
                    }

                    while (value->load(std::memory_order_relaxed))
                    {
                        sleeper.wait();
                        max_backoff--;
                    }
                }

                return w_lock_res;
            }

            void unlock() noexcept
            {
                auto value = get_value();
                w_lock.unlock();
                atomic_fetch_modify(*value, dec_writer);
            }
        };
    }
}