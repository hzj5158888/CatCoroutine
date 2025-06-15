#pragma once

#include <chrono>
#include <functional>
#include <set>

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
        friend class Timer;

    public:
        bool is_canceled{}, is_handled{};
        microseconds end_time{};
        microseconds m_duration{};
        std::function<void(bool)> callback{};
        Timer * timer{};
        spin_lock is_handling{};
#ifdef __DEBUG_SEM_TRACE__
        std::string sem_wakeup_reason{};
        bool sem_is_timeout{};
#endif

        std::atomic<bool> * get_canceled() { return reinterpret_cast<std::atomic<bool>*>(&is_canceled); }

        std::atomic<bool> * get_handled() { return reinterpret_cast<std::atomic<bool>*>(&is_handled); }

        TimerTask(microseconds duration, microseconds end_time, const std::function<void(bool)> & callback)
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

        bool reset_until(microseconds until, const std::function<void(bool)> & f);

        bool refresh();

        bool need_to_handle(microseconds now) const;

        bool remove_from_timer();

        void push_to_timer();

        bool operator > (const TimerTask & oth) const
        {
            if (end_time != oth.end_time)
                return end_time > oth.end_time;

            return this > std::addressof(oth);
        }

        bool operator < (const TimerTask & oth) const
        {
            if (end_time != oth.end_time)
                return end_time < oth.end_time;

            return this < std::addressof(oth);
        }

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
        using callback_t = std::function<void(bool)>;
        using apply_cb_t = std::function<void()>;

        constexpr static microseconds InvalidTime = microseconds(0);
        constexpr static microseconds TickInterval = microseconds(1500);

        spin_lock_sleep m_lock{};
        std::multiset<TimerTaskPtr, TimerTask::Comparator> m_task{};

        std::vector<TimerTaskPtr> pick_all_expired(microseconds end_point);
        void tick();
        void process_expired();
        void push_to_timers(const TimerTaskPtr &, bool enable_lock = true);
        TimerTaskPtr create_task(const std::function<void(bool)> &callback);
        void apply_task_until(TimerTaskPtr & task, microseconds end_time, const apply_cb_t &callback = apply_cb_t{});
        void apply_task(TimerTaskPtr & task, microseconds duration);
        TimerTaskPtr add_task(microseconds duration, const callback_t & callback);
        TimerTaskPtr add_task_until(microseconds end_time, const callback_t & callback);
    };
}