#pragma once

#include "../../include/CoCtx.h"
#include "SchedEntity.h"

namespace co {
    /* include/linux/sched.h */
    constexpr static int nice_to_weigh[] = {
            /* -20 */     88761, 71755, 56483, 46273, 36291,
            /* -15 */     29154, 23254, 18705, 14949, 11916,
            /* -10 */     9548, 7620, 6100, 4904, 3906,
            /*  -5 */     3121, 2501, 1991, 1586, 1277,
            /*   0 */     1024, 820, 655, 526, 423,
            /*   5 */     335, 272, 215, 172, 137,
            /*  10 */     110, 87, 70, 56, 45,
            /*  15 */     36, 29, 23, 18, 15,
    };

    class CfsSchedEntity : public SchedEntity
    {
    public:
        constexpr static int nice_offset = 20;
        /* ~128us */
        constexpr static int precision = 17;

        int nice{}, prev_nice{};
        uint64_t v_runtime{};
        uint64_t real_runtime{};
        uint64_t real_runtime_ns{};
        uint64_t start_exec_timestamp{};
        uint64_t end_exec_timestamp{};

        [[nodiscard]] uint64_t priority() const { return v_runtime; }

        void up_real_runtime()
        {
            real_runtime_ns += end_exec_timestamp - start_exec_timestamp;
            real_runtime = (real_runtime_ns >> precision) << precision;
            end_exec_timestamp = start_exec_timestamp = 0;
        }

        void start_exec() override
        {
            //auto now = std::chrono::system_clock::now();
            //start_exec_timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
            start_exec_timestamp = co_ctx::clock.rdns();
        }

        void end_exec() override
        {
            //auto now = std::chrono::system_clock::now();
            //end_exec_timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
            end_exec_timestamp = co_ctx::clock.rdns();
        }

        void prefetch() const { __builtin_prefetch(std::addressof(v_runtime), 0, 3); }

        void up_nice(int cur_nice)
        {
            assert(cur_nice + nice_offset >= 0 && cur_nice + nice_offset < 40);
            v_runtime = (real_runtime << 10) / nice_to_weigh[cur_nice + nice_offset];
            prev_nice = nice;
            nice = cur_nice;
        }

        void back_nice()
        {
            v_runtime = (real_runtime << 10) / nice_to_weigh[prev_nice + nice_offset];
            std::swap(nice, prev_nice);
        }

        void up_v_runtime()
        {
            up_real_runtime();
            v_runtime = (real_runtime << 10) / nice_to_weigh[nice + nice_offset];
        }
    };

}