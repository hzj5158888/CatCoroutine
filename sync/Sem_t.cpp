//
// Created by hzj on 25-1-14.
//

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>

#include "../include/CoPrivate.h"
#include "../include/CoCtx.h"
#include "include/Sem_t.h"
#include "../sched/include/Scheduler.h"
#include "atomic_utils.h"

namespace co {
    void Sem_t::inc_max_spin()
    {
        auto inc_spin_fn = [](int cur_max_spin)
        {
            if ((cur_max_spin << 2) > MAX_SPIN / 2)
                return std::min(cur_max_spin + SPIN_LEVEL, MAX_SPIN);

            return cur_max_spin << 2;
        };
        atomic_fetch_modify(max_spin, inc_spin_fn, std::memory_order_relaxed);
    }

    void Sem_t::dec_max_spin()
    {
        auto dec_spin_fn = [](int cur_max_spin) { return std::max(cur_max_spin >> 1, MIN_SPIN); };
        atomic_fetch_modify(max_spin, dec_spin_fn, std::memory_order_relaxed);
    }

    void Sem_t::wait_impl(const std::function<void(Co_t *)> & callback)
    {
        int cur_max_spin = max_spin.load(std::memory_order_relaxed);
        uint64_t cur_val = m_value.load(std::memory_order_relaxed);
        int spin_end_idx = spin_wait(cur_max_spin, [this, &cur_val, cur_max_spin](int idx) -> bool
        {
            if (idx < cur_max_spin - 1)
            {
                if ((cur_val & COUNT_MASK) == 0)
                    return false;

                return m_value.compare_exchange_weak(cur_val, cur_val - 1);
            } else {
                /* last spin */
                auto dec_cnt_or_inc_waiter_fn = [](uint64_t cur_val) -> uint64_t
                {
                    if ((cur_val & COUNT_MASK) > 0)
                        return cur_val - 1;
                    else
                        return cur_val + (static_cast<uint64_t>(1) << WAITER_SHIFT);
                };
                auto val = atomic_fetch_modify(m_value, dec_cnt_or_inc_waiter_fn);
                return (val & COUNT_MASK) > 0;
            }
        });

        if (spin_end_idx != -1)
        {
            inc_max_spin();
            return;
        } else {
            dec_max_spin();
        }

        auto scheduler = co_ctx::loc->scheduler;
        auto co = scheduler->running_co;
        scheduler->interrupt(CO_WAITING);
        scheduler->remove_from_scheduler(co);
#ifdef __DEBUG__
        co->sem_ptr = this;
#endif
        auto idx = (q_push_idx++) % QUEUE_NUMBER;
        wait_q[idx].push(co_wrap{co, callback});
        /* goto scheduler */
        scheduler->jump_to_sched();
    }

    void Sem_t::wait()
    {
        wait_impl({});
    }

    void Sem_t::wait_then(const std::function<void(Co_t *)> & callback)
    {
        wait_impl(callback);
    }

    bool Sem_t::try_wait()
    {
        auto sub_count_fn = [](int64_t cur_value) -> int64_t
        {
            if ((cur_value & COUNT_MASK) == 0)
                return cur_value;

            return cur_value - 1;
        };
        auto cur_value = atomic_fetch_modify(m_value, sub_count_fn);
        return (cur_value & COUNT_MASK) > 0;
    }

    Sem_t::co_wrap Sem_t::pick_from_wait_q()
    {
        auto idx = (q_pop_idx++) % QUEUE_NUMBER;
        return wait_q[idx].wait_and_pop();
    }

    void Sem_t::signal()
    {
        auto inc_count_fn = [](uint64_t cur_value) -> uint64_t
        {
            if ((cur_value >> WAITER_SHIFT) == 0)
            {
                if (UNLIKELY((cur_value & COUNT_MASK) == UINT32_MAX))
                    throw SemOverflowException();

                return cur_value + 1;
            } else {
                return cur_value - (static_cast<uint64_t>(1) << WAITER_SHIFT);
            }
        };
        auto cur_val = atomic_fetch_modify(m_value, inc_count_fn);
        if ((cur_val >> WAITER_SHIFT) == 0)
            return;

        /* exec the callback and push to scheduler */
        auto cur_co = co_ctx::loc->scheduler->running_co;
        co_wrap wrap = pick_from_wait_q();
        if (wrap.func)
            wrap.func(cur_co);
#ifdef __DEBUG__
        wrap.co->sem_ptr = nullptr;
#endif
        co_ctx::manager->apply(wrap.co);
    }

    void Sem_t::release(Sem_t *sem)
    {
        int sum{};
        for (auto &q: sem->wait_q)
            sum += q.size();

        if (UNLIKELY(sem->count() != sem->init_count || sum != 0))
        {
            throw SemClosedException();
        }

        sem->~Sem_t();
        if (UNLIKELY(sem->alloc != nullptr))
#ifdef __MEM_PMR__
            sem->alloc->deallocate(sem, sizeof(Sem_t));
#else
            sem->alloc->deallocate(sem);
#endif
    }

    int64_t Sem_t::count()
    {
        auto value = m_value.load(std::memory_order_relaxed);
        int64_t count = value & COUNT_MASK;
        uint32_t waiter = value >> WAITER_SHIFT;
        return count - waiter;
    }

    Sem_t::operator int64_t()
    {
        return count();
    }

}