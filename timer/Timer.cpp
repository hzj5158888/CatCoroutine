#include <memory>

#include "../include/CoCtx.h"
#include "./include/Timer.h"

namespace co {
    void Timer::push_to_timers(const TaskPtr & task)
    {
        std::lock_guard<writer_lock_t> lock(m_lock.get_writer());
        m_task.push(task);
    }

    void Timer::process_expired()
    {
        microseconds end_point = microseconds(co_ctx::loc->clock.rdus());
        {
            std::lock_guard<reader_lock_t> lock(m_lock.get_reader());
            if (m_task.empty() || !m_task.top()->need_to_handle(end_point, true))
                return;
        }

        std::lock_guard<writer_lock_t> lock(m_lock.get_writer());
        if (m_task.empty())
            return;

        while (!m_task.empty())
        {
            auto top = m_task.top();
            std::lock_guard task_lock(top->m_lock);
            if (top->need_to_handle(end_point, false))
            {
                if (top->new_end_time != TimerTask::InvalidTime)
                {
                    top->end_time = top->new_end_time;
                    top->new_end_time = TimerTask::InvalidTime;
                    m_task.replace_top(top);
                } else {
                    m_task.pop();
                    if (!top->is_canceled)
                        top->callback();

                    top->timer = nullptr;
                }

                top->is_handled = true;
            } else {
                break;
            }

            end_point = microseconds(co_ctx::loc->clock.rdus());
        }
    }

    void Timer::tick()
    {
        process_expired();
    }

    Timer::TaskPtr Timer::add_task_until(std::chrono::microseconds end_time, const std::function<void()> &callback)
    {
        microseconds start_time = microseconds(co_ctx::loc->clock.rdus());
        microseconds duration = end_time - start_time;
        auto ans = std::make_shared<TimerTask>(duration, end_time, callback);
        ans->timer = this;
        push_to_timers(ans);
        return ans;
    }

    Timer::TaskPtr Timer::add_task(std::chrono::microseconds duration, const std::function<void()> & callback)
    {
        microseconds start_time = microseconds(co_ctx::loc->clock.rdus());
        microseconds end_time = start_time + duration;
        auto ans = std::make_shared<TimerTask>(duration, end_time, callback);
        ans->timer = this;
        push_to_timers(ans);
        return ans;
    }
}