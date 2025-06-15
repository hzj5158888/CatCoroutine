//
// Created by hzj on 25-1-14.
//
#pragma once

#include <atomic>
#include <cstdint>
#include <utility>

#include "../../include/CoPrivate.h"
#include "../../allocator/include/MemoryPool.h"
#include "../../utils/include/spin_lock.h"
#include "../../data_structure/include/QueueLock.h"
#include "../../data_structure/include/QuaternaryHeapLock.h"
#include "../../data_structure/include/MultiSetLock.h"
#include "../../data_structure/include/MultiSetBtreeLock.h"
#include "../../data_structure/include/LockedContainer.h"
#include "../../data_structure/include/RandomStack.h"
#include "../../data_structure/include/Stack.h"
#include "../../utils/include/sem_utils.h"

namespace co {
    class SemClosedException : public std::exception {
    public:
        [[nodiscard]] const char *what() const noexcept override { return "Sem Closed Exception"; }
    };

    class SemOverflowException : public std::exception {
    public:
        [[nodiscard]] const char *what() const noexcept override { return "Sem Overflow Exception"; }
    };

    class SemWaiterOverflowException : public std::exception {
    public:
        [[nodiscard]] const char *what() const noexcept override { return "Sem Waiter Overflow Exception"; }
    };

    class Sem_t {
    private:
        constexpr static auto MIN_SPIN = 1;
        constexpr static auto MAX_SPIN = 64;
        constexpr static auto SPIN_LEVEL = 8;
        constexpr static auto COUNT_MASK = 0xffffffff;
        constexpr static auto WAITER_SHIFT = 32;
        constexpr static auto WAITER_MAX = UINT32_MAX;
        constexpr static auto COUNT_MAX = UINT32_MAX;
        constexpr static uint16_t QUEUE_NUMBER = 16;
        static_assert(QUEUE_NUMBER > 0);
        static_assert(is_pow_of_2(QUEUE_NUMBER));

        using callback_t = std::function<void(Co_t*)>;
    public:
        struct alignas(__CACHE_LINE__) co_wrap
        {
            Co_t * co{};
            callback_t func{};
            TimerTaskPtr timerTask{};

            co_wrap() = default;
            co_wrap(Co_t * co, const callback_t & f) : co(co), func(std::forward<const callback_t &>(f)) {}
            co_wrap(const co_wrap & oth) = default;
            ~co_wrap() = default;

            void swap(co_wrap && oth)
            {
                std::swap(co, oth.co);
                std::swap(func, oth.func);
                std::swap(timerTask, oth.timerTask);
            }

            co_wrap & operator = (co_wrap && oth) noexcept
            {
                swap(std::move(oth));
                return *this;
            }

            bool operator > (const co_wrap & oth) const { return co > oth.co; }
            bool operator < (const co_wrap & oth) const { return co < oth.co; }
        };

        using wait_q_t = LockedContainer<co_wrap, RandomStack<co_wrap>>;

        std::atomic<uint64_t> m_value{};
        uint32_t init_count{};
        std::atomic<int32_t> max_spin{MAX_SPIN};
        std::atomic<uint32_t> q_push_idx{};
        std::array<wait_q_t, QUEUE_NUMBER> wait_q{};
        std::atomic<uint32_t> q_pop_idx{};
        MultiSetLock<co_wrap> cancelable_wait{};
        
        alignas(__CACHE_LINE__) std::atomic<size_t> m_size{};

#ifdef __MEM_PMR__
        std::pmr::synchronized_pool_resource * alloc{};
#else
        MemoryPool *alloc{};
#endif

        explicit Sem_t(uint32_t val = 0);
        Sem_t(const Sem_t &sem) = delete;

        bool wait_for(std::chrono::microseconds duration);
        bool wait_for_then(std::chrono::microseconds duration, const callback_t & callback);
        bool wait_timed_impl(std::chrono::microseconds end_time, const callback_t & callback);
        void wait_impl(const callback_t & callback);
        void wait();
        void wait_then(const callback_t & callback);
        bool try_wait();
        void signal(bool call_func = true);
        static void release(Sem_t *ptr);
        std::optional<co_wrap> pick_from_wait_q();
        void inc_max_spin();
        void dec_max_spin();
        int64_t count();
        explicit operator int64_t();
        void operator delete(void *ptr) noexcept = delete;
    };
}