#include "../include/CoCtx.h"
#include "./include/Timer.h"

namespace co {
    bool TimerTask::cancel()
    {
        std::lock_guard lock(m_lock);
        is_canceled = true;
        return is_handled;
    }

    bool TimerTask::reset(std::chrono::microseconds duration, bool from_now)
    {
        std::lock_guard lock(m_lock);
        if (is_canceled)
            return false;

        microseconds start{};
        if (from_now)
            start = microseconds(co_ctx::loc->clock.rdus());
        else
            start = end_time - m_duration;

        if (timer == nullptr)
        {
            end_time = start + duration;
            timer = co_ctx::loc->timer.get();
            timer->push_to_timers(shared_from_this());
        } else {
            new_end_time = start + duration;
        }

        is_handled = false;
        m_duration = duration;
        return true;
    }

    bool TimerTask::reset_until(std::chrono::microseconds until)
    {
        std::lock_guard lock(m_lock);
        if (is_canceled)
            return false;

        microseconds start = microseconds(co_ctx::loc->clock.rdus());
        if (timer == nullptr)
        {
            end_time = until;
            timer = co_ctx::loc->timer.get();
            timer->push_to_timers(shared_from_this());
        } else {
            new_end_time = until;
        }

        is_handled = false;
        m_duration = until - start;
        return true;
    }

    bool TimerTask::refresh()
    {
        return reset(m_duration, true);
    }

    bool TimerTask::need_to_handle(std::chrono::microseconds now, bool need_lock)
    {
        std::unique_lock<spin_lock> lock{};
        if (need_lock)
            lock = std::unique_lock(m_lock);

        return is_canceled || now >= end_time || new_end_time != InvalidTime;
    }

    void TimerTask::swap(TimerTask && oth) noexcept
    {
        std::swap(m_lock, oth.m_lock);
        std::swap(callback, oth.callback);
        std::swap(end_time, oth.end_time);
        std::swap(m_duration, oth.m_duration);
        std::swap(is_canceled, oth.is_canceled);
        std::swap(timer, oth.timer);
    }
}