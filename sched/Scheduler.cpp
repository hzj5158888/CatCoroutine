#include <atomic>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <vector>

#include "Scheduler.h"
#include "../utils/include/co_utils.h"
#include "../utils/include/atomic_utils.h"
#include "CoPrivate.h"
#include "CoCtx.h"
#include "Context.h"
#include "utils.h"
#include "../timer/include/Timer.h"

namespace co {
    void Scheduler::push_to_ready(Co_t * co, bool enable_lock)
    {
        std::unique_lock<spin_lock_t> lock;
        if (enable_lock)
            lock = std::unique_lock(sched_lock);

        if (co->sched.can_migration)
            ready.push(sort_wrap{co->sched.priority(), co});
        else
            ready_fixed.push(sort_wrap{co->sched.priority(), co});

#ifdef __DEBUG_SCHED__
        co_ctx::removal_lock.lock();
        co_ctx::co_vec.insert(co);
        co_ctx::removal_lock.unlock();
#endif

        ready_count++;
    }

    void Scheduler::push_all_to_ready(const std::vector<sort_wrap> & co_vec, const std::vector<sort_wrap> & fixed_co, bool enable_lock)
    {
        std::unique_lock<spin_lock_t> lock;
        if (enable_lock)
            lock = std::unique_lock(sched_lock);

        ready.push_all(co_vec.begin(), co_vec.end());
        ready_fixed.push_all(fixed_co.begin(), fixed_co.end());
        ready_count += co_vec.size() + fixed_co.size();
    }

    void Scheduler::pull_half_co(std::vector<Co_t*> & ans)
    {
        if (ready.empty())
            return;

        std::lock_guard lock(sched_lock);
        /* double check */
        if (ready.empty())
            return;

        int trans_size = 0;
        int pull_count = ready.size() / 2;
        for (; trans_size < pull_count; trans_size++)
        {
            if (!sem_ready.try_wait())
                break;

            ans.push_back(ready.pop_back().co);

#ifdef __DEBUG_SCHED__
            co_ctx::removal_lock.lock();
            co_ctx::co_vec.erase(ans.back());
            co_ctx::removal_lock.unlock();
#endif
            remove_from_scheduler(ans.back());
            ans.back()->sched.occupy_thread = -1;
        }

        ready_count -= trans_size;
    }

    void Scheduler::pull_from_buffer(std::vector<Co_t*> & ans)
    {
        Co_t * cur{};
        for (int i = 0; i < APPLY_BUFFER_SIZE && buffer.pop(cur); i++)
        {
            assert(cur != nullptr);
            ans.push_back(cur);
        }
    }

    std::vector<Co_t*> Scheduler::pull_from_buffer()
    {
        std::vector<Co_t*> ans{};
        ans.reserve(APPLY_BUFFER_SIZE);
        pull_from_buffer(ans);
        return ans;
    }

    void Scheduler::apply_ready(Co_t * co)
    {
        uint64_t cur_sum_v_runtime{};
        get_ready_to_push(co, cur_sum_v_runtime);
        if (sched_lock.try_lock_for_backoff(MAX_BACKOFF))
        {
            push_to_ready(co, false);
            sched_lock.unlock();
            sem_ready.signal();
        } else {
            apply_ready_lazy(co);
        }
    }

    void Scheduler::apply_ready_lazy(Co_t * co)
    {
        if (UNLIKELY(!buffer.push(co)))
        {
            auto cur_sched_lock = std::unique_lock(sched_lock, std::defer_lock);
            auto buf_co = pull_from_buffer();
            buf_co.push_back(co);
            apply_ready_all(buf_co, cur_sched_lock, true);
        }
        sem_ready.signal();
    }

    void Scheduler::get_ready_to_push(Co_t * co, uint64_t & sum_v_runtime)
    {
#ifdef __SCHED_CFS__
        if (co->sched.v_runtime == 0)
            co->sched.v_runtime = min_v_runtime.load(std::memory_order_relaxed);
#endif

#ifdef __DEBUG__
        if (!co->sched.can_migration)
            DASSERT(co->sched.occupy_thread == -1 || co->sched.occupy_thread == this_thread_id);
#endif
        /* handle wakeup_reason */
        switch (co->wakeup_reason)
        {
            case CO_WAKEUP_TIMER:
                co->sched.up_nice(PRIORITY_HIGHEST);
                break;
            default:
                break;
        }

        co->status_lock.lock();
        co->status = CO_READY;
        co->status_lock.unlock();
        co->scheduler = this;
    }

    void Scheduler::apply_ready_eager(Co_t * co, bool from_buffer)
    {
        uint64_t cur_sum_v_runtime{};
        get_ready_to_push(co, cur_sum_v_runtime);
        push_to_ready(co, true);
        if (!from_buffer)
            sem_ready.signal();
    }

    void Scheduler::apply_ready_all(
            const std::vector<Co_t *> & co_vec,
            std::unique_lock<spin_lock_t> & locker,
            bool from_buffer,
            bool enable_lock,
            bool unlock_exit)
    {
        if (co_vec.empty())
            return;

#ifdef __SCHED_CFS__
        uint64_t cur_sum_v_runtime = 0;
#endif
        for (auto co : co_vec)
            get_ready_to_push(co, cur_sum_v_runtime);

        std::vector<sort_wrap> fixed_co{};
        std::vector<sort_wrap> unfixed_co{};
        fixed_co.reserve(co_vec.size() / 2);
        unfixed_co.reserve(co_vec.size() / 2);
        for (auto co : co_vec)
        {
            if (co->sched.can_migration)
                unfixed_co.push_back(sort_wrap{co->sched.priority(), co});
            else
                fixed_co.push_back(sort_wrap{co->sched.priority(), co});
        }

        if (enable_lock)
        {
            locker.lock();
            push_all_to_ready(unfixed_co, fixed_co, false);
            if (unlock_exit)
                locker.unlock();
        } else {
            push_all_to_ready(unfixed_co, fixed_co, false);
        }

        if (!from_buffer)
            sem_ready.signal(co_vec.size());
    }

    void Scheduler::remove_from_scheduler(Co_t * co)
    {
        co->scheduler = nullptr;
    }

    Co_t * Scheduler::pickup_ready()
    {
        std::unique_lock cur_sched_lock = std::unique_lock(sched_lock, std::defer_lock);
        if (!sem_ready.try_wait())
        {
            auto res = co_ctx::manager->stealing_work(this_thread_id);
            apply_ready_all(res, cur_sched_lock, false, true, false);
            if (UNLIKELY(!sem_ready.try_wait()))
            {
                if (cur_sched_lock.owns_lock())
                    cur_sched_lock.unlock();

                sem_ready.wait();
            } else {
                goto end_pull_from_buffer;
            }
        }

        /* pull coroutines from buffer */
        {
            if (atomization(ready_count)->load(std::memory_order_relaxed) == 0)
            {
                cur_sched_lock.lock();
                if (ready_count > 0)
                    goto end_pull_from_buffer;
                else
                    cur_sched_lock.unlock();
            }

            std::vector<Co_t*> res{};
            while (atomization(ready_count)->load(std::memory_order_relaxed) == 0)
            {
                pull_from_buffer(res);
                if (res.empty())
                    continue;

                apply_ready_all(res, cur_sched_lock, true, true, false);
                break;
            }
        }

        /* label: end_pull_from_buffer */
        end_pull_from_buffer:
#ifdef __DEBUG__
        if (!cur_sched_lock.owns_lock())
            cur_sched_lock.lock();

        DASSERT(!(ready.empty() && ready_fixed.empty()));
#endif

#ifdef __SCHED_CFS__
        if (!cur_sched_lock.owns_lock())
            cur_sched_lock.lock();

        ready_count--;

        Co_t * ans{};
        Co_t * ans_list[2]{nullptr, nullptr};
        if (!ready.empty())
            ans_list[0] = ready.top().co;
        if (!ready_fixed.empty())
            ans_list[1] = ready_fixed.top().co;

        assert(UNLIKELY(ans_list[0] || ans_list[1]));

        if (CoPtrLessCmp{}(ans_list[0], ans_list[1]))
        {
            ans = ans_list[0];
            ready.pop();
        } else {
            ans = ans_list[1];
            ready_fixed.pop();
        }

        auto up_v_runtime_fn = [this](uint64_t cur_v_time) -> uint64_t
        {
            if (!ready.empty())
            {
                auto top_v_time = ready.top().priority;
                cur_v_time = cur_v_time > 0 ? std::min(cur_v_time, top_v_time) : top_v_time;
            }
            if (!ready_fixed.empty())
            {
                auto top_v_time = ready_fixed.top().priority;
                cur_v_time = cur_v_time > 0 ? std::min(cur_v_time, top_v_time) : top_v_time;
            }

            return cur_v_time;
        };
        atomic_fetch_modify(min_v_runtime, up_v_runtime_fn, std::memory_order_acq_rel);

        cur_sched_lock.unlock();
#else
        static_assert(false);
#endif

        DASSERT(ans->status == CO_READY);
#ifdef __DEBUG_SCHED_READY__
        co_ctx::removal_lock.cur_sched_lock();
        co_ctx::co_vec.erase(ans);
        co_ctx::removal_lock.unlock();
#endif
        return ans;
    }

    void Scheduler::run(Co_t * co)
    {
        assert(UNLIKELY(co->status == CO_READY));

        /* READY => RUNNING 不需要对status_lock加锁 */
        co->stk_active_lock.lock();
        if (LIKELY(!co->is_main_co)) // 设置 非main coroutine 栈帧
        {
#ifdef __STACK_STATIC__
            /* 设置stack分配器 */
            if (UNLIKELY(co->co_ctx.static_stk_pool == nullptr))
                co->co_ctx.static_stk_pool = &co_ctx::loc->alloc.stk_pool;

            /* 分配堆栈 */
            co->co_ctx.static_stk_pool->alloc_static_stk(co);
            if (UNLIKELY(co->co_ctx.first_full_save))
                make_context_wrap(&co->co_ctx, &co::wrap);
#elif __STACK_DYN__
            if (UNLIKELY(co->ctx.stk_dyn_alloc == nullptr))
            {
                co_ctx::loc->alloc.dyn_stk_pool.alloc_stk(&co->ctx);
                make_context_wrap(&co->ctx, &co::wrap);
            }
#endif
        }

#ifdef __DEBUG_SCHED_RUN__
        co_ctx::removal_lock.lock();
        DASSERT(co_ctx::running_co.count(co) == 0);
        co_ctx::running_co.insert(co);
        co_ctx::removal_lock.unlock();
#endif
        running_co = co;
        co->status = CO_RUNNING;
        co->sched.start_exec();
        latest_arg = swap_context(&sched_ctx, &co->ctx);
    }

    /* Coroutine 中断执行 */
    Co_t * Scheduler::interrupt(int new_status, bool unlock_exit)
    {
        DASSERT(running_co != nullptr);
        running_co->status_lock.lock();
        /* update the status */
        running_co->status = new_status;
        running_co->sched.end_exec();
#ifdef __SCHED_CFS__
        /* update running time */
        running_co->sched.up_v_runtime();
#endif
        /* whether unlock when exit */
        if (unlock_exit)
            running_co->status_lock.unlock();

        return running_co;
    }

    uint64_t Scheduler::get_load()
    {
        uint64_t load{};
        load += atomization(ready_count)->load(std::memory_order_relaxed);
        return load;
    }

    void Scheduler::coroutine_yield()
    {
        /* interrupt coroutine */
        auto yield_co = interrupt(CO_INTERRUPT, false);
        remove_from_scheduler(yield_co);

        /* yield finish */
        yield_co->status_lock.unlock();

        /* apply to scheduler */
        co_ctx::manager->apply(yield_co);
    }

    void Scheduler::coroutine_await()
    {
        DASSERT(await_callee != nullptr);
        DASSERT(await_callee->status != CO_DEAD);

        auto caller = interrupt(CO_WAITING, false);
        await_callee->await_caller_lock.lock();
        await_callee->await_caller.push_back(caller);
        await_callee->await_caller_lock.unlock();
        caller->await_callee = await_callee;
        /* release caller */
        caller->status_lock.unlock();
        /* release callee */
        await_callee->status_lock.unlock();
        await_callee = nullptr;
    }

    void Scheduler::coroutine_dead()
    {
        auto dead_co = interrupt(CO_DEAD, false);
        DASSERT(dead_co != nullptr);

        /* wakeup waiting coroutine */
        co_ctx::manager->wakeup_await_co_all(dead_co);

        /* 释放栈空间 */
#ifdef __STACK_DYN__
        if (LIKELY(dead_co->ctx.stk_dyn_mem != nullptr))
        {
            auto stk_pool = dead_co->ctx.stk_dyn_alloc;
            stk_pool->free_stk(&dead_co->ctx);
        }
#elif __STACK_STATIC__
        if (!dead_co->is_main_co) [[likely]]
        {
            auto stk_pool = dead_co->co_ctx.static_stk_pool;
            stk_pool->destroy_stack(dead_co);
        }
#endif

#ifdef __DEBUG_SCHED_READY__
        co_ctx::removal_lock.lock();
        co_ctx::co_vec.erase(dead_co);
        co_ctx::removal_lock.unlock();
#endif

        dead_co->status = CO_DEAD;
        dead_co->status_lock.unlock();
    }

    void Scheduler::coroutine_sleep()
    {
        if (sleep_args.sleep_duration == Timer::InvalidTime && sleep_args.sleep_until == Timer::InvalidTime)
            return;

        auto cur_co = interrupt(CO_WAITING, true);
        remove_from_scheduler(cur_co);
        auto wakeup_callback = [cur_co](bool is_timeout)
        {
            if (!is_timeout)
                return;

            DEXPR(std::cout << "wakeup_callback: " << cur_co << std::endl;)
            cur_co->wakeup_reason = CO_WAKEUP_TIMER;
            co_ctx::manager->apply(cur_co);
        };

        if (sleep_args.sleep_duration != Timer::InvalidTime)
            co_ctx::loc->timer->add_task(sleep_args.sleep_duration, wakeup_callback);
        else if (sleep_args.sleep_until != Timer::InvalidTime)
            co_ctx::loc->timer->add_task_until(sleep_args.sleep_until, wakeup_callback);

        sleep_args = SleepArgs{};
    };

    void Scheduler::process_callback(int arg)
    {
        switch (arg)
        {
        case CALL_AWAIT:
            coroutine_await();
            break;
        case CALL_YIELD:
            coroutine_yield();
            break;
        case CALL_DEAD:
            coroutine_dead();
            break;
        case CALL_SLEEP:
            coroutine_sleep();
            break;
        case CONTEXT_RESTORE:
            break;
        default:
            assert(false && "unknown arg");
        }
    }

    void Scheduler::process_co_exec_end(Co_t * co)
    {
#ifdef __STACK_STATIC__
        co->co_ctx.set_stk_size();

            /* release static stack */
            if (LIKELY(co->co_ctx.static_stk_pool != nullptr))
                co->co_ctx.static_stk_pool->release_stack(co);
#else
        co->ctx.set_stk_dyn_size();
#endif
#ifdef __DEBUG_SCHED_RUN__
        co_ctx::removal_lock.lock();
        co_ctx::running_co.erase(co);
        co_ctx::removal_lock.unlock();
#endif
        /* handle wakeup_reason */
        switch (co->wakeup_reason)
        {
            case CO_WAKEUP_TIMER:
                co->sched.back_nice();
                co->wakeup_reason = CO_WAKEUP_NONE;
                break;
            default:
                break;
        }

        /* finish run */
        co->stk_active_lock.unlock();
    }

    [[noreturn]] void Scheduler::start()
    {
        while (true)
        {
            int res = latest_arg;
            if (res != CONTEXT_CONTINUE)
            {
                process_callback(res);
                latest_arg = CONTEXT_CONTINUE; // clear arg
                continue;
            }

            auto co = pickup_ready();
            run(co);
            process_co_exec_end(co);
        }
    }

    void Scheduler::jump_to_sched(int arg)
    {
        auto running = running_co;

        if (LIKELY(running != nullptr))
            swap_context(&running->ctx, &sched_ctx, arg);
        else
            swap_context(nullptr, &sched_ctx, arg);
    }
}