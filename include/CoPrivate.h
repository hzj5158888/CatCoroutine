//
// Created by hzj on 25-1-9.
//
#pragma once

#include <cstdint>
#include <queue>
#include <atomic>

#include "../context/include/Context.h"
#include "../include/Coroutine.h"
#include "../allocator/include/MemoryPool.h"
#include "../../sched/include/SchedulerDef.h"
#include "../../sched/include/CfsSchedEntity.h"
#include "../../data_structure/include/QueueLockFree.h"
#include "../../allocator/include/PmrAllocator.h"

enum CO_STATUS
{
    CO_NEW = 1,
    CO_READY,
    CO_RUNNING,
	CO_INTERRUPT,
    CO_WAITING,
	CO_DEAD
};

struct Co_t
{
    uint32_t id{co::INVALID_CO_ID};
    std::atomic<uint8_t> status{CO_NEW};

	// 状态锁
	spin_lock status_lock{};

	// 分配器信息
#ifdef __MEM_PMR__
	std::pmr::synchronized_pool_resource * allocator{};
#else
	MemoryPool * allocator{};
#endif

    // 上下文
    Context ctx{};
	// 栈是否活跃
	spin_lock stk_active_lock{};

	// await
	Co_t * await_callee{}; // await 谁
    spin_lock await_caller_lock{};
    std::queue<Co_t*> await_caller{}; // 谁 await
#ifdef __DEBUG__
    void * sem_ptr{};
#endif

    // 调度信息
#ifdef __SCHED_CFS__
	alignas(__CACHE_LINE__) CfsSchedEntity sched{};
#elif __SCHED_FIFO__
    alignas(__CACHE_LINE__) SchedEntity sched{};
#endif
	Scheduler * scheduler{};

	Co_t() = default;
	Co_t(const Co_t & oth) = delete;
	Co_t(Co_t && oth) = delete;
	~Co_t() = default;

#ifdef __SCHED_CFS__
	bool operator > (const Co_t & oth) const;
	bool operator < (const Co_t & oth) const;
#endif
	void operator delete (void * ptr) noexcept = delete;
};

template<>
struct std::hash<Co_t>
{
	std::size_t operator() (const Co_t & oth) const noexcept { return oth.id; }
};