//
// Created by hzj on 25-1-14.
//

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>
#include <bitset>

#include "../include/CoPrivate.h"
#include "../include/CoCtx.h"
#include "include/Sem_t.h"
#include "../sched/include/Scheduler.h"
#include "atomic_utils.h"
#include "../timer/include/Timer.h"

namespace co {
    Sem_t::Sem_t(uint32_t val)
    {
        m_value = val;
        init_count = val;
        if constexpr (std::is_same_v<wait_q_t, LockedContainer<co_wrap, RandomStack<co_wrap>>>)
        {
            for (auto & q : wait_q)
                q = wait_q_t((uint64_t) co_ctx::clock.rdns());
        }
    }

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

    void Sem_t::wait_impl(const callback_t & callback)
    {
#ifdef __DEBUG_SEM_TRACE__
        co_ctx::loc->scheduler->running_co->sem_wakeup_reason.emplace_back("enter wait timed");
#endif
        int cur_max_spin = max_spin.load(std::memory_order_relaxed);
        uint64_t cur_val = m_value.load(std::memory_order_relaxed);
        int spin_end_idx = spin_wait(cur_max_spin, [this, &cur_val, cur_max_spin](int idx) -> bool
        {
            if (idx < cur_max_spin - 1)
            {
                if ((cur_val & COUNT_MASK) == 0)
                {
                    cur_val = m_value.load(std::memory_order_relaxed);
                    return false;
                } else {
                    return m_value.compare_exchange_weak(cur_val, cur_val - 1);
                }
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
#ifdef __DEBUG_SEM_TRACE__
        co->sem_ptr = this;
#endif
        auto idx = q_push_idx++;
        wait_q[idx % QUEUE_NUMBER].push(co_wrap{co, callback});
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

    bool Sem_t::wait_timed_impl(std::chrono::microseconds end_time, const callback_t & callback)
    {
#ifdef __DEBUG_SEM_TRACE__
        co_ctx::loc->scheduler->running_co->sem_wakeup_reason.emplace_back("enter wait timed");
#endif
        int cur_max_spin = max_spin.load(std::memory_order_relaxed);
        uint64_t cur_val = m_value.load(std::memory_order_relaxed);
        int spin_end_idx = spin_wait(cur_max_spin, [this, &cur_val, cur_max_spin](int idx) -> bool
        {
            if (idx < cur_max_spin - 1)
            {
                if ((cur_val & COUNT_MASK) == 0)
                {
                    cur_val = m_value.load(std::memory_order_relaxed);
                    return false;
                }

                return m_value.compare_exchange_weak(cur_val, cur_val - 1);
            } else {
                /* last spin */
                auto dec_cnt_or_inc_waiter_fn = [](uint64_t cur_val) -> uint64_t
                {
                    if ((cur_val & COUNT_MASK) > 0)
                        return cur_val - 1;
                    else {
                        if (UNLIKELY((cur_val >> WAITER_SHIFT) == WAITER_MAX))
                            throw SemWaiterOverflowException();

                        return cur_val + (static_cast<uint64_t>(1) << WAITER_SHIFT);
                    }
                };
                auto val = atomic_fetch_modify(m_value, dec_cnt_or_inc_waiter_fn);
                return (val & COUNT_MASK) > 0;
            }
        });

        if (spin_end_idx != -1)
        {
#ifdef __DEBUG_SEM_TRACE__
            co_ctx::loc->scheduler->running_co->sem_wakeup_reason.emplace_back("by spin waiting");
#endif
            inc_max_spin();
            return true;
        }

        dec_max_spin();

        auto scheduler = co_ctx::loc->scheduler;
        auto co = scheduler->running_co;
        scheduler->interrupt(CO_WAITING);
        scheduler->remove_from_scheduler(co);
#ifdef __DEBUG_SEM_TRACE__
        co->sem_ptr = this;
#endif
        /* 注册定时器 */
        bool ans{};
        auto timer_callback_fn = [&ans, this, co](bool is_timeout)
        {
            ans = !is_timeout;
            if (!is_timeout)
            {
#ifdef __DEBUG_SEM_TRACE__
                co->sem_wakeup_reason.emplace_back("not timeout");
#endif
                return;
            }

#ifdef __DEBUG_SEM_TRACE__
            cb_args.co->sem_ptr = nullptr;
            cb_args.co->sem_wakeup_reason.emplace_back("timeout fn");
#endif
            signal(false);
            co_ctx::manager->apply(co);
        };
        auto wrap = co_wrap{co, callback};
        wrap.timerTask = co_ctx::loc->timer->create_task([&timer_callback_fn](bool is_timeout)
        {
            timer_callback_fn(is_timeout);
        });
#ifdef __DEBUG_SEM_TRACE__
        wrap.co->cur_task = wrap.timerTask;
#endif
        /* 加入等待队列 */
        co_ctx::loc->timer->apply_task_until(wrap.timerTask, end_time, [this, &wrap]()
        {
            cancelable_wait.insert(wrap);
        });
#ifdef __DEBUG__
        m_size++;
#endif
        /* goto scheduler */
        scheduler->jump_to_sched();
        return ans;
    }

    bool Sem_t::wait_for(std::chrono::microseconds duration)
    {
        if (duration <= microseconds(0))
            return false;

        auto now = std::chrono::microseconds(co_ctx::clock.rdus());
        return wait_timed_impl(now + duration, {});
    }

    bool Sem_t::wait_for_then(std::chrono::microseconds duration, const callback_t & f)
    {
        auto now = std::chrono::microseconds(co_ctx::clock.rdus());
        return wait_timed_impl(now + duration, f);
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

    std::optional<Sem_t::co_wrap> Sem_t::pick_from_wait_q()
    {
        auto wrap_opt = cancelable_wait.try_pop();
        if (wrap_opt.has_value())
            return wrap_opt;

        auto idx = (q_pop_idx++) % QUEUE_NUMBER;
        auto wrap = wait_q[idx].wait_and_pop();
#ifdef __DEBUG__
        m_size--;
#endif
        return wrap;
    }

    void Sem_t::signal(bool call_func)
    {
        auto inc_count_fn = [](uint64_t cur_value) -> uint64_t
        {
            if ((cur_value >> WAITER_SHIFT) == 0)
            {
                if (UNLIKELY((cur_value & COUNT_MASK) == COUNT_MAX))
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
        std::optional<co_wrap> wrap_opt{};
        while (true)
        {
            wrap_opt = pick_from_wait_q();
            auto & wrap = wrap_opt.value();
            /* handle timeout */
            if (wrap.timerTask && !wrap.timerTask->cancel())
            {
                return;
            } else {
                break;
            }
        }
        auto & wrap = wrap_opt.value();
#ifdef __DEBUG_SEM_TRACE__
        wrap.co->sem_ptr = nullptr;
        wrap.co->sem_wakeup_reason.emplace_back("signal");
#endif
        if (call_func && wrap.func)
            wrap.func(cur_co);

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