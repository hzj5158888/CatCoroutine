#include "../include/CoCtx.h"
#include "./include/Timer.h"

namespace co {
    void TimerTask::push_to_timer()
    {
        if (timer == nullptr)
            timer = co_ctx::loc->timer.get();

        timer->push_to_timers(shared_from_this());
    }

    bool TimerTask::remove_from_timer()
    {
        if (timer == nullptr)
            return false;

        std::lock_guard lock{timer->m_lock};
        auto iter = timer->m_task.find(shared_from_this());
        if (iter == timer->m_task.end())
            return false;

        timer->m_task.erase(iter);
        timer = nullptr;
        return true;
    }

    bool TimerTask::cancel()
    {
        bool cur_handled = get_handled()->load(std::memory_order_acquire);
        bool cur_canceled = get_canceled()->load(std::memory_order_acquire);
        if (cur_canceled || cur_handled || !remove_from_timer())
            return cur_canceled;

        callback(false);
        get_canceled()->store(true);
        return true;
    }

    bool TimerTask::reset(std::chrono::microseconds duration, bool from_now)
    {
        bool cur_handled = get_handled()->load(std::memory_order_acquire);
        bool cur_canceled = get_canceled()->load(std::memory_order_acquire);
        if (cur_canceled || cur_handled || !remove_from_timer())
            return false;

        microseconds start{};
        if (from_now)
            start = microseconds(co_ctx::clock.rdus());
        else
            start = end_time - m_duration;

        end_time = start + duration;
        m_duration = duration;
        push_to_timer();
        return true;
    }

    bool TimerTask::reset_until(std::chrono::microseconds until)
    {
        bool cur_handled = get_handled()->load(std::memory_order_acquire);
        bool cur_canceled = get_canceled()->load(std::memory_order_acquire);
        if (cur_canceled || cur_handled || !remove_from_timer())
            return false;

        microseconds start = microseconds(co_ctx::clock.rdus());
        end_time = until;
        m_duration = until - start;
        push_to_timer();
        return true;
    }

    bool TimerTask::reset_until(std::chrono::microseconds until, const std::function<void(bool)> & f)
    {
        bool ans = reset_until(until);
        if (!ans)
            return false;

        callback = f;
        return true;
    }

    bool TimerTask::refresh()
    {
        return reset(m_duration, true);
    }

    bool TimerTask::need_to_handle(std::chrono::microseconds now) const
    {
        return now >= end_time;
    }

    void TimerTask::swap(TimerTask && oth) noexcept
    {
        std::swap(callback, oth.callback);
        std::swap(end_time, oth.end_time);
        std::swap(m_duration, oth.m_duration);
        std::swap(is_canceled, oth.is_canceled);
        std::swap(is_handled, oth.is_handled);
        std::swap(timer, oth.timer);
    }
}