#pragma once

#include <chrono>
#include <functional>

#include "TimerDef.h"
#include "../../allocator/include/MemPoolAllocator.h"
#include "../../utils/include/spin_lock.h"
#include "../../utils/include/spin_lock_sleep.h"
#include "../../data_structure/include/QuaternaryHeap.h"

namespace co {
    using std::chrono::microseconds;
    using std::chrono::milliseconds;

    class TimerTask : public std::enable_shared_from_this<TimerTask>
    {
    private:
        constexpr static microseconds InvalidTime = microseconds(0);

        friend class Timer;
        using lock_t = spin_lock;
    public:
        lock_t m_lock{};
        bool is_canceled{}, is_handled{};
        microseconds end_time{};
        microseconds m_duration{};
        microseconds new_end_time{};
        std::function<void()> callback{};
        Timer * timer{};

        TimerTask(microseconds duration, microseconds end_time, const std::function<void()> & callback)
        {
            this->m_duration = duration;
            this->end_time = end_time;
            this->callback = callback;
        }

        TimerTask(TimerTask && oth) noexcept { swap(std::move(oth)); }

        void swap(TimerTask && oth) noexcept;

        bool cancel();

        bool reset(microseconds duration, bool from_now);

        bool reset_until(microseconds until);

        bool refresh();

        bool need_to_handle(microseconds now, bool need_lock);

        bool operator > (const TimerTask & oth) const { return end_time > oth.end_time; }
        bool operator < (const TimerTask & oth) const { return end_time < oth.end_time; }

        struct Comparator
        {
            bool operator () (const std::shared_ptr<TimerTask> & a, const std::shared_ptr<TimerTask> & b) const
            {
                if (a && b)
                    return (*a) < (*b);
                else if (!a)
                    return true;
                else
                    return false;
            }
        };
    };

    class Timer
    {
    public:
        constexpr static microseconds TickInterval = microseconds(1500);

        using TaskPtr = std::shared_ptr<TimerTask>;
        using lock_t = typename spin_rw_lock_sleep::locker;
        using reader_lock_t = typename spin_rw_lock_sleep::reader;
        using writer_lock_t = typename spin_rw_lock_sleep::writer;

        lock_t m_lock{};
        QuaternaryHeap<TaskPtr, TimerTask::Comparator> m_task{};

        void tick();
        void process_expired();
        void push_to_timers(const TaskPtr &);
        TaskPtr add_task(microseconds duration, const std::function<void()> & callback);
        TaskPtr add_task_until(microseconds end_time, const std::function<void()> & callback);
    };
}