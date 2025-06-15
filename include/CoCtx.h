#pragma once

#include <cstdint>
#include <thread>

#ifdef __DEBUG_SCHED__
#include <unordered_set>
#include "../utils/include/spin_lock.h"
#endif

#include "../io/include/epoller_def.h"
#include "../utils/include/tscns.h"
#include "../utils/include/Singleton.hpp"
#include "../sched/include/SchedulerDef.h"
#include "../timer/include/TimerDef.h"
#include "AllocatorGroup.h"
#include "xor_shift_rand.h"

namespace co {
    struct local_t
    {
        std::thread::id thread_id{};
        AllocatorGroup alloc{};
        Scheduler *scheduler{};
        std::shared_ptr<Timer> timer{};
        xor_shift_rand_64 rand{};
    };

    struct global_t
    {
        bool is_init;
        std::shared_ptr<SchedManager> manager;
        AllocatorGroup * g_alloc;
        TSCNS clock;
        std::shared_ptr<Timer> timer{};
#ifdef __DEBUG_SCHED__
        std::unordered_set<Co_t*> co_vec;
        std::unordered_multiset<Co_t*> running_co;
        spin_lock removal_lock;
#endif
    };

    namespace co_ctx
    {
        extern bool is_init;
        extern std::shared_ptr<SchedManager> manager;
        extern AllocatorGroup * g_alloc;
        extern TSCNS clock;
        extern std::shared_ptr<Epoller> epoller;
#ifdef __DEBUG_SCHED__
        extern std::unordered_set<Co_t*> co_vec;
        extern std::unordered_multiset<Co_t*> running_co;
        extern spin_lock removal_lock;
#endif
        extern thread_local std::shared_ptr<local_t> loc;
    }
}