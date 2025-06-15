//
// Created by hzj on 25-1-11.
//

#pragma once

#include <cstdint>
#include <mutex>
#include <cstddef>
#include <vector>
#include <exception>
#include <vector>

#include "spin_lock.h"
#include "../../utils/include/spin_lock_sleep.h"
#include "../../utils/include/sem_utils.h"
#include "../../include/CoPrivate.h"
#include "../../data_structure/include/QuaternaryHeap.h"
#include "../../data_structure/include/BinaryHeap.h"
#ifdef __SCHED_FIFO__
#include "../../data_structure/include/QueueLockFree.h"
#include "../../allocator/include/MemPoolAllocator.h"
#endif
#include "../../data_structure/include/RingQueue.h"

namespace co {
    class ApplyRunningCoException : public std::exception
    {
    public:
        [[nodiscard]] const char *what() const noexcept override { return "Apply Ready Coroutine Exception"; }
    };

#ifdef __SCHED_CFS__
    struct sort_wrap
    {
        uint64_t priority{};
        Co_t * co{};
    };

    struct CoPtrLessCmp
    {
        bool operator()(const Co_t *a, const Co_t *b) const
        {
            if (a && b)
            {
                return a->sched.priority() < b->sched.priority();
            } else if (a == nullptr)
            {
                return false;
            } else {
                return true;
            }
        }
    };

    struct SortWrapLessCmp
    {
        bool operator() (const sort_wrap & a, const sort_wrap & b) const
        {
            /* a.priority - b.priority */
            /* a.priority & b.priority < 2^63 */
            return (((a.priority - b.priority) >> (sizeof(uint64_t) - 1)) & 1);
        }
    };
#endif

    enum {
        APPLY_EAGER = 1,
        APPLY_LAZY,
        APPLY_NORMAL
    };

    class alignas(__CACHE_LINE__) Scheduler
    {
    public:
        int this_thread_id{};
        //std::atomic<uint64_t> sum_v_runtime{};
        size_t ready_count{};
        counting_semaphore sem_ready{};
        Context sched_ctx{};

#ifdef __SCHED_CFS__
        constexpr static auto MAX_BACKOFF = 12;
        using spin_lock_t = spin_lock_sleep;
        /* lockness data structure */
        alignas(__CACHE_LINE__) spin_lock_t sched_lock{};
        QuaternaryHeap<sort_wrap, SortWrapLessCmp> ready{};
        QuaternaryHeap<sort_wrap, SortWrapLessCmp> ready_fixed{};
#else
        static_assert(false);
#endif

        std::atomic<uint64_t> min_v_runtime{};
        int latest_arg{};
        Co_t * running_co{};
        Co_t * await_callee{};

        struct SleepArgs
        {
            std::chrono::microseconds sleep_duration{};
            std::chrono::microseconds sleep_until{};
        } sleep_args;

        constexpr static auto APPLY_BUFFER_SIZE = 128;
        boost::lockfree::queue<Co_t *, boost::lockfree::capacity<APPLY_BUFFER_SIZE>> buffer{};

        std::vector<Co_t *> pull_from_buffer();

        void pull_from_buffer(std::vector<Co_t *> &);

        void push_all_to_ready(const std::vector<sort_wrap> &, const std::vector<sort_wrap> &, bool);

        void push_to_ready(Co_t *co, bool);

        void get_ready_to_push(Co_t *co, uint64_t &);

        void apply_ready(Co_t *co);

        void apply_ready_lazy(Co_t *co);

        void apply_ready_eager(Co_t *co, bool from_buffer = false);

        void apply_ready_all(
                const std::vector<Co_t *> &co_vec,
                std::unique_lock<spin_lock_t> & locker,
                bool from_buffer = false,
                bool enable_lock = true,
                bool unlock_exit = true
        );

        void remove_from_scheduler(Co_t *co);

        [[nodiscard]] Co_t *pickup_ready();

        void run(Co_t *co);

        void coroutine_yield();

        void coroutine_await();

        void coroutine_dead();

        void coroutine_sleep();

        void jump_to_sched(int arg = CONTEXT_RESTORE);

        Co_t *interrupt(int new_status = CO_INTERRUPT, bool unlock_exit = true);

        [[noreturn]] void start();

        void process_callback(int arg);

        void process_co_exec_end(Co_t * co);

        void pull_half_co(std::vector<Co_t *> &);

        uint64_t get_load();
    };

    class SchedManager {
    public:
        constexpr static uint64_t INF = 0x3f3f3f3f3f3f3f3f;

        /* for init */
        std::atomic<size_t> scheduler_count{};

        std::vector<Scheduler *> schedulers{};
        spin_lock init_lock{};
        spin_lock w_lock{};

        explicit SchedManager(int thread_count) { schedulers.resize(thread_count); }

        void apply_impl(Co_t *co, int flag);

        void apply(Co_t *co);

        void apply_eager(Co_t *co);

        void apply_lazy(Co_t *co);

        void wakeup_await_co_all(Co_t *await_callee);

        std::vector<Co_t *> stealing_work(int);

        void stealing_work(int, std::vector<Co_t *> &);

        void add_scheduler(Scheduler *s, int idx);
    };
}
