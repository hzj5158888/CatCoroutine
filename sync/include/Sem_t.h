//
// Created by hzj on 25-1-14.
//
#pragma once

#include <atomic>
#include <cstdint>

#include "../../include/CoPrivate.h"
#include "../../allocator/include/MemoryPool.h"
#include "../../utils/include/spin_lock.h"
#include "../../data_structure/include/QueueLock.h"
#include "../../data_structure/include/QuaternaryHeapLock.h"
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

    class Sem_t {
    private:
        constexpr static auto MIN_SPIN = 2;
        constexpr static auto MAX_SPIN = 64;
        constexpr static auto SPIN_LEVEL = 4;
        constexpr static auto COUNT_MASK = 0xffffffff;
        constexpr static auto WAITER_SHIFT = 32;
        constexpr static uint16_t QUEUE_NUMBER = 16;
        static_assert(QUEUE_NUMBER > 0);
        static_assert(is_pow_of_2(QUEUE_NUMBER));
    public:
        struct co_wrap
        {
            Co_t * co{};
            std::function<void(Co_t *)> func{};

            co_wrap() = default;
            co_wrap(Co_t * co, const std::function<void(Co_t *)> & f) : co(co), func(f) {}

            bool operator > (const co_wrap & oth) const { return co > oth.co; }
            bool operator < (const co_wrap & oth) const { return co < oth.co; }
        };

        std::atomic<uint64_t> m_value{};
        uint32_t init_count{};
        std::atomic<int32_t> max_spin{MAX_SPIN};
        std::atomic<uint16_t> q_push_idx{};
        std::array<QuaternaryHeapLock<co_wrap>, QUEUE_NUMBER> wait_q{};
        std::atomic<uint16_t> q_pop_idx{};

#ifdef __MEM_PMR__
        std::pmr::synchronized_pool_resource * alloc{};
#else
        MemoryPool *alloc{};
#endif

        explicit Sem_t(uint32_t val) : m_value(val) { init_count = val; };

        Sem_t(const Sem_t &sem) = delete;

        void wait_impl(const std::function<void(Co_t *)> & callback);
        void wait();
        void wait_then(const std::function<void(Co_t *)> & callback);
        bool try_wait();
        void signal();
        static void release(Sem_t *ptr);
        co_wrap pick_from_wait_q();
        void inc_max_spin();
        void dec_max_spin();
        int64_t count();
        explicit operator int64_t();
        void operator delete(void *ptr) noexcept = delete;
    };
}