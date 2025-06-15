#include <memory>

#include "../include/CoCtx.h"
#include "./include/Timer.h"

namespace co {
    void Timer::push_to_timers(const TimerTaskPtr & task, bool enable_lock)
    {
        std::unique_lock<spin_lock_sleep> lock;
        if (enable_lock)
            lock = std::unique_lock(m_lock);

        m_task.insert(task);
    }

    std::vector<TimerTaskPtr> Timer::pick_all_expired(microseconds end_point)
    {
        std::vector<TimerTaskPtr> ans{};
        while (!m_task.empty())
        {
            auto top = *m_task.begin();
            if (top->need_to_handle(end_point))
            {
                m_task.erase(m_task.begin());
#ifdef __DEBUG_SEM_TRACE__
                top->sem_is_timeout = true;
#endif
                top->get_handled()->store(true, std::memory_order_relaxed);
                top->is_handling.lock();
                ans.push_back(top);
            } else {
                break;
            }
        }

        return ans;
    }

    void Timer::process_expired()
    {
        microseconds end_point = microseconds(co_ctx::clock.rdus());
        std::unique_lock lock(m_lock);
        if (m_task.empty() || !(*m_task.begin())->need_to_handle(end_point))
            return;

        std::vector<TimerTaskPtr> expired = pick_all_expired(end_point);
        lock.unlock();

        for (auto & task : expired)
        {
            task->callback(true);
            task->is_handling.unlock();
        }
    }

    void Timer::tick()
    {
        process_expired();
    }

    TimerTaskPtr Timer::create_task(const std::function<void(bool)> &callback)
    {
        auto ans = std::make_shared<TimerTask>(InvalidTime, InvalidTime, callback);
        return ans;
    }

    void Timer::apply_task_until(TimerTaskPtr & task, microseconds end_time, const apply_cb_t & callback)
    {
        microseconds start_time = microseconds(co_ctx::clock.rdus());
        microseconds duration = end_time - start_time;
        task->m_duration = duration;
        task->end_time = end_time;
        task->timer = this;

        std::lock_guard lock(m_lock);
        push_to_timers(task, false);
        if (callback)
            callback();
    }

    void Timer::apply_task(TimerTaskPtr &task, std::chrono::microseconds duration)
    {
        microseconds start_time = microseconds(co_ctx::clock.rdus());
        microseconds end_time = start_time + duration;
        task->m_duration = duration;
        task->end_time = end_time;
        task->timer = this;
        push_to_timers(task);
    }

    TimerTaskPtr Timer::add_task_until(std::chrono::microseconds end_time, const std::function<void(bool)> &callback)
    {
        auto task = create_task(callback);
        apply_task_until(task, end_time);
        return task;
    }

    TimerTaskPtr Timer::add_task(std::chrono::microseconds duration, const std::function<void(bool)> & callback)
    {
        auto task = create_task(callback);
        apply_task(task, duration);
        return task;
    }
}