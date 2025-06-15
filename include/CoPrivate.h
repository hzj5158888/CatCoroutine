//
// Created by hzj on 25-1-9.
//
#pragma once

#include <cstdint>
#include <queue>
#include <atomic>
#include <stack>

#include "../context/include/Context.h"
#include "../include/Coroutine.h"
#include "../allocator/include/MemoryPool.h"
#include "../../sched/include/SchedulerDef.h"
#include "../../sched/include/CfsSchedEntity.h"

namespace co {
    enum CoStatus {
        CO_NEW = 1,
        CO_READY,
        CO_RUNNING,
        CO_INTERRUPT,
        CO_WAITING,
        CO_DEAD
    };

    enum CoWakeupReasons {
        CO_WAKEUP_NONE = 0,
        CO_WAKEUP_TIMER
    };

    struct Co_t
    {
        bool is_main_co{};
        std::atomic<uint8_t> status{CO_NEW};
        spin_lock status_lock{};
        uint16_t wakeup_reason{};

        // 调度信息
#ifdef __SCHED_CFS__
        CfsSchedEntity sched{};
#elif __SCHED_FIFO__
        SchedEntity sched{};
#endif
        Scheduler * scheduler{};

        /* 用于通道唤醒后接收数据
         */
        void * recv_buffer{};
        bool buffer_has_value{};

        // 上下文
        Context ctx{};
        spin_lock stk_active_lock{}; // 栈是否活跃

        // 分配器信息
#ifdef __MEM_PMR__
        std::pmr::synchronized_pool_resource * allocator{};
#else
        MemoryPool *allocator{};
#endif

        // await
        Co_t *await_callee{}; // await 谁
        spin_lock await_caller_lock{};
        std::vector<Co_t *> await_caller{}; // 谁 await
#ifdef __DEBUG_SEM_TRACE__
        void * sem_ptr{};
        TimerTaskPtr cur_task{};
        std::vector<std::string> sem_wakeup_reason{};
#endif

        Co_t() = default;
        Co_t(const Co_t &oth) = delete;
        Co_t(Co_t &&oth) = delete;
        ~Co_t() = default;

        bool operator>(const Co_t &oth) const;
        bool operator<(const Co_t &oth) const;
    };
}